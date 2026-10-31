#include "AssetsWindow.hpp"
#include <Assets/Audio/AudioAsset.hpp>
#include <Assets/Import/AssetImporter.hpp>
#include <Assets/Models/MeshAsset.hpp>
#include <Controllers/CameraController.hpp>
#include <Engine/Project/Project.hpp>
#include <Gui/ImGui/CommonGuiFunctions.hpp>
#include <Gui/ImGui/ocornut/imgui_impl_vulkan.h>
#include <LowLevel/FileSystem/File.hpp>
#include <Platform/Windowing/WindowManager.hpp>
#include <Scene/Scene.hpp>
#include "../Wizards/Shaders.hpp"
#include "Panels.hpp"
#include "Scene/SceneManager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <system_error>

namespace SF::Engine
{
    namespace
    {
        constexpr const char *kTypeFilterNames[] = {"All",
                                                    "Mesh",
                                                    "Texture",
                                                    "Shader",
                                                    "LuaScript",
                                                    "CppCode",
                                                    "VFX",
                                                    "Audio",
                                                    "SkeletalAnimation",
                                                    "Scene",
                                                    "TemplateAsset",
                                                    "ConfigFile",
                                                    "Font",
                                                    "Material",
                                                    "Library",
                                                    "AnimationStateMachine"};

        constexpr char kWindowTitle[]   = ICON_MD_FOLDER " Asset Browser";
        constexpr char kAssetPayload[]  = "SF_ASSET";  // ImGui drag-drop payload: a 16-byte asset UUID
        constexpr char kFolderPayload[] = "SF_FOLDER"; // payload: UTF-8 path of a folder
        constexpr char kScenePayload[]  = "SF_SCENE";  // payload: UTF-8 path of a scene file
        constexpr float kGroundY        = 0.0f;        // placement plane when dropping onto the 3D view
        constexpr float kMaxPlaceDist   = 500.0f;      // ray hits further than this fall back to a point in front

        constexpr double kDirPollIntervalSec     = 0.5;
        constexpr float kListPreviewSize         = 50.0f;
        constexpr float kDetailsPreviewMaxHeight = 300.0f;
        constexpr uint32_t kDefaultImageSize     = 256;
        constexpr VkFormat kDefaultImageFormat   = VK_FORMAT_R8G8B8A8_UNORM;

        constexpr Vec4 kHeadingColor(0.5f, 0.8f, 1.0f, 1.0f);
        constexpr Vec4 kErrorColor(1.0f, 0.3f, 0.3f, 1.0f);
        constexpr Vec4 kWarningColor(1.0f, 0.6f, 0.2f, 1.0f);
        constexpr Vec4 kCurrentSceneColor(0.45f, 0.80f, 1.0f, 1.0f);

        struct ViewModeInfo
        {
            AssetBrowser::ViewMode mode;
            const char *icon;
            const char *menuLabel;
        };
        constexpr ViewModeInfo kViewModes[] = {
                {AssetBrowser::ViewMode::Grid, ICON_MD_GRID_ON, ICON_MD_GRID_ON " Grid"},
                {AssetBrowser::ViewMode::List, ICON_MD_FORMAT_LIST_BULLETED, ICON_MD_FORMAT_LIST_BULLETED " List"},
                {AssetBrowser::ViewMode::Details, ICON_MD_DETAILS, ICON_MD_DETAILS " Details"}};

        // Pushes a two-level ID ("folder" + "Textures") so a folder and a scene file
        // that share a name can never collide.
        struct ScopedId
        {
            ScopedId(const char *scope, const string &name)
            {
                ImGui::PushID(scope);
                ImGui::PushID(name.c_str());
            }
            ~ScopedId()
            {
                ImGui::PopID();
                ImGui::PopID();
            }
            ScopedId(const ScopedId &)            = delete;
            ScopedId &operator=(const ScopedId &) = delete;
        };

        string ToLower(string_view s)
        {
            string out(s);
            ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
            return out;
        }

        bool ContainsNoCase(string_view haystack, string_view needle)
        {
            if (needle.empty())
                return true;
            const auto it =
                    ranges::search(haystack, needle, [](char h, char n)
                                   { return tolower(static_cast<unsigned char>(h)) == static_cast<unsigned char>(n); })
                            .begin();
            return it != haystack.end();
        }

        bool LessNoCase(const string &a, const string &b)
        {
            return ranges::lexicographical_compare(a, b, [](unsigned char x, unsigned char y)
                                                   { return tolower(x) < tolower(y); });
        }

        string CleanFileName(string s)
        {
            for (char &c: s)
                if (strchr("<>:\"/\\|?*", c) || static_cast<unsigned char>(c) < 32)
                    c = '_';
            s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1); // trailing whitespace (npos + 1 == 0 -> clears all-blank)
            return s;
        }

        bool IsSceneFile(const filesystem::path &p)
        {
            const string f            = p.filename().string();
            constexpr string_view ext = SceneFileExtension;
            return f.size() > ext.size() && f.compare(f.size() - ext.size(), ext.size(), ext) == 0;
        }

        string SceneDisplayName(const filesystem::path &p)
        {
            string name = p.filename().string();
            name.resize(name.size() - SceneFileExtension.size());
            return name;
        }

        bool IsSameOrInside(const filesystem::path &path, const filesystem::path &folder)
        {
            const auto rel = path.lexically_relative(folder);
            return !rel.empty() && *rel.begin() != filesystem::path("..");
        }

        string PathToPayload(const filesystem::path &p)
        {
            const u8string u8 = p.u8string();
            return {reinterpret_cast<const char *>(u8.data()), u8.size()};
        }

        filesystem::path PathFromPayload(const ImGuiPayload *payload)
        {
            return {u8string(static_cast<const char8_t *>(payload->Data), static_cast<size_t>(payload->DataSize))};
        }

        string ToHex(uint32_t v)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "0x%X", v);
            return buf;
        }

        // Scale `src` to fit inside `box`, preserving aspect ratio.
        Vec2 FitInside(Vec2 src, Vec2 box, bool allowUpscale)
        {
            if (src.x <= 0.0f || src.y <= 0.0f)
                return box;
            float scale = min(box.x / src.x, box.y / src.y);
            if (!allowUpscale)
                scale = min(scale, 1.0f);
            return {src.x * scale, src.y * scale};
        }

        void ToolbarGap()
        {
            ImGui::SameLine();
            ImGui::Dummy(Vec2(10, 0));
            ImGui::SameLine();
        }

        void Heading(const char *text) { ImGui::TextColored(kHeadingColor, "%s", text); }

        void DrawIcon(const shared_ptr<Image2d> &icon, Vec2 size)
        {
            if (icon)
                ImGui::Image(icon->GetTexID(), size);
            else
                ImGui::Dummy(size);
        }

        bool BeginPropertyTable(const char *id)
        {
            if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                return false;
            ImGui::TableSetupColumn(ICON_MD_LABEL " Property", ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            return true;
        }

        void PropertyRow(const char *label, string_view value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(value.data(), value.data() + value.size());
        }


        template<typename TImage, typename Fn>
        bool TryVisit(const shared_ptr<AssetBase> &asset, Fn &fn)
        {
            if (const auto image = ::SF::RTTI::rtti_pointer_cast<ImageAsset<TImage>>(asset))
            {
                fn(image);
                return true;
            }
            return false;
        }

        template<typename Fn>
        bool VisitImageAsset(const shared_ptr<AssetBase> &asset, Fn &&fn)
        {
            return TryVisit<Image2d>(asset, fn) || TryVisit<Image3d>(asset, fn) || TryVisit<Image2dArray>(asset, fn) ||
                   TryVisit<Cubemap>(asset, fn);
        }

        const char *GetAssetIcon(AssetType type)
        {
            switch (type)
            {
                case AssetType::Texture:
                    return ICON_MD_IMAGE;
                case AssetType::Mesh:
                    return ICON_MD_3D_ROTATION;
                case AssetType::Shader:
                    return ICON_MD_CODE;
                case AssetType::Material:
                    return ICON_MD_PALETTE;
                case AssetType::Audio:
                    return ICON_MD_AUDIO_FILE;
                case AssetType::Scene:
                    return ICON_MD_WEB;
                case AssetType::Font:
                    return ICON_MD_FONT_DOWNLOAD;
                case AssetType::LuaScript:
                case AssetType::CppCode:
                    return ICON_MD_TERMINAL;
                default:
                    return ICON_MD_INSERT_DRIVE_FILE;
            }
        }

        const char *GetAssetTypeName(AssetType type)
        {
            switch (type)
            {
                case AssetType::Mesh:
                    return "Mesh";
                case AssetType::Texture:
                    return "Texture";
                case AssetType::Shader:
                    return "Shader";
                case AssetType::LuaScript:
                    return "Lua Script";
                case AssetType::CppCode:
                    return "C++ Code";
                case AssetType::VFX:
                    return "VFX";
                case AssetType::Audio:
                    return "Audio";
                case AssetType::SkeletalAnimation:
                    return "Skeletal Animation";
                case AssetType::Scene:
                    return "Scene";
                case AssetType::TemplateAsset:
                    return "Template";
                case AssetType::ConfigFile:
                    return "Config";
                case AssetType::Font:
                    return "Font";
                case AssetType::Material:
                    return "Material";
                case AssetType::Library:
                    return "Library";
                case AssetType::AnimationStateMachine:
                    return "Animation State Machine";
                default:
                    return "Unknown";
            }
        }

        const char *GetFormatName(VkFormat format)
        {
            switch (format)
            {
                case VK_FORMAT_R8G8B8A8_UNORM:
                    return "RGBA8_UNORM";
                case VK_FORMAT_R8G8B8_UNORM:
                    return "RGB8_UNORM";
                case VK_FORMAT_R8G8_UNORM:
                    return "RG8_UNORM";
                case VK_FORMAT_R8_UNORM:
                    return "R8_UNORM";
                case VK_FORMAT_R16G16B16A16_SFLOAT:
                    return "RGBA16_SFLOAT";
                case VK_FORMAT_R32G32B32A32_SFLOAT:
                    return "RGBA32_SFLOAT";
                case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
                    return "BC1_RGB";
                case VK_FORMAT_BC3_UNORM_BLOCK:
                    return "BC3_UNORM";
                default:
                    return "Unknown Format";
            }
        }

        const char *GetFilterName(VkFilter filter)
        {
            switch (filter)
            {
                case VK_FILTER_NEAREST:
                    return "Nearest";
                case VK_FILTER_LINEAR:
                    return "Linear";
                case VK_FILTER_CUBIC_IMG:
                    return "Cubic";
                default:
                    return "Unknown";
            }
        }

        const char *GetAddressModeName(VkSamplerAddressMode mode)
        {
            switch (mode)
            {
                case VK_SAMPLER_ADDRESS_MODE_REPEAT:
                    return "Repeat";
                case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
                    return "Mirrored Repeat";
                case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
                    return "Clamp to Edge";
                case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
                    return "Clamp to Border";
                case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
                    return "Mirror Clamp to Edge";
                default:
                    return "Unknown";
            }
        }
    } // namespace

    void AssetBrowser::Draw()
    {
        if (!ProjectManager::Get()->IsAProjectLoaded())
            return;

        EnsureIconsLoaded();
        SyncAssetsWithDisk(false);
        EnsureDropSubscribed();
        ProcessPendingDrops();
        ProcessSceneAssetDrag();

        if (!ImGui::Begin(kWindowTitle, nullptr, ImGuiWindowFlags_MenuBar))
        {
            m_windowRectValid = false;
            ImGui::End();
            return;
        }

        m_windowMin       = ImGui::GetWindowPos();
        m_windowMax       = Vec2(ImGui::GetWindowPos()) + Vec2(ImGui::GetWindowSize());
        m_windowRectValid = true;

        if (m_currentPath.empty())
            NavigateTo(ProjectManager::Get()->GetProjectAssetPath());

        DrawMenuBar();
        DrawToolbar();
        ImGui::Separator();

        // A pending "new scene" tile only makes sense in the folder it was started in.
        if (m_inlineEditMode == InlineEditMode::NewScene && m_inlineEditPath != m_currentPath)
            EndInlineEdit();

        DrawPathNavigation();
        ImGui::Separator();

        DrawFilterControls();
        ImGui::Separator();
        DrawStatus();

        if (m_inlineEditMode != InlineEditMode::None && ImGui::IsKeyPressed(ImGuiKey_Escape))
            EndInlineEdit();

        RefreshDirectoryCache();
        DrawContent();

        ImGui::End();

        DrawDeleteConfirmPopup();

        if (showCS)
            ShowCreateShaderWizard(m_currentPath);
        if (showCSI)
            ShowCreateShaderIncludeWizard(m_currentPath);
    }

    void AssetBrowser::EnsureIconsLoaded()
    {
        if (!m_icons.save)
            m_icons.save = GetSaveLogo();
        if (!m_icons.newFile)
            m_icons.newFile = GetNewLogo();
        if (!m_icons.hpp)
            m_icons.hpp = GetHppLogo();
        if (!m_icons.h)
            m_icons.h = GetHlogo();
        if (!m_icons.folder)
            m_icons.folder = GetFolderLogo();
        if (!m_icons.file)
            m_icons.file = GetFileLogo();
        if (!m_icons.c)
            m_icons.c = GetCLogo();
        if (!m_icons.cpp)
            m_icons.cpp = GetCppLogo();
        if (!m_icons.scene)
            m_icons.scene = GetSceneLogo();
        if (!m_icons.audio)
            m_icons.audio = GetAudioLogo();
    }

    void AssetBrowser::DrawContent()
    {
        const bool showDetails = m_viewMode == ViewMode::Details && m_selectedAsset.has_value();

        if (showDetails)
        {
            ImGui::Columns(2, "AssetSplitter", true);
            // Only set the width when the panel opens; doing it every frame makes the splitter undraggable.
            if (!m_detailsOpen)
                ImGui::SetColumnWidth(0, ImGui::GetContentRegionAvail().x * 0.6f);

            DrawAssetGrid();
            ImGui::NextColumn();
            DrawDetailsPanel();

            ImGui::Columns(1);
        } else
        {
            DrawAssetGrid();
        }

        m_detailsOpen = showDetails;
    }

    void AssetBrowser::DrawMenuBar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu(ICON_MD_INSERT_DRIVE_FILE " File"))
            {
                if (ImGui::BeginMenu(ICON_MD_ADD " New Asset"))
                {
                    DrawCreateAssetItems();
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem(ICON_MD_REFRESH " Refresh"))
                    RefreshAssets();
                if (ImGui::MenuItem(ICON_MD_SAVE " Save All"))
                    AssetController::Get()->SaveAll();
                if (ImGui::MenuItem(ICON_MD_PUBLIC " Scene"))
                    BeginNewScene();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                for (const ViewModeInfo &info: kViewModes)
                    if (ImGui::MenuItem(info.menuLabel, nullptr, m_viewMode == info.mode))
                        m_viewMode = info.mode;

                ImGui::Separator();
                ImGui::MenuItem(ICON_MD_IMAGE " Show Thumbnails", nullptr, &m_showThumbnails);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(ICON_MD_HELP " Help"))
            {
                ImGui::MenuItem(ICON_MD_INFO " About", nullptr, false);
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        // Opened by the "+" button in the toolbar.
        if (ImGui::BeginPopup("CreateAssetPopup"))
        {
            DrawCreateAssetItems();
            ImGui::EndPopup();
        }
    }

    void AssetBrowser::DrawCreateAssetItems()
    {
        if (ImGui::MenuItem(ICON_MD_CROP_ORIGINAL " Image2D"))
            CreateImageAsset<Image2d>();
        if (ImGui::MenuItem(ICON_MD_CROP " Image3D"))
            CreateImageAsset<Image3d>();
        if (ImGui::MenuItem(ICON_MD_VIEW_COLUMN " Image2D Array"))
            CreateImageAsset<Image2dArray>();
        if (ImGui::MenuItem(ICON_MD_3D_ROTATION " Cubemap"))
            CreateImageAsset<Cubemap>();
    }

    void AssetBrowser::DrawToolbar()
    {
        ImGui::BeginGroup();

        bool first = true;
        for (const ViewModeInfo &info: kViewModes)
        {
            if (!first)
                ImGui::SameLine();
            first = false;

            const bool active = m_viewMode == info.mode;
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(active ? ImGuiCol_ButtonActive : ImGuiCol_Button));
            if (ImGui::Button(info.icon))
                m_viewMode = info.mode;
            ImGui::PopStyleColor();
        }

        ToolbarGap();

        if (ImGui::Button(ICON_MD_ADD))
            ImGui::OpenPopup("CreateAssetPopup");
        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_REFRESH))
            RefreshAssets();

        ToolbarGap();

        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat(ICON_MD_ZOOM_IN, &m_thumbnailSize, 32.0f, 200.0f, "%.0f");

        ToolbarGap();

        ImGui::Checkbox(ICON_MD_IMAGE " Thumbs", &m_showThumbnails);

        ImGui::EndGroup();
    }

    void AssetBrowser::DrawPathNavigation()
    {
        ImGui::BeginGroup();

        // TODO: real back/forward history. For now Back behaves like Up and Forward does nothing.
        if (ImGui::ArrowButton("##back", ImGuiDir_Left))
            NavigateUp();
        ImGui::SameLine();
        ImGui::ArrowButton("##forward", ImGuiDir_Right);
        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_ARROW_UPWARD))
            NavigateUp();
        // Dropping something on "up" moves it to the parent folder (never above the Assets root).
        if (m_currentPath.has_parent_path() &&
            IsSameOrInside(m_currentPath.parent_path(), ProjectManager::Get()->GetProjectAssetPath()))
            AcceptBrowserDrop(m_currentPath.parent_path());
        ImGui::SameLine();

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
        // Note: the 3rd argument of InputText is the buffer *size*, flags come 4th.
        ImGui::InputText("##path", m_currentPathText.data(), m_currentPathText.size() + 1,
                         ImGuiInputTextFlags_ReadOnly);

        ImGui::EndGroup();
    }

    void AssetBrowser::DrawFilterControls()
    {
        ImGui::BeginGroup();

        ImGui::SetNextItemWidth(250);
        if (ImGui::InputTextWithHint("##search", ICON_MD_SEARCH " Search assets...", m_searchBuffer,
                                     sizeof(m_searchBuffer)))
            m_searchFilter = ToLower(m_searchBuffer);

        ImGui::SameLine();

        int currentType = static_cast<int>(m_typeFilter) + 1; // +1 because -1 = All
        ImGui::SetNextItemWidth(180);
        if (ImGui::Combo("##typefilter", &currentType, kTypeFilterNames, IM_ARRAYSIZE(kTypeFilterNames)))
            m_typeFilter = static_cast<AssetType>(currentType - 1);

        ImGui::EndGroup();
    }


    void AssetBrowser::NavigateTo(const filesystem::path &path)
    {
        if (path == m_currentPath)
            return;
        m_currentPath     = path;
        m_currentPathText = path.string();
    }

    void AssetBrowser::NavigateUp()
    {
        if (m_currentPath.has_parent_path())
            NavigateTo(m_currentPath.parent_path());
    }

    void AssetBrowser::RefreshAssets()
    {
        m_previewCache.clear();
        m_loadAttempted.clear();
        m_dirDirty = true;
        SyncAssetsWithDisk(/*force=*/true);
    }

    // Files deleted or added outside the editor. Pruning is a handful of stat calls per known asset, so it
    // runs every second; looking for new .meta files walks the whole Assets tree, so that is every 5 s
    // (or immediately on Refresh).
    void AssetBrowser::SyncAssetsWithDisk(bool force)
    {
        const double now = ImGui::GetTime();
        if (!force && now < m_nextAssetSync)
            return;
        m_nextAssetSync = now + 1.0;

        const bool scanForNew = force || now >= m_nextAssetScan;
        if (scanForNew)
            m_nextAssetScan = now + 5.0;

        auto *controller = AssetController::Get();
        if (!controller || !controller->SyncWithDisk(scanForNew))
            return;

        m_dirDirty = true;
        if (m_selectedAsset && !controller->FindByUUID(*m_selectedAsset))
            m_selectedAsset.reset();
        erase_if(m_previewCache, [&](const auto &entry) { return !controller->FindByUUID(entry.first); });
    }

    // Listing a directory is a syscall storm; doing it every frame (as before, twice) is what made
    // the browser slow. We now rebuild only when the path changes, something marks the cache dirty,
    // or the folder's mtime changes (polled a couple of times per second).
    void AssetBrowser::RefreshDirectoryCache()
    {
        namespace fs = filesystem;

        const double now = ImGui::GetTime();
        error_code ec;

        // The folder we're viewing was deleted (e.g. in Explorer): fall back to the nearest folder that exists.
        if (!fs::exists(m_currentPath, ec) && !ec)
        {
            const fs::path root = ProjectManager::Get()->GetProjectAssetPath();
            fs::path p          = m_currentPath;
            while (p != root && p != p.parent_path() && !fs::exists(p, ec))
                p = p.parent_path();
            NavigateTo(fs::exists(p, ec) ? p : root);
            m_dirDirty = true;
        }

        if (!m_dirDirty && m_cachedPath == m_currentPath)
        {
            if (now < m_nextDirPoll)
                return;
            m_nextDirPoll = now + kDirPollIntervalSec;

            const auto stamp = fs::last_write_time(m_currentPath, ec);
            if (!ec && stamp == m_cachedStamp)
                return;
        }

        m_dirDirty    = false;
        m_cachedPath  = m_currentPath;
        m_nextDirPoll = now + kDirPollIntervalSec;
        m_cachedStamp = fs::last_write_time(m_currentPath, ec);

        m_folders.clear();
        m_sceneFiles.clear();

        for (fs::directory_iterator it(m_currentPath, ec), end; !ec && it != end; it.increment(ec))
        {
            error_code entryEc;
            const fs::path &path = it->path();

            if (it->is_directory(entryEc))
                m_folders.push_back(DirEntry{path, path.filename().string()});
            else if (it->is_regular_file(entryEc) && IsSceneFile(path))
                m_sceneFiles.push_back(DirEntry{path, SceneDisplayName(path)});
        }

        const auto byName = [](const DirEntry &a, const DirEntry &b) { return LessNoCase(a.name, b.name); };
        ranges::sort(m_folders, byName);
        ranges::sort(m_sceneFiles, byName);
    }

    filesystem::path AssetBrowser::CreateUniqueFolder(const filesystem::path &parent, const string &baseName)
    {
        filesystem::path candidate = parent / baseName;
        int suffix                 = 1;
        while (File::Exists(candidate.string()))
            candidate = parent / (baseName + " (" + to_string(suffix++) + ")");
        return candidate;
    }

    void AssetBrowser::OpenSceneFile(const filesystem::path &file)
    {
        if (!OpenScene(file))
            Log::Error("Failed to open scene: {}", file.string());
    }

    void AssetBrowser::DrawAssetGrid()
    {
        const auto controller = AssetController::Get();
        if (controller == nullptr)
            return;

        ImGui::BeginChild("AssetGrid", Vec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        if (m_viewMode == ViewMode::List)
            DrawListView(controller->assets_);
        else
            DrawGridView(controller->assets_);

        // Right-click on empty grid space -> "New" menu
        if (ImGui::BeginPopupContextWindow("AssetBrowserContext",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            DrawContextMenu();
            ImGui::EndPopup();
        }

        ImGui::EndChild();
    }

    void AssetBrowser::DrawContextMenu()
    {
        if (!ImGui::BeginMenu("New"))
            return;

        if (ImGui::MenuItem("Folder"))
        {
            const auto path = CreateUniqueFolder(m_currentPath, "New Folder");
            if (File::CreateDirectory(path.string()))
            {
                m_dirDirty = true;
                BeginRenameFolder(path);
            }
        }
        if (ImGui::MenuItem("Prefab"))
        {
            // TODO: ShowPrefabWizard();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Shader"))
            showCS = true;
        if (ImGui::MenuItem("Shader Include"))
            showCSI = true;

        if (ImGui::BeginMenu("Script"))
        {
            // TODO: hook these up to script-creation wizards
            ImGui::MenuItem("C++ Header");
            ImGui::MenuItem("C++ Source");
            ImGui::MenuItem("Lua Source");
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Scene"))
            BeginNewScene();

        ImGui::EndMenu();
    }

    bool AssetBrowser::ShouldShowAsset(const shared_ptr<AssetBase> &asset) const
    {
        if (m_typeFilter != kAllTypes && asset->type != m_typeFilter)
            return false;
        // Imported assets live next to their .meta; show them only in their own folder.
        // Assets without a source file (procedural) show everywhere, as before.
        if (!asset->assetPath.empty() && asset->assetPath.parent_path() != m_currentPath)
            return false;
        return ContainsNoCase(asset->name, m_searchFilter);
    }


    void AssetBrowser::DrawGridView(const AssetList &assets)
    {
        const float cellSize = m_thumbnailSize + 11.0f; // card = thumbnail + 2 x 5 padding, plus cell spacing

        if (const int columns = max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellSize));
            !ImGui::BeginTable("AssetGrid", columns, ImGuiTableFlags_None))
            return;

        for (const DirEntry &folder: m_folders)
        {
            ImGui::TableNextColumn();
            DrawFolderTile(folder);
        }

        if (IsCreatingSceneHere())
        {
            ImGui::TableNextColumn();
            DrawNewSceneTile();
        }

        // Scene files live on disk, not in AssetController, so they need their own tiles.
        for (const DirEntry &scene: m_sceneFiles)
        {
            ImGui::TableNextColumn();
            DrawSceneFileTile(scene);
        }

        int index = 0;
        for (const auto &asset: assets)
        {
            if (!asset || !ShouldShowAsset(asset))
                continue;
            ImGui::TableNextColumn();
            DrawAssetTile(asset, index++);
        }

        ImGui::EndTable();
    }

    namespace
    {
        // Type colour shown as the bar under a card's thumbnail (same idea as the Unreal content browser).
        Vec4 AssetAccent(const AssetType type)
        {
            switch (type)
            {
                case AssetType::Texture:
                    return {0.89f, 0.34f, 0.34f, 1.0f};
                case AssetType::Mesh:
                    return {0.35f, 0.62f, 0.95f, 1.0f};
                case AssetType::Audio:
                    return {1.00f, 0.72f, 0.12f, 1.0f};
                case AssetType::Scene:
                    return {1.00f, 0.55f, 0.10f, 1.0f};
                case AssetType::Shader:
                    return {0.65f, 0.45f, 0.95f, 1.0f};
                case AssetType::Material:
                    return {0.35f, 0.80f, 0.40f, 1.0f};
                case AssetType::LuaScript:
                case AssetType::CppCode:
                    return {0.40f, 0.85f, 0.85f, 1.0f};
                default:
                    return {0.60f, 0.60f, 0.60f, 1.0f};
            }
        }

        uint32_t ToU32(const Vec4 &c) { return ImGui::ColorConvertFloat4ToU32(c); }
    } // namespace

    // One content-browser card, drawn like Unreal's: dark rounded card, thumbnail on top, a coloured type bar
    // under it, then the name (two lines max) and a dimmed type label. The whole card is one invisible button,
    // which is also the drag source / drop target, and is left as the "last item" for context menus.
    AssetBrowser::CardResult AssetBrowser::DrawCard(const CardDesc &card)
    {
        constexpr float kPad = 5.0f, kRound = 6.0f, kAccentH = 3.0f, kGap = 4.0f;

        const float thumb  = m_thumbnailSize;
        const float lineH  = ImGui::GetTextLineHeight();
        const float width  = thumb + kPad * 2.0f;
        const float height = kPad + thumb + kAccentH + kGap + lineH * 3.0f + kPad; // 2 name lines + type label

        const Vec2 min = ImGui::GetCursorScreenPos();
        const Vec2 max = min + Vec2(width, height);

        ImGui::InvisibleButton("##card", Vec2(width, height));
        const bool hovered = ImGui::IsItemHovered();

        CardResult result;
        result.clicked       = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        result.rightClicked  = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        result.doubleClicked = !card.editing && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

        if (!card.editing && card.dnd.payloadType && ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(card.dnd.payloadType, card.dnd.payload.data(), card.dnd.payload.size());
            ImGui::TextUnformatted(card.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (card.dnd.dropFolder)
            AcceptBrowserDrop(*card.dnd.dropFolder);

        ImDrawList *dl = ImGui::GetWindowDrawList();

        // card + thumbnail well
        dl->AddRectFilled(min, max, hovered ? IM_COL32(62, 62, 62, 255) : IM_COL32(44, 44, 44, 255), kRound);
        const Vec2 tMin = min + Vec2(kPad, kPad);
        const Vec2 tMax = tMin + Vec2(thumb, thumb);
        dl->AddRectFilled(tMin, tMax, IM_COL32(20, 20, 20, 255), 3.0f);

        if (card.thumbTex)
        {
            const Vec2 fitted = FitInside(card.thumbSize, Vec2(thumb) * card.thumbScale, true);
            const Vec2 off    = (Vec2(thumb) - fitted) * 0.5f;
            dl->AddImage(card.thumbTex, tMin + off, tMin + off + fitted);
        } else if (card.glyph)
        {
            constexpr float kGlyphScale = 2.0f;
            const Vec2 g                = Vec2(ImGui::CalcTextSize(card.glyph)) * kGlyphScale;
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * kGlyphScale, tMin + (Vec2(thumb) - g) * 0.5f,
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), card.glyph);
        }

        // type colour bar, full card width
        dl->AddRectFilled(Vec2(min.x, tMax.y), Vec2(max.x, tMax.y + kAccentH), ToU32(card.accent));

        // name + type label
        const auto nameMin = Vec2(min.x + kPad, tMax.y + kAccentH + kGap);
        const float textW  = width - kPad * 2.0f;
        if (card.editing)
        {
            ImGui::SetCursorScreenPos(nameMin);
            DrawInlineEditField(textW);
        } else
        {
            const uint32_t nameCol = card.highlighted ? ToU32(kCurrentSceneColor) : ImGui::GetColorU32(ImGuiCol_Text);
            dl->PushClipRect(nameMin, nameMin + Vec2(textW, lineH * 2.0f), true);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), nameMin, nameCol, card.name.c_str(), nullptr, textW);
            dl->PopClipRect();
        }
        dl->AddText(Vec2(nameMin.x, nameMin.y + lineH * 2.0f), IM_COL32(150, 150, 150, 255), card.typeLabel.c_str());

        // selection: tinted fill + border
        if (card.selected)
        {
            dl->AddRectFilled(min, max, IM_COL32(66, 150, 250, 40), kRound);
            dl->AddRect(min, max, IM_COL32(66, 150, 250, 255), kRound, 2.0f);
        }

        return result;
    }

    void AssetBrowser::DrawFolderTile(const DirEntry &folder)
    {
        const ScopedId id("folder", folder.name);
        const bool editing = IsEditing(InlineEditMode::RenameFolder, folder.path);

        CardDesc card;
        card.name       = folder.name;
        card.typeLabel  = "Folder";
        card.accent     = Vec4(1.0f, 0.72f, 0.2f, 1.0f);
        card.selected   = !m_selectedAsset && m_selectedPath == folder.path;
        card.editing    = editing;
        card.thumbScale = 0.7f;
        if (m_icons.folder)
        {
            card.thumbTex  = m_icons.folder->GetTexID();
            card.thumbSize = Vec2(1.0f, 1.0f); // square icon
        }
        card.dnd = TileDnD{
                .payloadType = kFolderPayload, .payload = PathToPayload(folder.path), .dropFolder = &folder.path};

        const auto [clicked, rightClicked, doubleClicked] = DrawCard(card);
        if (clicked || rightClicked)
        {
            m_selectedAsset.reset();
            m_selectedPath = folder.path;
        }
        if (doubleClicked)
            NavigateTo(folder.path);

        if (!editing)
            DrawFolderContextMenu(folder);
    }

    void AssetBrowser::DrawSceneFileTile(const DirEntry &scene)
    {
        const ScopedId id("scene", scene.name);
        const bool editing = IsEditing(InlineEditMode::RenameScene, scene.path);

        CardDesc card;
        card.name        = scene.name;
        card.typeLabel   = "Scene";
        card.accent      = AssetAccent(AssetType::Scene);
        card.selected    = !m_selectedAsset && m_selectedPath == scene.path;
        card.highlighted = IsFileCurrentCurrentScene(scene.path);
        card.editing     = editing;
        card.thumbScale  = 0.7f;
        if (m_icons.scene)
        {
            card.thumbTex  = m_icons.scene->GetTexID();
            card.thumbSize = Vec2(1.0f, 1.0f);
        }
        card.dnd = TileDnD{.payloadType = kScenePayload, .payload = PathToPayload(scene.path), .dropFolder = nullptr};

        const auto [clicked, rightClicked, doubleClicked] = DrawCard(card);
        if (clicked || rightClicked)
        {
            m_selectedAsset.reset();
            m_selectedPath = scene.path;
        }
        if (doubleClicked)
            OpenSceneFile(scene.path);

        if (!editing)
            DrawSceneContextMenu(scene);
    }

    void AssetBrowser::DrawNewSceneTile()
    {
        ImGui::PushID("newscene_tile");

        CardDesc card;
        card.typeLabel  = "Scene";
        card.accent     = AssetAccent(AssetType::Scene);
        card.editing    = true;
        card.thumbScale = 0.7f;
        if (m_icons.scene)
        {
            card.thumbTex  = m_icons.scene->GetTexID();
            card.thumbSize = Vec2(1.0f, 1.0f);
        }
        DrawCard(card);

        ImGui::PopID();
    }

    void AssetBrowser::DrawAssetTile(const shared_ptr<AssetBase> &asset, int index)
    {
        ImGui::PushID(index);

        const bool editing = !asset->assetPath.empty() && IsEditing(InlineEditMode::RenameAsset, asset->assetPath);

        CardDesc card;
        card.name      = asset->name;
        card.typeLabel = GetAssetTypeName(asset->type);
        card.accent    = AssetAccent(asset->type);
        card.selected  = m_selectedAsset && *m_selectedAsset == asset->uuid;
        card.editing   = editing;
        // Drag onto a folder to move it, or onto the 3D view to place it; see ProcessSceneAssetDrag().
        card.dnd = TileDnD{kAssetPayload, string(reinterpret_cast<const char *>(&asset->uuid), sizeof(UUID)), nullptr};

        Vec2 texSize{};
        if (ImTextureID tex = 0; m_showThumbnails && ResolveThumbnail(asset, tex, texSize))
        {
            card.thumbTex  = tex;
            card.thumbSize = texSize;
        } else if (asset->type == AssetType::Audio && m_icons.audio)
        {
            card.thumbTex   = m_icons.audio->GetTexID();
            card.thumbSize  = Vec2(1.0f, 1.0f);
            card.thumbScale = 0.7f;
        } else if (asset->type == AssetType::Scene && m_icons.scene)
        {
            card.thumbTex   = m_icons.scene->GetTexID();
            card.thumbSize  = Vec2(1.0f, 1.0f);
            card.thumbScale = 0.7f;
        } else
        {
            card.glyph = GetAssetIcon(asset->type);
        }

        if (const CardResult r = DrawCard(card); r.clicked || r.rightClicked)
            m_selectedAsset = asset->uuid;

        if (!editing)
            DrawAssetContextMenu(asset);
        ImGui::PopID();
    }

    // Draws exactly one item: the real preview, the scene logo, or a placeholder button.
    void AssetBrowser::DrawAssetThumbnail(const shared_ptr<AssetBase> &asset, Vec2 size)
    {
        if (m_showThumbnails && TryDrawThumbnail(asset, size))
            return;

        if (asset->type == AssetType::Scene && m_icons.scene)
        {
            ImGui::Image(m_icons.scene->GetTexID(), size);
            return;
        }
        if (asset->type == AssetType::Audio && m_icons.audio)
        {
            ImGui::Image(m_icons.audio->GetTexID(), size);
            return;
        }

        DrawPlaceholder(asset, "##thumb", size, Vec4(0.2f, 0.2f, 0.2f, m_showThumbnails ? 1.0f : 0.0f));
    }

    // Flat button with the asset-type glyph centred on it. The glyph is painted through the draw list,
    // so the button stays the "last item" (the old SetCursorPos dance also placed it below the button).
    void AssetBrowser::DrawPlaceholder(const shared_ptr<AssetBase> &asset, const char *id, Vec2 size, const Vec4 &color)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, color);
        ImGui::Button(id, size);
        ImGui::PopStyleColor();

        const char *icon = GetAssetIcon(asset->type);
        const Vec2 glyph = ImGui::CalcTextSize(icon);
        const Vec2 min   = ImGui::GetItemRectMin();
        const Vec2 max   = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddText(
                Vec2(min.x + (max.x - min.x - glyph.x) * 0.5f, min.y + (max.y - min.y - glyph.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), icon);
    }

    // Resolves an image asset to a ready-to-draw texture (loading it from its source on first use).
    bool AssetBrowser::ResolveThumbnail(const shared_ptr<AssetBase> &asset, ImTextureID &texture, Vec2 &size)
    {
        bool ok = false;
        VisitImageAsset(asset,
                        [&](const auto &image)
                        {
                            // Assets loaded from a .meta have no texture until something asks for it.
                            if (!image->texture)
                            {
                                if (image->assetPath.empty() || !m_loadAttempted.insert(image->uuid).second)
                                    return;
                                if (!image->EnsureLoaded())
                                    return;
                            }
                            const TexturePreview &preview = GetOrCreatePreview(image->uuid, image->texture);
                            if (!preview.isValid || !preview.textureID)
                                return;

                            texture = preview.textureID;
                            size    = Vec2(preview.size);
                            ok      = true;
                        });
        return ok;
    }

    // Draws the texture preview if the asset is an image with a ready preview. True if something was drawn.
    bool AssetBrowser::TryDrawThumbnail(const shared_ptr<AssetBase> &asset, Vec2 box)
    {
        ImTextureID texture = 0;
        Vec2 size{};
        if (!ResolveThumbnail(asset, texture, size))
            return false;

        ImGui::Image(texture, FitInside(size, box, true));
        return true;
    }

    void AssetBrowser::DrawFolderContextMenu(const DirEntry &folder)
    {
        if (!ImGui::BeginPopupContextItem("FolderContext"))
            return;

        if (ImGui::MenuItem(ICON_MD_OPEN_IN_NEW " Open"))
            NavigateTo(folder.path);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MD_DRIVE_FILE_RENAME_OUTLINE " Rename"))
            BeginRenameFolder(folder.path);
        if (ImGui::MenuItem(ICON_MD_DELETE " Delete"))
            RequestDelete(folder.path, /*isScene=*/false);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MD_CONTENT_COPY " Copy Path"))
            ImGui::SetClipboardText(folder.path.string().c_str());

        ImGui::EndPopup();
    }

    void AssetBrowser::DrawSceneContextMenu(const DirEntry &scene)
    {
        if (!ImGui::BeginPopupContextItem("SceneFileContext"))
            return;

        if (ImGui::MenuItem(ICON_MD_OPEN_IN_NEW " Open"))
            OpenSceneFile(scene.path);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MD_DRIVE_FILE_RENAME_OUTLINE " Rename"))
            BeginRenameScene(scene.path);
        if (ImGui::MenuItem(ICON_MD_DELETE " Delete"))
            RequestDelete(scene.path, /*isScene=*/true);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MD_CONTENT_COPY " Copy Path"))
            ImGui::SetClipboardText(scene.path.string().c_str());

        ImGui::EndPopup();
    }

    void AssetBrowser::DrawAssetContextMenu(const shared_ptr<AssetBase> &asset)
    {
        if (!ImGui::BeginPopupContextItem("AssetContext"))
            return;

        if (ImGui::MenuItem(ICON_MD_OPEN_IN_NEW " Open"))
            m_selectedAsset = asset->uuid;
        // Procedural assets have no file, so there is nothing to rename on disk.
        if (ImGui::MenuItem(ICON_MD_DRIVE_FILE_RENAME_OUTLINE " Rename", nullptr, false, !asset->assetPath.empty()))
            BeginRenameAsset(asset);
        if (ImGui::MenuItem(ICON_MD_DELETE " Delete"))
            RequestDeleteAsset(asset);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MD_SAVE " Save"))
            asset->Save();
        if (ImGui::MenuItem(ICON_MD_CONTENT_COPY " Copy UUID"))
            ImGui::SetClipboardText(asset->uuid.ToString().c_str());
        if (ImGui::MenuItem(ICON_MD_CONTENT_COPY " Copy Path", nullptr, false, !asset->assetPath.empty()))
            ImGui::SetClipboardText(asset->assetPath.string().c_str());

        ImGui::EndPopup();
    }

    void AssetBrowser::DrawListView(const AssetList &assets)
    {
        constexpr ImGuiTableFlags flags =
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
        if (!ImGui::BeginTable("AssetList", 4, flags))
            return;

        ImGui::TableSetupColumn(ICON_MD_IMAGE " Preview", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn(ICON_MD_TITLE " Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(ICON_MD_CATEGORY " Type", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn(ICON_MD_SETTINGS " Actions", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableHeadersRow();

        for (const DirEntry &folder: m_folders)
            DrawFolderRow(folder);

        if (IsCreatingSceneHere())
            DrawNewSceneRow();

        for (const DirEntry &scene: m_sceneFiles)
            DrawSceneRow(scene);

        int index = 0;
        for (const auto &asset: assets)
        {
            if (!asset || !ShouldShowAsset(asset))
                continue;
            DrawAssetRow(asset, index++);
        }

        ImGui::EndTable();
    }

    void AssetBrowser::DrawFolderRow(const DirEntry &folder)
    {
        const ScopedId id("folder", folder.name);
        const bool editing = IsEditing(InlineEditMode::RenameFolder, folder.path);

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(ICON_MD_FOLDER);

        ImGui::TableSetColumnIndex(1);
        if (editing)
        {
            DrawInlineEditField(-1); // -1 = fill remaining column width
        } else
        {
            const bool clicked =
                    ImGui::Selectable(folder.name.c_str(), !m_selectedAsset && m_selectedPath == folder.path,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
            if (clicked || ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                m_selectedAsset.reset();
                m_selectedPath = folder.path;
            }
            if (clicked && ImGui::IsMouseDoubleClicked(0))
                NavigateTo(folder.path);

            DrawFolderContextMenu(folder);
            if (ImGui::BeginDragDropSource())
            {
                const string payload = PathToPayload(folder.path);
                ImGui::SetDragDropPayload(kFolderPayload, payload.data(), payload.size());
                ImGui::TextUnformatted(folder.name.c_str());
                ImGui::EndDragDropSource();
            }
            AcceptBrowserDrop(folder.path);
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("Folder");

        ImGui::TableSetColumnIndex(3);
        if (!editing)
        {
            if (ImGui::SmallButton(ICON_MD_DRIVE_FILE_RENAME_OUTLINE "##rename"))
                BeginRenameFolder(folder.path);
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MD_DELETE "##delete"))
                RequestDelete(folder.path, /*isScene=*/false);
        }
    }

    void AssetBrowser::DrawNewSceneRow()
    {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        DrawIcon(m_icons.scene, Vec2(24, 24));

        ImGui::TableSetColumnIndex(1);
        DrawInlineEditField(-1);

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("Scene");
    }

    // Scene files used to be missing from the list view entirely.
    void AssetBrowser::DrawSceneRow(const DirEntry &scene)
    {
        const ScopedId id("scene", scene.name);
        const bool editing = IsEditing(InlineEditMode::RenameScene, scene.path);

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        DrawIcon(m_icons.scene, Vec2(24, 24));

        ImGui::TableSetColumnIndex(1);
        if (editing)
        {
            DrawInlineEditField(-1);
        } else
        {
            const bool current = IsFileCurrentCurrentScene(scene.path);
            if (current)
                ImGui::PushStyleColor(ImGuiCol_Text, kCurrentSceneColor);
            const bool clicked =
                    ImGui::Selectable(scene.name.c_str(), !m_selectedAsset && m_selectedPath == scene.path,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
            const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            if (current)
                ImGui::PopStyleColor();

            if (clicked || rightClicked)
            {
                m_selectedAsset.reset();
                m_selectedPath = scene.path;
            }
            if (clicked && ImGui::IsMouseDoubleClicked(0))
                OpenSceneFile(scene.path);

            DrawSceneContextMenu(scene);
            if (ImGui::BeginDragDropSource())
            {
                const string payload = PathToPayload(scene.path);
                ImGui::SetDragDropPayload(kScenePayload, payload.data(), payload.size());
                ImGui::TextUnformatted(scene.name.c_str());
                ImGui::EndDragDropSource();
            }
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("Scene");

        ImGui::TableSetColumnIndex(3);
        if (!editing)
        {
            if (ImGui::SmallButton(ICON_MD_DRIVE_FILE_RENAME_OUTLINE "##rename"))
                BeginRenameScene(scene.path);
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MD_DELETE "##delete"))
                RequestDelete(scene.path, /*isScene=*/true);
        }
    }

    void AssetBrowser::DrawAssetRow(const shared_ptr<AssetBase> &asset, int index)
    {
        ImGui::PushID(index);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        DrawSmallPreview(asset);

        ImGui::TableSetColumnIndex(1);
        const bool isSelected = m_selectedAsset && *m_selectedAsset == asset->uuid;
        if (!asset->assetPath.empty() && IsEditing(InlineEditMode::RenameAsset, asset->assetPath))
        {
            DrawInlineEditField(-1);
        } else
        {
            if (ImGui::Selectable(asset->name.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns))
                m_selectedAsset = asset->uuid;
            DrawAssetContextMenu(asset);
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(kAssetPayload, &asset->uuid, sizeof(UUID));
                ImGui::TextUnformatted(asset->name.c_str());
                ImGui::EndDragDropSource();
            }
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(GetAssetTypeName(asset->type));

        ImGui::TableSetColumnIndex(3);
        if (ImGui::SmallButton(ICON_MD_OPEN_IN_NEW "##open"))
            m_selectedAsset = asset->uuid;
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MD_SAVE "##save"))
            asset->Save();
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MD_DELETE "##delete"))
            RequestDeleteAsset(asset);

        ImGui::PopID();
    }

    void AssetBrowser::DrawSmallPreview(const shared_ptr<AssetBase> &asset)
    {
        constexpr Vec2 size(kListPreviewSize, kListPreviewSize);
        if (asset->type == AssetType::Audio && m_icons.audio)
        {
            ImGui::Image(m_icons.audio->GetTexID(), size);
            return;
        }
        if (!TryDrawThumbnail(asset, size))
            DrawPlaceholder(asset, "##preview", size, Vec4(0.2f, 0.2f, 0.2f, 0.5f));
    }

    void AssetBrowser::DrawDetailsPanel()
    {
        ImGui::BeginChild("DetailsPanel", Vec2(0, 0), true);

        const auto asset = AssetController::Get()->FindByUUID(*m_selectedAsset);
        if (!asset)
            ImGui::TextColored(kErrorColor, "%s", ICON_MD_ERROR " Asset not found");
        else if (!VisitImageAsset(asset, [this](const auto &image) { DrawImageDetails(image); }))
            DrawGenericAssetDetails(asset);

        ImGui::EndChild();
    }

    template<typename TImage>
    void AssetBrowser::DrawImageDetails(const shared_ptr<ImageAsset<TImage>> &asset)
    {
        ImGui::BeginGroup();

        Heading(ICON_MD_IMAGE " Image Asset Details");
        ImGui::Separator();

        const auto &tex = asset->texture;

        if (tex)
        {
            if (const TexturePreview &preview = GetOrCreatePreview(asset->uuid, tex);
                preview.isValid && preview.textureID)
            {
                ImGui::TextUnformatted(ICON_MD_VISIBILITY " Preview:");

                const Vec2 box(ImGui::GetContentRegionAvail().x - 20, kDetailsPreviewMaxHeight);
                ImGui::Image(preview.textureID, FitInside(Vec2(preview.size), box, /*allowUpscale=*/false));
                ImGui::Separator();
            }
        }

        Heading(ICON_MD_INFO " Properties");
        if (BeginPropertyTable("ImageProperties"))
        {
            PropertyRow(ICON_MD_TITLE " Name", asset->name);
            PropertyRow(ICON_MD_CATEGORY " Type", GetAssetTypeName(asset->type));

            if (tex)
            {
                const auto extent = tex->GetExtent();

                PropertyRow(ICON_MD_CROP_ORIGINAL " Dimensions",
                            to_string(extent.x) + "x" + to_string(extent.y) + "x" + to_string(extent.z));
                PropertyRow(ICON_MD_FORMAT_COLOR_FILL " Format", GetFormatName(tex->GetFormat()));
                PropertyRow(ICON_MD_LAYERS " Mip Levels", to_string(tex->GetMipLevels()));
                PropertyRow(ICON_MD_VIEW_COLUMN " Array Layers", to_string(tex->GetArrayLevels()));
                PropertyRow(ICON_MD_FILTER " Filter", GetFilterName(tex->GetFilter()));
                PropertyRow(ICON_MD_BORDER_ALL " Address Mode", GetAddressModeName(tex->GetAddressMode()));
                PropertyRow(ICON_MD_LABEL " Samples", to_string(tex->GetSamples()));
                PropertyRow(ICON_MD_SETTINGS " Usage", ToHex(static_cast<uint32_t>(tex->GetUsage())));
            }

            ImGui::EndTable();
        }

        ImGui::Separator();
        DrawActionButtons(asset);

        ImGui::EndGroup();
    }

    void AssetBrowser::DrawGenericAssetDetails(const shared_ptr<AssetBase> &asset)
    {
        ImGui::BeginGroup();

        Heading(ICON_MD_INFO " Asset Details");
        ImGui::Separator();

        if (BeginPropertyTable("AssetProperties"))
        {
            PropertyRow(ICON_MD_TITLE " Name", asset->name);
            PropertyRow(ICON_MD_CATEGORY " Type", GetAssetTypeName(asset->type));
            PropertyRow(ICON_MD_TAG " UUID", asset->uuid.ToString());
            if (!asset->assetPath.empty())
                PropertyRow(ICON_MD_INSERT_DRIVE_FILE " Source", asset->assetPath.filename().string());

            if (const auto model = ::SF::RTTI::rtti_pointer_cast<MeshAsset>(asset))
            {
                if (model->IsLoaded())
                {
                    uint32_t verts = 0, tris = 0;
                    for (const auto &p: model->parts)
                    {
                        verts += p.vertexCount;
                        tris += p.triangleCount;
                    }
                    const Vec3 size = model->boundsMax - model->boundsMin;
                    char dims[96];
                    snprintf(dims, sizeof(dims), "%.2f x %.2f x %.2f", size.x, size.y, size.z);
                    PropertyRow(ICON_MD_VIEW_IN_AR " Parts", to_string(model->parts.size()));
                    PropertyRow(ICON_MD_SCATTER_PLOT " Vertices", to_string(verts));
                    PropertyRow(ICON_MD_CHANGE_HISTORY " Triangles", to_string(tris));
                    PropertyRow(ICON_MD_STRAIGHTEN " Size", dims);
                    if (model->animationCount > 0)
                        PropertyRow(ICON_MD_WARNING " Animations", to_string(model->animationCount) + " (ignored)");
                }
            } else if (const auto audio = ::SF::RTTI::rtti_pointer_cast<AudioAsset>(asset))
            {
                if (audio->buffer)
                {
                    char dur[32];
                    snprintf(dur, sizeof(dur), "%.2f s", audio->durationSeconds);
                    PropertyRow(ICON_MD_TIMER " Duration", dur);
                    PropertyRow(ICON_MD_SPEAKER " Channels", to_string(audio->channels));
                    PropertyRow(ICON_MD_GRAPHIC_EQ " Sample Rate", to_string(audio->sampleRate) + " Hz");
                }
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        DrawActionButtons(asset);

        ImGui::EndGroup();
    }

    template<typename T>
    void AssetBrowser::DrawActionButtons(const shared_ptr<T> &asset)
    {
        Heading(ICON_MD_SETTINGS " Actions");

        if (ImGui::Button(ICON_MD_SAVE " Save", Vec2(100, 0)))
            asset->Save();
        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_DELETE " Delete", Vec2(100, 0)))
            RequestDeleteAsset(asset);
    }

    // TODO: AssetWizard class
    template<typename TImage>
    void AssetBrowser::CreateImageAsset()
    {
        static_assert(is_base_of_v<Image, TImage>, "TImage must derive from Image");

        auto asset = AssetController::Get()->RegisterAsset<ImageAsset<TImage>>(
                "New" + string(GetAssetTypeName(AssetType::Texture)));

        // Only 3D images take a 3D extent. Cubemap uses 2D dimensions (square face size).
        if constexpr (is_same_v<TImage, Image3d>)
            asset->Create(UVec3(kDefaultImageSize, kDefaultImageSize, kDefaultImageSize), kDefaultImageFormat);
        else
            asset->Create(UVec2(kDefaultImageSize, kDefaultImageSize), kDefaultImageFormat);

        asset->Save();
        m_selectedAsset = asset->uuid;
    }

    // Entries (including invalid ones) are cached until RefreshAssets() clears the cache, so a lookup is
    // one hash find per call instead of a map write every frame.
    // NOTE: once real GPU upload exists and previews can become valid later, re-try invalid entries here.
    template<typename TImage>
    const AssetBrowser::TexturePreview &AssetBrowser::GetOrCreatePreview(const UUID &guid,
                                                                         const shared_ptr<TImage> &texture)
    {
        if (const auto it = m_previewCache.find(guid); it != m_previewCache.end())
            return it->second;

        TexturePreview preview;
        preview.isValid = false;
        preview.size    = texture->GetExtent();

        // Image2d etc. create their ImGui descriptor when they upload (GetTexID()); 0 means "none yet".
        preview.textureID = texture->GetTexID();
        preview.isValid   = preview.textureID != 0;

        return m_previewCache.emplace(guid, preview).first->second;
    }

    void AssetBrowser::BeginInlineEdit(const InlineEditMode mode, const filesystem::path &path, const string &text)
    {
        m_inlineEditMode = mode;
        m_inlineEditPath = path;
        snprintf(m_inlineEditBuffer, sizeof(m_inlineEditBuffer), "%s", text.c_str());
        m_inlineEditFocusPending = true;
    }

    void AssetBrowser::EndInlineEdit()
    {
        m_inlineEditMode = InlineEditMode::None;
        m_inlineEditPath.clear();
    }

    bool AssetBrowser::IsEditing(const InlineEditMode mode, const filesystem::path &path) const
    {
        return m_inlineEditMode == mode && m_inlineEditPath == path;
    }

    bool AssetBrowser::IsCreatingSceneHere() const
    {
        return m_inlineEditMode == InlineEditMode::NewScene && m_inlineEditPath == m_currentPath;
    }

    void AssetBrowser::DrawInlineEditField(const float width)
    {
        ImGui::SetNextItemWidth(width);

        if (m_inlineEditFocusPending)
        {
            ImGui::SetKeyboardFocusHere();
            m_inlineEditFocusPending = false;
        }

        const bool confirmed =
                ImGui::InputText("##inlineedit", m_inlineEditBuffer, sizeof(m_inlineEditBuffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

        // Enter confirms; so does clicking away. IsItemDeactivated() is only true for a field that was
        // active, so no "armed" flag is needed. (The old local `m_inlineEditArmed` shadowed nothing and was
        // reset every frame, so click-away never committed.)
        if (confirmed || ImGui::IsItemDeactivated())
            CommitInlineEdit();
    }

    void AssetBrowser::CommitInlineEdit()
    {
        switch (m_inlineEditMode)
        {
            case InlineEditMode::RenameFolder:
                CommitFolderRename();
                break;
            case InlineEditMode::NewScene:
                CommitNewScene();
                break;
            case InlineEditMode::RenameScene:
                CommitSceneRename();
                break;
            case InlineEditMode::RenameAsset:
                CommitAssetRename();
                break;
            default:
                break;
        }
    }

    void AssetBrowser::BeginNewScene()
    {
        const auto dir = m_currentPath.empty() ? ProjectManager::Get()->GetProjectAssetPath() : m_currentPath;
        BeginInlineEdit(InlineEditMode::NewScene, dir, "New Scene");
        ImGui::SetWindowFocus(kWindowTitle); // bring the tab forward if it's docked behind another
    }

    void AssetBrowser::CommitNewScene()
    {

        if (const string name = CleanFileName(m_inlineEditBuffer); !name.empty()) // empty name = treat as cancel
        {
            if (CreateDefaultScene(m_inlineEditPath, name).empty())
                Log::Error("Failed to create scene in {}", m_inlineEditPath.string());
            else
                RefreshAssets();
        }

        EndInlineEdit();
    }

    void AssetBrowser::BeginRenameFolder(const filesystem::path &path)
    {
        BeginInlineEdit(InlineEditMode::RenameFolder, path, path.filename().string());
    }

    void AssetBrowser::CommitFolderRename()
    {
        // CleanFileName also strips path separators, so "a/b" can no longer move the folder somewhere else.

        if (const string newName = CleanFileName(m_inlineEditBuffer);
            !newName.empty() && newName != m_inlineEditPath.filename().string())
        {
            if (const filesystem::path newPath = m_inlineEditPath.parent_path() / newName;
                !File::Exists(newPath.string()) && File::Rename(m_inlineEditPath.string(), newPath.string()))
            {
                if (m_currentPath == m_inlineEditPath)
                    NavigateTo(newPath);
                m_dirDirty = true;
            }
        }

        EndInlineEdit();
    }

    void AssetBrowser::BeginRenameScene(const filesystem::path &file)
    {
        BeginInlineEdit(InlineEditMode::RenameScene, file, SceneDisplayName(file));
    }

    void AssetBrowser::CommitSceneRename()
    {
        namespace fs = filesystem;

        if (const string name = CleanFileName(m_inlineEditBuffer); !name.empty())
        {
            const fs::path dst = m_inlineEditPath.parent_path() / (name + string(SceneFileExtension));
            if (error_code ec; dst != m_inlineEditPath && !fs::exists(dst, ec))
            {
                fs::rename(m_inlineEditPath, dst, ec);
                if (ec)
                {
                    Log::Error("Failed to rename scene: {}", ec.message());
                } else
                {
                    NotifyMovedScene(m_inlineEditPath, dst);
                    m_dirDirty = true;
                }
            }
        }

        EndInlineEdit();
    }

    namespace
    {
        // Dropped folders are expanded recursively (flattened into the destination folder).
        // Files the importer doesn't understand are only counted.
        vector<filesystem::path> CollectImportable(const vector<filesystem::path> &dropped, size_t &unsupported)
        {
            namespace fs = filesystem;
            vector<fs::path> out;
            error_code ec;

            const auto consider = [&](const fs::path &p)
            {
                if (AssetImporter::IsSupported(p))
                    out.push_back(p);
                else
                    ++unsupported;
            };

            for (const fs::path &p: dropped)
            {
                if (fs::is_directory(p, ec))
                {
                    for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end;
                         !ec && it != end; it.increment(ec))
                    {
                        if (error_code fileEc; it->is_regular_file(fileEc))
                            consider(it->path());
                    }
                } else
                    consider(p);
            }
            return out;
        }

        // Is the mouse over any ImGui window? (false = it is over the bare 3D view.)
        bool IsOverAnyImGuiWindow()
        {
            const ImGuiContext *ctx = ImGui::GetCurrentContext();
            return ctx && ctx->HoveredWindow != nullptr;
        }
    } // namespace

    // The Window's GLFW drop callback fires in the middle of glfwPollEvents(), outside any ImGui frame, so we
    // only queue the paths there. They are handled on the next Draw(), when ImGui knows what the mouse is over.
    // (GLFW/Win32 updates the cursor position to the drop point just before the callback.)
    void AssetBrowser::EnsureDropSubscribed()
    {
        if (m_dropSubscribed)
            return;

        auto *windows  = WindowManager::Get();
        Window *window = windows ? windows->GetWindow(0) : nullptr;
        if (!window)
            return;

        m_dropConnection = window->OnDrop().connect(
                [this](const vector<string> &paths)
                {
                    if (!ProjectManager::Get()->IsAProjectLoaded())
                    {
                        Log::Warning("Dropped files ignored: no project is loaded.");
                        return;
                    }
                    for (const string &p: paths)
                    {
                        // GLFW hands out UTF-8.
                        const u8string u8(reinterpret_cast<const char8_t *>(p.data()), p.size());
                        m_pendingDrops.emplace_back(u8);
                    }
                });
        m_dropSubscribed = true;
    }

    void AssetBrowser::ProcessPendingDrops()
    {
        if (m_pendingDrops.empty())
            return;

        const vector<filesystem::path> dropped = std::move(m_pendingDrops);
        m_pendingDrops.clear();

        const Vec2 mouse       = ImGui::GetIO().MousePos;
        const bool overPanel   = IsOverAnyImGuiWindow();
        const bool overBrowser = overPanel && m_windowRectValid && mouse.x >= m_windowMin.x &&
                                 mouse.x <= m_windowMax.x && mouse.y >= m_windowMin.y && mouse.y <= m_windowMax.y;
        const bool overScene = !overPanel;

        if (!overBrowser && !overScene)
        {
            SetStatus("Drop files on the Asset Browser (import) or on the scene view (import + place).", true);
            return;
        }

        size_t unsupported = 0;
        const auto files   = CollectImportable(dropped, unsupported);
        if (files.empty())
        {
            SetStatus("Nothing imported: supported types are models, images and audio.", true);
            return;
        }
        if (unsupported > 0)
            Log::Info("Import: ignored {} unsupported file(s) in the drop.", unsupported);

        ImportFiles(files, /*placeInScene=*/overScene);
    }

    void AssetBrowser::ImportFiles(const vector<filesystem::path> &files, bool placeInScene)
    {
        const filesystem::path dest =
                m_currentPath.empty() ? ProjectManager::Get()->GetProjectAssetPath() : m_currentPath;

        const auto results = AssetImporter::ImportMany(files, dest);

        Scene *scene = SceneManager::Get()->GetScene();
        Vec3 point(0.0f);
        const bool canPlace = placeInScene && scene && GroundPointUnderCursor(point);

        int imported = 0, placed = 0, failed = 0;
        string firstError;

        for (const ImportResult &r: results)
        {
            if (!r.Ok())
            {
                ++failed;
                if (firstError.empty())
                    firstError = r.source.filename().string() + ": " + r.error;
                continue;
            }

            if (!r.alreadyImported)
                ++imported;
            m_selectedAsset = r.asset->uuid;

            if (canPlace)
            {
                if (const Vec3 where = point + Vec3(2.0f * static_cast<float>(placed), 0.0f, 0.0f);
                    scene->InstantiateAsset(r.asset, where))
                    ++placed;
            }
        }

        m_dirDirty = true;

        string text = "Imported " + to_string(imported) + " asset(s)";
        if (canPlace)
            text += ", placed " + to_string(placed);
        if (failed > 0)
            text += "; " + to_string(failed) + " failed (" + firstError + ")";
        SetStatus(std::move(text), failed > 0);
    }

    // An asset tile dragged out of this panel and released over the bare 3D view places the asset there.
    // ImGui keeps the payload alive for a frame or two after release, so we track the drag ourselves and
    // fire exactly once, on the first frame the mouse is up.
    void AssetBrowser::ProcessSceneAssetDrag()
    {
        const ImGuiPayload *payload = ImGui::GetDragDropPayload();
        const bool mine = payload && payload->IsDataType(kAssetPayload) && payload->DataSize == sizeof(UUID);

        if (!mine)
            m_dragConsumed = false;
        if (mine && !m_dragConsumed)
            m_dragAsset = *static_cast<const UUID *>(payload->Data);

        if (!m_dragAsset)
            return;

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (!mine)
                m_dragAsset.reset(); // cancelled (Esc) or the source vanished
            return;
        }

        // Released.
        const UUID id = *m_dragAsset;
        m_dragAsset.reset();
        m_dragConsumed = mine;

        if (IsOverAnyImGuiWindow())
            return; // dropped on a panel, not the scene

        Scene *scene     = SceneManager::Get()->GetScene();
        const auto asset = AssetController::Get()->FindByUUID(id);
        Vec3 point(0.0f);
        if (!scene || !asset || !GroundPointUnderCursor(point))
            return;

        if (scene->InstantiateAsset(asset, point))
            SetStatus("Placed '" + asset->name + "'", false);
        else
            SetStatus("Could not place '" + asset->name + "' (see log)", true);
    }

    // Ray through the mouse position (ImGui screen coordinates == window logical pixels) against y = kGroundY.
    // Falls back to a point 10 units in front of the cursor when the ray doesn't hit the plane.
    bool AssetBrowser::GroundPointUnderCursor(Vec3 &out) const
    {
        const Camera *cam = CameraController::Get().GetActive();
        if (!cam)
            return false;

        const ImGuiIO &io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
            return false;

        const float ndcX = 2.0f * io.MousePos.x / io.DisplaySize.x - 1.0f;
        const float ndcY =
                2.0f * io.MousePos.y / io.DisplaySize.y - 1.0f; // the projection already carries the Vulkan Y flip
        // Near plane is depth 1 only for the infinite-far inverse-Z projection; depth 0 otherwise.
        const float ndcZ = (cam->IsFarPlaneInfinite() && cam->IsInverseZ()) ? 1.0f : 0.0f;

        const Mat4 invViewProj = glm::inverse(cam->GetProjection(io.DisplaySize.x / io.DisplaySize.y) * cam->GetView());
        const Vec4 h           = invViewProj * Vec4(ndcX, ndcY, ndcZ, 1.0f);
        if (abs(h.w) < 1e-8f)
            return false;

        const Vec3 origin = cam->GetPosition();
        const Vec3 dir    = glm::normalize(Vec3(h) / h.w - origin);

        if (dir.y < -1e-4f)
        {
            const float t = (kGroundY - origin.y) / dir.y;
            if (t > 0.0f && t <= kMaxPlaceDist)
            {
                out = origin + dir * t;
                return true;
            }
        }

        out = origin + dir * 10.0f;
        return true;
    }

    // ======================================================================
    //  Rename asset / move by drag-and-drop
    // ======================================================================

    void AssetBrowser::BeginRenameAsset(const shared_ptr<AssetBase> &asset)
    {
        if (asset && !asset->assetPath.empty())
            BeginInlineEdit(InlineEditMode::RenameAsset, asset->assetPath, asset->assetPath.stem().string());
    }

    void AssetBrowser::CommitAssetRename()
    {
        namespace fs      = filesystem;
        const string name = CleanFileName(m_inlineEditBuffer);
        const auto asset  = AssetController::Get()->FindByPath(m_inlineEditPath);

        if (asset && !name.empty() && name != asset->assetPath.stem().string())
        {
            const fs::path src = asset->assetPath;
            const fs::path dst = src.parent_path() / (name + src.extension().string());
            fs::path srcMeta   = src;
            srcMeta += ".meta";
            fs::path dstMeta = dst;
            dstMeta += ".meta";

            if (error_code ec; fs::exists(dst, ec) || fs::exists(dstMeta, ec))
            {
                SetStatus("'" + dst.filename().string() + "' already exists", true);
            } else
            {
                fs::rename(src, dst, ec);
                if (!ec)
                {
                    fs::rename(srcMeta, dstMeta, ec);
                    if (ec)
                    {
                        error_code undo;
                        fs::rename(dst, src, undo); // keep file and .meta together
                    }
                }

                if (ec)
                {
                    SetStatus("Could not rename: " + ec.message(), true);
                } else
                {
                    asset->assetPath = dst;
                    asset->name      = name;
                    asset->SaveMeta(); // the .meta stores the display name
                    m_dirDirty = true;
                }
            }
        }

        EndInlineEdit();
    }

    // Call right after an item that should accept drops into `destFolder` (folder tiles/rows, the Up button).
    void AssetBrowser::AcceptBrowserDrop(const filesystem::path &destFolder)
    {
        if (!ImGui::BeginDragDropTarget())
            return;

        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(kAssetPayload))
        {
            if (p->DataSize == sizeof(UUID))
                MoveAssetTo(*static_cast<const UUID *>(p->Data), destFolder);
        } else if (const ImGuiPayload *f = ImGui::AcceptDragDropPayload(kFolderPayload))
        {
            MoveFolderTo(PathFromPayload(f), destFolder);
        } else if (const ImGuiPayload *s = ImGui::AcceptDragDropPayload(kScenePayload))
        {
            MoveSceneTo(PathFromPayload(s), destFolder);
        }

        ImGui::EndDragDropTarget();
    }

    bool AssetBrowser::MoveAssetTo(const UUID &id, const filesystem::path &folder)
    {
        namespace fs           = filesystem;
        const auto *controller = AssetController::Get();
        const auto asset       = controller ? controller->FindByUUID(id) : nullptr;
        if (!asset || asset->assetPath.empty())
            return false; // procedural assets have no file to move

        const fs::path src = asset->assetPath;
        if (src.parent_path() == folder)
            return false;

        const fs::path dst = folder / src.filename();
        fs::path srcMeta   = src;
        srcMeta += ".meta";
        fs::path dstMeta = dst;
        dstMeta += ".meta";

        error_code ec;
        if (fs::exists(dst, ec) || fs::exists(dstMeta, ec))
        {
            SetStatus("'" + dst.filename().string() + "' already exists in that folder", true);
            return false;
        }

        fs::rename(src, dst, ec);
        if (!ec)
        {
            fs::rename(srcMeta, dstMeta, ec);
            if (ec)
            {
                error_code undo;
                fs::rename(dst, src, undo);
            }
        }
        if (ec)
        {
            SetStatus("Could not move '" + asset->name + "': " + ec.message(), true);
            return false;
        }

        asset->assetPath = dst; // scenes reference the asset by UUID, so nothing else needs to change
        m_dirDirty       = true;
        SetStatus("Moved '" + asset->name + "' to " + folder.filename().string(), false);
        return true;
    }

    bool AssetBrowser::MoveSceneTo(const filesystem::path &file, const filesystem::path &folder)
    {
        namespace fs = filesystem;
        if (file.parent_path() == folder)
            return false;

        const fs::path dst = folder / file.filename();
        error_code ec;
        if (fs::exists(dst, ec))
        {
            SetStatus("'" + dst.filename().string() + "' already exists in that folder", true);
            return false;
        }

        fs::rename(file, dst, ec);
        if (ec)
        {
            SetStatus("Could not move scene: " + ec.message(), true);
            return false;
        }

        NotifyMovedScene(file, dst);
        m_dirDirty = true;
        SetStatus("Moved scene to " + folder.filename().string(), false);
        return true;
    }

    bool AssetBrowser::MoveFolderTo(const filesystem::path &folder, const filesystem::path &destFolder)
    {
        namespace fs = filesystem;
        if (folder.parent_path() == destFolder)
            return false;
        if (IsSameOrInside(destFolder, folder))
        {
            SetStatus("A folder can't be moved into itself", true);
            return false;
        }

        const fs::path dst = destFolder / folder.filename();
        error_code ec;
        if (fs::exists(dst, ec))
        {
            SetStatus("'" + dst.filename().string() + "' already exists in that folder", true);
            return false;
        }

        // Scenes below the folder, so the scene system can be told where they went.
        vector<pair<fs::path, fs::path>> movedScenes;
        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
        {
            if (error_code fileEc; it->is_regular_file(fileEc) && IsSceneFile(it->path()))
                movedScenes.emplace_back(it->path(), dst / it->path().lexically_relative(folder));
        }

        ec.clear();
        fs::rename(folder, dst, ec);
        if (ec)
        {
            SetStatus("Could not move folder: " + ec.message(), true);
            return false;
        }

        if (const auto *controller = AssetController::Get())
        {
            for (const auto &asset: controller->assets_)
                if (asset && !asset->assetPath.empty() && IsSameOrInside(asset->assetPath, folder))
                    asset->assetPath = dst / asset->assetPath.lexically_relative(folder);
        }
        for (const auto &[from, to]: movedScenes)
            NotifyMovedScene(from, to);

        if (IsSameOrInside(m_currentPath, folder))
            NavigateTo(dst / m_currentPath.lexically_relative(folder));

        m_dirDirty = true;
        SetStatus("Moved folder '" + folder.filename().string() + "' to " + destFolder.filename().string(), false);
        return true;
    }

    void AssetBrowser::SetStatus(string text, bool isError)
    {
        m_status        = std::move(text);
        m_statusIsError = isError;
        m_statusUntil   = ImGui::GetTime() + (isError ? 12.0 : 6.0);
    }

    void AssetBrowser::DrawStatus()
    {
        if (m_status.empty())
            return;
        if (ImGui::GetTime() > m_statusUntil)
        {
            m_status.clear();
            return;
        }
        ImGui::TextColored(m_statusIsError ? kErrorColor : kHeadingColor, "%s", m_status.c_str());
    }

    void AssetBrowser::RequestDelete(const filesystem::path &path, bool isScene)
    {
        m_deleteTargetAsset.reset();
        m_deleteTargetPath       = path;
        m_deleteTargetIsScene    = isScene; // previously never reset when deleting a folder after a scene
        m_showDeleteConfirmPopup = true;
    }

    void AssetBrowser::RequestDeleteAsset(const shared_ptr<AssetBase> &asset)
    {
        if (!asset)
            return;
        m_deleteTargetAsset      = asset->uuid;
        m_deleteTargetIsScene    = false;
        m_deleteTargetPath       = asset->assetPath.empty() ? filesystem::path(asset->name) : asset->assetPath;
        m_showDeleteConfirmPopup = true;
    }

    void AssetBrowser::DrawDeleteConfirmPopup()
    {
        if (m_showDeleteConfirmPopup)
        {
            ImGui::OpenPopup("Confirm Delete");
            m_showDeleteConfirmPopup = false;
        }

        ImGui::SetNextWindowSize(Vec2(360, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::TextColored(kWarningColor, "%s",
                           m_deleteTargetAsset     ? ICON_MD_WARNING " Delete this asset and its files?"
                           : m_deleteTargetIsScene ? ICON_MD_WARNING " Delete this scene file?"
                                                   : ICON_MD_WARNING " Delete this folder and all its contents?");
        ImGui::TextWrapped("%s", m_deleteTargetPath.string().c_str());
        ImGui::Separator();

        if (ImGui::Button(ICON_MD_DELETE " Delete", Vec2(120, 0)))
        {
            ExecuteDelete();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_CLOSE " Cancel", Vec2(120, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void AssetBrowser::ExecuteDelete()
    {
        auto *controller = AssetController::Get();

        if (m_deleteTargetAsset)
        {
            const UUID id = *m_deleteTargetAsset;
            m_deleteTargetAsset.reset();

            const auto asset = controller ? controller->FindByUUID(id) : nullptr;
            if (asset)
            {
                bool filesGone = true; // procedural assets have no files
                if (!asset->assetPath.empty())
                {
                    error_code ec, metaEc;
                    filesystem::remove(asset->assetPath, ec);
                    filesystem::path meta = asset->assetPath;
                    meta += ".meta";
                    filesystem::remove(meta, metaEc);

                    if (ec || metaEc)
                    {
                        // Keep the asset listed; dropping it now would only make it reappear on the next scan.
                        filesGone = false;
                        Log::Error("Failed to delete asset '{}': {}", asset->name, (ec ? ec : metaEc).message());
                        SetStatus("Could not delete '" + asset->name + "': " + (ec ? ec : metaEc).message(), true);
                    }
                }

                if (filesGone)
                {
                    controller->RemoveAsset(id);
                    m_previewCache.erase(id);
                    if (m_selectedAsset && *m_selectedAsset == id)
                        m_selectedAsset.reset();
                    SetStatus("Deleted '" + asset->name + "'", false);
                }
            }
            m_dirDirty = true;
            return;
        }

        if (m_deleteTargetIsScene)
        {
            error_code ec;
            filesystem::remove(m_deleteTargetPath, ec);
            if (ec)
                Log::Error("Failed to delete {}: {}", m_deleteTargetPath.string(), ec.message());
            else
                NotifyDeletedScene(m_deleteTargetPath);
        } else
        {
            // Component-wise check; the old string-prefix test also matched "/a/foobar" for "/a/foo".
            const bool viewingDeletedFolder = IsSameOrInside(m_currentPath, m_deleteTargetPath);

            File::DeleteDirectory(m_deleteTargetPath.string(), true);

            // Everything imported below the folder is gone from disk; drop it from the live list too.
            if (controller)
            {
                controller->RemoveAssetsUnder(m_deleteTargetPath);
                if (m_selectedAsset && !controller->FindByUUID(*m_selectedAsset))
                    m_selectedAsset.reset();
                erase_if(m_previewCache, [&](const auto &entry) { return !controller->FindByUUID(entry.first); });
            }

            if (viewingDeletedFolder && m_deleteTargetPath.has_parent_path())
                NavigateTo(m_deleteTargetPath.parent_path());
        }

        m_dirDirty = true;
    }
} // namespace SF::Engine
