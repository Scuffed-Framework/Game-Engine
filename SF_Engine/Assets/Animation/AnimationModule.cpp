#include "AnimationModule.hpp"

#include <Engine/Log/Log.hpp>
#include <UtilityClasses/StlExtensions.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace SF::Engine
{
    namespace
    {
        // Skin joint names are the node's sid; fall back to id for exporters that omit it.
        std::string JointName(const XMLNode &node)
        {
            auto name = node.GetAttribute("sid");

            if (name.empty())
                name = node.GetAttribute("id");

            return name;
        }

        // `carry` is the accumulated transform of non-joint ancestors, consumed by the first joint below them.
        void ParseNode(const XMLNode &node, Skeleton &skeleton, const std::uint32_t parent, const Mat4 &carry)
        {
            Mat4 local;

            for (const auto &matrix: node.GetChildren("matrix"))
            {
                const auto values = ParseNumericArrayFromStr<float>(matrix.GetContent());

                if (values.size() != 16)
                {
                    Log::Error("COLLADA <matrix> does not have 16 values.");
                }

                Mat4 m(1.0f);

                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r)
                        m[c][r] = values[r * 4 + c];

                local = m;
            }

            if (node.GetAttribute("type") == "JOINT")
            {
                const auto index = skeleton.AddJoint(JointName(node), parent, Transform::FromMatrix(carry * local));

                for (const auto &child: node.GetChildren("node"))
                    ParseNode(child, skeleton, index, Mat4(1.0f));
            } else
            {
                for (const auto &child: node.GetChildren("node"))
                    ParseNode(child, skeleton, parent, carry * local);
            }
        }

        std::optional<XMLNode> FindSource(const XMLNode &skin, const std::string &id)
        {
            for (const auto &source: skin.GetChildren("source"))
            {
                if (source.GetAttribute("id") == id)
                    return source;
            }

            return std::nullopt;
        }

        // Returns the (hash-stripped) source id of the <input> with the given semantic.
        std::string FindInputSource(const XMLNode &parent, const std::string &semantic)
        {
            for (const auto &input: parent.GetChildren("input"))
            {
                if (input.GetAttribute("semantic") == semantic)
                {
                    auto s = input.GetAttribute("source");
                    if (!s.empty() && s.front() == '#')
                        s.erase(0, 1);
                    return s;
                }
            }

            return {};
        }

        std::vector<std::string> LoadJointNames(const XMLNode &skin)
        {
            // <skin><joints><input semantic="JOINT" source="#..."/> points at a <source> holding the names.
            const auto sourceId = FindInputSource(skin.GetChild("joints"), "JOINT");
            const auto source   = FindSource(skin, sourceId);

            if (!source)
            {
                Log::Error("COLLADA skin has no joint name source '{}'.", sourceId);
                return {};
            }

            // Blender writes Name_array, some exporters write IDREF_array.
            const std::array<std::string, 2> arrayNames{"Name_array", "IDREF_array"};

            for (const auto &arrayName: arrayNames)
            {
                for (const auto &array: source->GetChildren(arrayName))
                {

                    std::istringstream stream(array.GetContent());
                    std::vector<std::string> out;
                    std::string token;

                    while (stream >> token)
                        out.push_back(token);

                    return out;
                }
            }

            Log::Error("COLLADA joint source '{}' has no Name_array/IDREF_array.", sourceId);
            return {};
        }

        std::vector<float> LoadWeightValues(const XMLNode &skin, const XMLNode &vertexWeightsNode)
        {
            const auto sourceId = FindInputSource(vertexWeightsNode, "WEIGHT");
            const auto source   = FindSource(skin, sourceId);

            if (!source)
            {
                Log::Error("COLLADA skin has no weight source '{}'.", sourceId);
                return {};
            }

            return ParseNumericArrayFromStr<float>(source->GetChild("float_array").GetContent());
        }

        std::vector<VertexWeights> LoadVertexWeights(const XMLNode &vertexWeightsNode,
                                                     const std::vector<float> &weights, const std::size_t jointCount,
                                                     const std::uint32_t maxWeights)
        {
            std::uint32_t jointOffset  = UINT32_MAX;
            std::uint32_t weightOffset = UINT32_MAX;
            std::uint32_t stride       = 0;

            // Determine input offsets and vertex stride.
            for (const auto &input: vertexWeightsNode.GetChildren("input"))
            {
                const auto semantic = input.GetAttribute("semantic");
                const auto offset   = static_cast<std::uint32_t>(std::stoul(input.GetAttribute("offset")));

                stride = std::max(stride, offset + 1);

                if (semantic == "JOINT")
                    jointOffset = offset;
                else if (semantic == "WEIGHT")
                    weightOffset = offset;
            }

            if (jointOffset == UINT32_MAX || weightOffset == UINT32_MAX || stride == 0)
            {
                Log::Error("Invalid COLLADA vertex_weights input layout.");
                return {};
            }

            // <vcount> and <v> are child elements, not attributes.
            const auto counts =
                    ParseNumericArrayFromStr<std::uint32_t>(vertexWeightsNode.GetChild("vcount").GetContent());
            const auto rawData = ParseNumericArrayFromStr<std::uint32_t>(vertexWeightsNode.GetChild("v").GetContent());

            std::vector<VertexWeights> result;
            result.reserve(counts.size());

            std::size_t pointer = 0;

            for (const auto count: counts)
            {
                VertexWeights skin;

                for (std::uint32_t i = 0; i < count; ++i)
                {
                    if (pointer + stride > rawData.size())
                    {
                        Log::Error("Invalid COLLADA vertex weight data.");
                        return {};
                    }

                    const std::uint32_t jointId  = rawData[pointer + jointOffset];
                    const std::uint32_t weightId = rawData[pointer + weightOffset];

                    if (jointId >= jointCount || weightId >= weights.size())
                    {
                        Log::Error("COLLADA vertex weight references a missing joint or weight.");
                        return {};
                    }

                    skin.AddJointEffect(jointId, weights[weightId]);
                    pointer += stride;
                }

                skin.LimitJointNumber(maxWeights);
                result.emplace_back(std::move(skin));
            }

            if (pointer != rawData.size())
                Log::Error("Unexpected trailing COLLADA vertex weight data.");

            return result;
        }
    } // namespace

    Skeleton AnimationModule::LoadSkeleton(const XMLNode &colladaRoot, const Mat4 &correction)
    {
        Skeleton skeleton;

        const auto scenes = colladaRoot.GetChild("library_visual_scenes");

        for (const auto &scene: scenes.GetChildren("visual_scene"))
        {
            for (const auto &node: scene.GetChildren("node"))
                ParseNode(node, skeleton, Skeleton::NoParent, correction);

            break; // first visual scene only
        }

        if (skeleton.Empty())
        {
            Log::Warning("COLLADA document contains no JOINT nodes.");
            return skeleton;
        }

        skeleton.RebuildInverseBind();
        return skeleton;
    }

    Skin AnimationModule::LoadSkin(const XMLNode &colladaRoot, const Skeleton &skeleton, const std::uint32_t maxWeights)
    {
        const auto controllers = colladaRoot.GetChild("library_controllers");

        for (const auto &controller: controllers.GetChildren("controller"))
        {
            const auto skinNode          = controller.GetChild("skin");
            const auto vertexWeightsNode = skinNode.GetChild("vertex_weights");

            auto jointNames    = LoadJointNames(skinNode);
            const auto weights = LoadWeightValues(skinNode, vertexWeightsNode);
            auto vertexWeights = LoadVertexWeights(vertexWeightsNode, weights, jointNames.size(), maxWeights);

            Skin skin(std::move(jointNames), std::move(vertexWeights));
            skin.BindToSkeleton(skeleton);
            return skin;
        }

        Log::Warning("COLLADA document contains no skin controller.");
        return {};
    }
} // namespace SF::Engine
