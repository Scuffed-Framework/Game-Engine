#pragma once
#include <EntityComponentSystem/Component.hpp>
#include <LowLevel/XML/XMLModule.hpp>
#include <Math/BasicMath.hpp>
#include <Rendering/Mesh/Mesh.hpp>
#include <Rendering/RHI/Images/Image.hpp>
#include <memory>
#include "Color/Color.hpp"

namespace SF::Engine
{
    inline Image2d MakeTextureFromColor(Color col)
    {
        auto bitmap = std::make_unique<Bitmap>(UVec2{1, 1}, 4);

        const uint32_t packed = col.ToInt(Color::PackingOrder::RGBA);
        uint8_t *data         = bitmap->GetData().get();

        // RGBA order, byte-for-byte
        data[0] = static_cast<uint8_t>((packed >> 24) & 0xFF); // R
        data[1] = static_cast<uint8_t>((packed >> 16) & 0xFF); // G
        data[2] = static_cast<uint8_t>((packed >> 8) & 0xFF);  // B
        data[3] = static_cast<uint8_t>(packed & 0xFF);         // A

        return Image2d(std::move(bitmap), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_FILTER_LINEAR,
                       VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLE_COUNT_1_BIT, false, false);
    }

    // TODO: merge or remove
    struct Std140 PBRMaterialPushConstants
    {
        Vec4 baseColor; // rgba
        float metallicFactor;
        float roughnessFactor;
        float aoFactor;
        float emissiveFactor;

        float heightScale;
        float tessellationFactor;
        float tessMinDistance;
        float tessMaxDistance;
    };
    static_assert(std::is_trivially_copyable_v<PBRMaterialPushConstants>);

    struct MeshMaterial : public Component::Registrar<MeshMaterial>, public DragAndDropObjectTag
    {
        // TODO: DND Images in Editor and Reflected Types
        static bool Reflect()
        {
            Reflection::SerializeContext::Instance()
                    .Class<MeshMaterial>()
                    ->Version(1)
                    ->Field("Albedo", &MeshMaterial::albedo)
                    ->Field("Normal Map", &MeshMaterial::normal)
                    ->Field("PBR", &MeshMaterial::pbr)
                    ->Field("Emission Mask", &MeshMaterial::emissive)

                    ->Field("Base Color", &MeshMaterial::baseColor)
                    ->Field("Roughness Factor", &MeshMaterial::roughnessFactor)
                    ->Field("Ambient Occlusion Factor", &MeshMaterial::aoFactor)
                    ->Field("Emissive Factor", &MeshMaterial::emissiveFactor);
            return true;
        }
        inline static const bool Registered = (Register("Material"), Reflect());

        std::shared_ptr<Image2d> albedo;   // bind=4  (white if null)
        std::shared_ptr<Image2d> normal;   // bind=5  (flat if null)
        std::shared_ptr<Image2d> pbr;      // bind=6  (r=rough g=metal b=ao, white if null)
        std::shared_ptr<Image2d> emissive; // bind=7  (black if null)
        // TODO:
        // std::shared_ptr<Image2d> roughness;
        // std::shared_ptr<Image2d> ao;
        // std::shared_ptr<Image2d> displacement;

        Color baseColor       = {1, 1, 1, 1};
        float roughnessFactor = 1.0f;
        float metallicFactor  = 0.0f;
        float aoFactor        = 1.0f;
        float emissiveFactor  = 0.0f;
        // TODO:
        // float displacementFactor = 0.05f;
        // float tessellationFactor = 16.0f;
        // float tessMinDistance    = 5.0f;
        // float tessMaxDistance    = 50.0f;

        void Reset() override
        {
            baseColor       = {1, 1, 1, 1};
            roughnessFactor = 1.0f;
            metallicFactor  = 0.0f;
            aoFactor        = 1.0f;
            emissiveFactor  = 0.0f;
        }

        [[nodiscard]] std::unique_ptr<Component> Clone() const override
        {
            return std::make_unique<MeshMaterial>(*this);
        }

        void Serialize(XMLNode &node) const override
        {
            // Only serialize if the texture exists
            if (albedo)
            {
                XMLNode n = node.AddChild("albedo");
                albedo->Serialize(n);
            }
            if (normal)
            {
                XMLNode n = node.AddChild("normal");
                normal->Serialize(n);
            }
            if (pbr)
            {
                XMLNode n = node.AddChild("pbr");
                pbr->Serialize(n);
            }
            if (emissive)
            {
                XMLNode n = node.AddChild("emissive");
                emissive->Serialize(n);
            }

            node.SetAttribute("roughnessFactor", roughnessFactor);
            node.SetAttribute("metallicFactor", metallicFactor);
            node.SetAttribute("aoFactor", aoFactor);
            node.SetAttribute("emissiveFactor", emissiveFactor);

            XMLNode colorNode = node.AddChild("baseColor");
            colorNode.SetAttribute("r", baseColor.r);
            colorNode.SetAttribute("g", baseColor.g);
            colorNode.SetAttribute("b", baseColor.b);
            colorNode.SetAttribute("a", baseColor.a);
        }

        void Deserialize(const XMLNode &node) override
        {
            // Only deserialize if the child node exists
            if (XMLNode n = node.GetChild("albedo"); n.IsValid())
            {
                albedo = std::make_shared<Image2d>("", VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, true, true,
                                                   false);
                albedo->Deserialize(n);
            }
            if (XMLNode n = node.GetChild("normal"); n.IsValid())
            {
                normal = std::make_shared<Image2d>("", VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, true, true,
                                                   false);
                normal->Deserialize(n);
            }
            if (XMLNode n = node.GetChild("pbr"); n.IsValid())
            {
                pbr = std::make_shared<Image2d>("", VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, true, true,
                                                false);
                pbr->Deserialize(n);
            }
            if (XMLNode n = node.GetChild("emissive"); n.IsValid())
            {
                emissive = std::make_shared<Image2d>("", VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, true, true,
                                                     false);
                emissive->Deserialize(n);
            }

            node.GetAttribute("roughnessFactor", roughnessFactor);
            node.GetAttribute("metallicFactor", metallicFactor);
            node.GetAttribute("aoFactor", aoFactor);
            node.GetAttribute("emissiveFactor", emissiveFactor);

            XMLNode colorNode = node.GetChild("baseColor");
            if (colorNode.IsValid())
            {
                colorNode.GetAttribute("r", baseColor.r);
                colorNode.GetAttribute("g", baseColor.g);
                colorNode.GetAttribute("b", baseColor.b);
                colorNode.GetAttribute("a", baseColor.a);
            }
        }
    };
} // namespace SF::Engine
