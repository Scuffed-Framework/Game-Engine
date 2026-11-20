#pragma once
#include <Engine/Module.hpp>
#include <LowLevel/XML/XMLModule.hpp>

#include <cstdint>

#include "Skeleton/AnimSkeleton.hpp"
#include "Skin/AnimSkin.hpp"

namespace SF::Engine
{
    class AnimationModule : public ModuleRegistrar<AnimationModule>
    {
        REGISTER_MODULE_EX(AnimationModule, ModuleStage::Normal, ModuleStartupStage::OnEngineInit);

    public:
        void Update() override {};

        // Builds a Skeleton from the JOINT nodes of the first <visual_scene> in a COLLADA document.
        // Non-joint nodes above a root joint (e.g. Blender's Armature node) are folded into that root's bind
        // transform. `correction` is applied on top of the roots (e.g. a Z-up -> Y-up rotation).
        // Joints are created in DFS order, so the parent-first invariant holds. Bind data is already rebuilt.
        [[nodiscard]] static Skeleton LoadSkeleton(const XMLNode &colladaRoot, const Mat4 &correction = Mat4(1.0f));

        // Loads the first <controller>/<skin> of a COLLADA document. Each vertex is padded/limited to exactly
        // maxWeights influences. The returned Skin is already bound to `skeleton` (joint ids = skeleton indices).
        [[nodiscard]] static Skin LoadSkin(const XMLNode &colladaRoot, const Skeleton &skeleton,
                                           std::uint32_t maxWeights = 4);
    };
} // namespace SF::Engine
