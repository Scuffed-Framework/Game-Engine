#include "ReflectedTypes.hpp"
#include <Gui/ImGui/Declare_Widget.hpp> // WidgetBuilder (strip the macros out of this header)
#include <Gui/ImGui/ocornut/imgui_stdlib.h>
#include <cstdint>

namespace SF::Engine
{
    using namespace Reflection;
    namespace
    {
        struct EulerCache
        {
            Vec3 euler{};
            Quaternion last{1, 0, 0, 0};
            bool init = false;
        };

        bool DrawQuat(WidgetBuilder &wb, Quaternion &q, const FieldMeta &m)
        {
            static std::unordered_map<std::uint64_t, EulerCache> cache; // keyed by the PushID stack
            EulerCache &c = cache[ImGui::GetID("##euler")];

            // Re-derive Euler only if the quat changed from elsewhere (gizmo, undo, load)
            if (!c.init || glm::abs(glm::dot(c.last, q)) < 1.0f - 1e-6f)
            {
                c.euler = glm::degrees(glm::eulerAngles(q));
                c.last  = q;
                c.init  = true;
            }

            static constexpr float kZero[3] = {0.0f, 0.0f, 0.0f};
            const bool changed              = wb.VecRow("v", c.euler, kZero, m.speed > 0 ? m.speed : 0.5f, "%.2f");
            if (changed)
            {
                q      = normalize(Quaternion(glm::radians(c.euler)));
                c.last = q;
            }
            return changed;
        }

        constexpr ImGuiColorEditFlags kColorFlags =
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueBar;

        bool DrawFields(WidgetBuilder &wb, const ClassData &cd, void *obj);

        bool DrawValue(WidgetBuilder &wb, const IFieldBinding &f, void *obj, const FieldMeta &m)
        {
            void *p         = f.GetPtr(obj);
            const char *fmt = m.format;
            switch (f.Kind())
            {
                case FieldKind::Float:
                {
                    auto *v = static_cast<float *>(p);
                    if (m.slider)
                        return ImGui::SliderFloat("##v", v, m.min, m.max, fmt ? fmt : "%.3f");
                    return ImGui::DragFloat("##v", v, m.speed, m.hasRange ? m.min : 0, m.hasRange ? m.max : 0,
                                            fmt ? fmt : "%.3f");
                }
                case FieldKind::Int:
                {
                    auto *v = static_cast<int32_t *>(p);
                    if (m.slider)
                        return ImGui::SliderInt("##v", v, (int) m.min, (int) m.max);
                    return ImGui::DragInt("##v", v, m.speed, m.hasRange ? (int) m.min : 0,
                                          m.hasRange ? (int) m.max : 0);
                }
                case FieldKind::UInt:
                {
                    auto lo = static_cast<uint32_t>(m.min), hi = static_cast<uint32_t>(m.max);
                    return ImGui::DragScalar("##v", ImGuiDataType_U32, p, m.speed, m.hasRange ? &lo : nullptr,
                                             m.hasRange ? &hi : nullptr);
                }
                case FieldKind::Bool:
                    return ImGui::Checkbox("##v", static_cast<bool *>(p));
                case FieldKind::String:
                    return ImGui::InputText("##v", static_cast<std::string *>(p));
                case FieldKind::Vec2:
                    return wb.VecRow("v", *static_cast<Vec2 *>(p), m.resetVec, m.speed, fmt ? fmt : "%.3f");

                case FieldKind::Vec3:
                    if (m.color)
                        return ImGui::ColorEdit3("##v", glm::value_ptr(*static_cast<Vec3 *>(p)), kColorFlags);
                    return wb.VecRow("v", *static_cast<Vec3 *>(p), m.resetVec, m.speed, fmt ? fmt : "%.3f");

                case FieldKind::Vec4:
                    if (m.color)
                        return ImGui::ColorEdit4("##v", glm::value_ptr(*static_cast<Vec4 *>(p)), kColorFlags);
                    return wb.VecRow("v", *static_cast<Vec4 *>(p), m.resetVec, m.speed, fmt ? fmt : "%.3f");
                case FieldKind::Enum:
                {
                    if (!m.enumItems)
                    {
                        ImGui::TextDisabled(R"((enum: add .Enum("A\0B\0")))");
                        return false;
                    }
                    int v = f.GetEnum(obj);
                    if (!ImGui::Combo("##v", &v, m.enumItems))
                        return false;
                    f.SetEnum(obj, v);
                    return true;
                }
                case FieldKind::Quat:
                    return DrawQuat(wb, *static_cast<Quaternion *>(p), m);
                default:
                    return false;
            }
        }

        bool DrawFields(WidgetBuilder &wb, const ClassData &cd, void *obj)
        {
            bool changed = false;
            for (auto &f: cd.fields)
            {
                const FieldMeta &m = f->meta;
                if (m.hidden || (m.visible && !m.visible(obj)))
                    continue;

                const char *label = m.displayName ? m.displayName : f->GetName();
                ImGui::PushID(f->GetName());

                if (f->Kind() == FieldKind::Object)
                {
                    if (const ClassData *nested = SerializeContext::Instance().FindClassData(f->GetFieldTypeId()))
                    {
                        wb.SubSection(label);
                        changed |= DrawFields(wb, *nested, f->GetPtr(obj));
                    } else
                    {
                        wb.PropLabel(label);
                        ImGui::TextDisabled("(not reflected)");
                    }
                } else
                {
                    wb.PropLabel(label);
                    if (m.readOnly)
                        ImGui::BeginDisabled();
                    changed |= DrawValue(wb, *f, obj, m);
                    if (m.readOnly)
                        ImGui::EndDisabled();
                    if (m.tooltip && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", m.tooltip);
                }
                ImGui::PopID();
            }
            return changed;
        }
    } // namespace

    bool IsReflected(std::type_index ti) { return SerializeContext::Instance().FindTypeId(ti) != nullptr; }

    bool DrawReflected(std::type_index ti, void *obj, const char *id, float labelW)
    {
        const auto &ctx     = SerializeContext::Instance();
        const TypeId *tid   = ctx.FindTypeId(ti);
        const ClassData *cd = tid ? ctx.FindClassData(*tid) : nullptr;
        if (!cd)
            return false;

        WidgetBuilder wb(cd->name, id, labelW);
        return wb.IsOpen() && DrawFields(wb, *cd, obj);
    }
} // namespace SF::Engine
