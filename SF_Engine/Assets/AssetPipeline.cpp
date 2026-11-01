#include "AssetPipeline.hpp"

#include <Assets/Audio/AudioAsset.hpp>
#include <Assets/Models/MeshAsset.hpp>
#include <Engine/Log/Log.hpp>
#include <Engine/Project/Project.hpp>
#include <LowLevel/FileSystem/File.hpp>
#include <LowLevel/XML/XMLModule.hpp>
#include <Rendering/RHI/Images/Image.hpp>

#include <unordered_set>

namespace SF::Engine
{
    std::unordered_map<std::string, AssetFactoryFn> &AssetController::Factories()
    {
        static std::unordered_map<std::string, AssetFactoryFn> factories;
        return factories;
    }

    void AssetController::RegisterFactory(const std::string &rttiTypeName, AssetFactoryFn factory)
    {
        auto &factories = Factories();
        if (factories.contains(rttiTypeName))
        {
            Log::Error("AssetController: factory for '{}' registered more than once; ignoring duplicate.",
                       rttiTypeName);
            return;
        }
        factories.emplace(rttiTypeName, std::move(factory));
    }

    void AssetController::RegisterBuiltinFactories()
    {
        static bool registered = false;
        if (registered)
            return;
        registered = true;

        // Temporaries: constructing an AssetRegistrar is what registers the factory.
        (void) AssetRegistrar<MeshAsset>{};
        (void) AssetRegistrar<AudioAsset>{};
        (void) AssetRegistrar<ImageAsset<Image2d>>{};
    }

    bool AssetController::Initialize()
    {
        RegisterBuiltinFactories();
        assets_ = vector<shared_ptr<AssetBase>>();
        return true;
    }

    // Loads one "<source>.meta" into a live asset. Returns false (and logs) if it can't.
    bool AssetController::LoadMetaFile(const std::filesystem::path &metaPath)
    {
        XMLModule *reader = XMLModule::Get();
        if (!reader->LoadFromFile(metaPath.string()))
        {
            Log::Error("AssetController: failed to parse meta '{}': {}", metaPath.string(), reader->GetLastError());
            return false;
        }

        XMLNode metaRoot  = reader->GetRootNode();
        XMLNode assetNode = metaRoot.GetChild("Asset");
        if (!assetNode.IsValid())
        {
            Log::Error("AssetController: meta '{}' has no <Asset> node; skipping.", metaPath.string());
            return false;
        }

        std::string concreteType;
        assetNode.GetAttribute("ConcreteType", concreteType);

        const auto &factories = Factories();
        auto factoryIt        = factories.find(concreteType);
        if (factoryIt == factories.end())
        {
            std::string name;
            assetNode.GetAttribute("Name", name);
            Log::Error("AssetController: no factory for type '{}' (asset '{}'); skipping.", concreteType, name);
            return false;
        }

        std::shared_ptr<AssetBase> asset = factoryIt->second();

        // "Foo.shader.meta" -> "Foo.shader" (only the trailing .meta is stripped)
        std::filesystem::path assetDataPath = metaPath;
        assetDataPath.replace_extension();

        asset->assetPath = assetDataPath;
        asset->Deserialize(metaRoot);

        // A copied/duplicated .meta carries the original's UUID. Give the copy its own identity.
        if (auto clash = FindByUUID(asset->uuid); clash && clash->assetPath != asset->assetPath)
        {
            Log::Warning("AssetController: '{}' has the same asset ID as '{}'; assigning a new one.",
                         assetDataPath.string(), clash->assetPath.string());
            asset->uuid = UUID::Generate();
            asset->SaveMeta();
        }

        assets_.push_back(std::move(asset));
        return true;
    }

    void AssetController::ProjectLoaded()
    {
        // Drop assets that came from a previous project's disk scan so reloading doesn't duplicate them.
        // Procedural assets (no assetPath, e.g. engine LUTs registered at startup) are kept.
        std::erase_if(assets_, [](const std::shared_ptr<AssetBase> &a) { return a && !a->assetPath.empty(); });

        const std::filesystem::path assetsRoot = ProjectManager::Get()->GetProjectAssetPath();
        if (!std::filesystem::exists(assetsRoot))
        {
            Log::Warning("AssetController: assets folder '{}' does not exist yet.", assetsRoot.string());
            return;
        }

        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(assetsRoot, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
            {
                Log::Error("AssetController: error scanning '{}': {}", assetsRoot.string(), ec.message());
                break;
            }
            if (it->is_directory() || it->path().extension() != ".meta")
                continue;

            LoadMetaFile(it->path());
        }

        Log::Info("AssetController: loaded {} assets from '{}'.", assets_.size(), assetsRoot.string());
    }

    bool AssetController::SyncWithDisk(bool scanForNew)
    {
        auto *projects = ProjectManager::Get();
        if (!projects || !projects->IsAProjectLoaded())
            return false;

        namespace fs = std::filesystem;
        bool changed = false;

        // 1) Deleted outside the editor: the source or its .meta is gone. A stat error is not "gone".
        const std::size_t removed = std::erase_if(assets_,
                                                  [](const std::shared_ptr<AssetBase> &a)
                                                  {
                                                      if (!a || a->assetPath.empty())
                                                          return false;
                                                      std::error_code e1, e2;
                                                      fs::path meta = a->assetPath;
                                                      meta += ".meta";
                                                      const bool srcExists  = fs::exists(a->assetPath, e1);
                                                      const bool metaExists = fs::exists(meta, e2);
                                                      return (!e1 && !srcExists) || (!e2 && !metaExists);
                                                  });
        if (removed > 0)
        {
            Log::Info("AssetController: {} asset(s) were removed from disk.", removed);
            changed = true;
        }

        // 2) Appeared outside the editor: a .meta (with its source) we don't know about yet.
        if (scanForNew)
        {
            const fs::path root = projects->GetProjectAssetPath();
            std::error_code ec;
            if (fs::exists(root, ec))
            {
                std::unordered_set<std::string> known;
                for (const auto &a: assets_)
                {
                    if (a && !a->assetPath.empty())
                    {
                        std::error_code e;
                        known.insert(fs::weakly_canonical(a->assetPath, e).generic_string());
                    }
                }

                for (auto it =
                             fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
                     !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                {
                    if (it->path().extension() != ".meta")
                        continue;

                    std::error_code e;
                    if (!it->is_regular_file(e))
                        continue;

                    fs::path source = it->path();
                    source.replace_extension();
                    if (!fs::exists(source, e))
                        continue;
                    if (known.contains(fs::weakly_canonical(source, e).generic_string()))
                        continue;

                    if (LoadMetaFile(it->path()))
                        changed = true;
                }
            }
        }
        return changed;
    }

    void AssetController::SaveAll()
    {
        for (auto &asset: assets_)
        {
            if (!asset)
                continue;
            asset->Save();     // write the payload/data file
            asset->SaveMeta(); // write just this asset's .meta sidecar
        }
    }

    std::shared_ptr<AssetBase> AssetController::FindByUUID(const UUID &guid) const
    {
        for (const auto &asset: assets_)
        {
            if (asset && asset->uuid == guid)
            {
                return asset;
            }
        }

        return nullptr;
    }

    std::shared_ptr<AssetBase> AssetController::FindByName(std::string_view name) const
    {
        for (const auto &asset: assets_)
        {
            if (asset && asset->name == name)
            {
                return asset;
            }
        }

        return nullptr;
    }

    std::shared_ptr<AssetBase> AssetController::FindByPath(const std::filesystem::path &path) const
    {
        if (path.empty())
            return nullptr;

        std::error_code ec;
        const std::filesystem::path wanted = std::filesystem::weakly_canonical(path, ec);

        for (const auto &asset: assets_)
        {
            if (!asset || asset->assetPath.empty())
                continue;
            if (asset->assetPath == path)
                return asset;

            std::error_code ec2;
            if (!ec && std::filesystem::weakly_canonical(asset->assetPath, ec2) == wanted && !ec2)
                return asset;
        }
        return nullptr;
    }

    void AssetController::AddAsset(std::shared_ptr<AssetBase> asset)
    {
        if (asset)
            assets_.push_back(std::move(asset));
    }

    bool AssetController::RemoveAsset(const UUID &guid)
    {
        File::Delete(FindByUUID(guid)->assetPath);
        return std::erase_if(assets_, [&](const std::shared_ptr<AssetBase> &a) { return a && a->uuid == guid; }) > 0;
    }

    std::size_t AssetController::RemoveAssetsUnder(const std::filesystem::path &folder)
    {
        const std::filesystem::path base = folder.lexically_normal();
        return std::erase_if(assets_,
                             [&](const std::shared_ptr<AssetBase> &a)
                             {
                                 if (!a || a->assetPath.empty())
                                     return false;
                                 const auto rel = a->assetPath.lexically_normal().lexically_relative(base);
                                 return !rel.empty() && *rel.begin() != std::filesystem::path("..");
                             });
    }

    void AssetController::SaveManifest()
    {
        if (!ProjectManager::Get()->IsAProjectLoaded())
        {
            Log::Warning("AssetController: SaveManifest called with no project loaded; ignoring.");
            return;
        }

        const std::filesystem::path manifestPath = ProjectManager::Get()->GetProjectAssetPath() / "AssetManifest.xml";

        XMLModule *writer = XMLModule::Get();
        writer->SetRootNode("AssetManifest");
        XMLNode root = writer->GetRootNode();

        for (const auto &asset: assets_)
        {
            if (asset)
                asset->Serialize(root);
        }

        if (!writer->SaveToFile(manifestPath.string()))
        {
            Log::Error("AssetController: failed to save asset manifest to '{}'", manifestPath.string());
        }
    }
} // namespace SF::Engine
