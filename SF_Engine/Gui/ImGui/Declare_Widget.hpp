#pragma once
#include <Gui/ImGui/ocornut/imgui.h>
#include <cfloat>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#include <glm/gtc/type_ptr.hpp>
#pragma GCC diagnostic pop

namespace SF::Engine
{
    namespace WidgetTheme
    {
        inline constexpr Vec4 kAccent      = {0.00f, 0.54f, 1.00f, 1.00f};
        inline constexpr Vec4 kComponentBg = {0.08f, 0.08f, 0.08f, 1.00f};
        inline constexpr Vec4 kHeaderBg    = {0.12f, 0.12f, 0.14f, 1.00f};

        struct AxisStyle
        {
            const char *label;
            Vec4 dim, hot;
        };

        // X, Y, Z, W
        inline constexpr AxisStyle kAxes[4] = {
                {"X", {0.55f, 0.14f, 0.14f, 1.0f}, {0.86f, 0.20f, 0.20f, 1.0f}},
                {"Y", {0.14f, 0.50f, 0.14f, 1.0f}, {0.20f, 0.80f, 0.20f, 1.0f}},
                {"Z", {0.14f, 0.28f, 0.70f, 1.0f}, {0.20f, 0.40f, 1.00f, 1.0f}},
                {"W", {0.35f, 0.35f, 0.38f, 1.0f}, {0.55f, 0.55f, 0.60f, 1.0f}},
        };
    } // namespace WidgetTheme

    class WidgetBuilder
    {
    public:
        /**
         * @param sectionLabel   Shown in the CollapsingHeader.
         * @param uniqueId       PushID string
         * (pass the caller-supplied id).
         * @param labelColWidth  Width in pixels of the left (label) column.
         */
        explicit WidgetBuilder(const char *sectionLabel, const char *uniqueId, float labelColWidth = 90.0f) :
            m_sectionLabel(sectionLabel)
        {
            ImGui::PushID(uniqueId);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, WidgetTheme::kComponentBg);
            ImGui::BeginChild("##WB_Child", Vec2(0, 0), ImGuiChildFlags_AutoResizeY);
            m_open = DrawSectionHeader(sectionLabel);
            if (m_open)
            {
                ImGui::Spacing();
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, Vec2(4, 3));
                m_tableOpen = ImGui::BeginTable("##WB_Table", 2, ImGuiTableFlags_SizingStretchProp);
                if (m_tableOpen)
                {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelColWidth);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                }
            }
        }

        ~WidgetBuilder()
        {
            if (m_open)
            {
                if (m_tableOpen)
                {
                    ImGui::EndTable();
                    ImGui::PopStyleVar();
                }
            }

            ImGui::EndChild();
            ImGui::PopStyleColor(); // ChildBg
            ImGui::PopID();
        }
        WidgetBuilder(const WidgetBuilder &)            = delete;
        WidgetBuilder &operator=(const WidgetBuilder &) = delete;

        [[nodiscard]] bool IsOpen() const { return m_open && m_tableOpen; }

        void PropLabel(const char *label)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
        }

        void SubSection(const char *label) const
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, WidgetTheme::kAccent);
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(1);
            float y = ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.5f;
            ImGui::GetWindowDrawList()->AddLine(
                    Vec2(ImGui::GetCursorScreenPos().x, y),
                    Vec2(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x, y),
                    PackCol32(255, 255, 255, 30), 1.0f);
            ImGui::Spacing();
        }

        /**
         * N-component float row (2..4) with a coloured reset button per axis.
         * @param id
         * @param v
         * @param resetTo  N values; clicking an axis button snaps that component to its value.
         * @param speed
         * @param fmt
         * @return true if any component changed this frame.
         */
        template<int N>
        static bool VecRow(const char *id, float *v, const float *resetTo, const float speed = 0.1f,
                           const char *fmt = "%.3f")
        {
            static_assert(N >= 2 && N <= 4, "VecRow supports 2 to 4 components");
            const float btnW = ImGui::GetFrameHeight();
            bool changed     = false;

            ImGui::PushID(id);
            for (int i = 0; i < N; ++i)
            {
                constexpr float kBtnGap  = 1.0f;
                constexpr float kItemGap = 4.0f;
                if (i)
                    ImGui::SameLine(0, kItemGap);

                const auto &[label, dim, hot] = WidgetTheme::kAxes[i];
                ImGui::PushID(i);

                ImGui::PushStyleColor(ImGuiCol_Button, dim);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hot);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, hot);
                if (ImGui::Button(label, Vec2(btnW, 0)) && v[i] != resetTo[i])
                {
                    v[i]    = resetTo[i];
                    changed = true;
                }
                ImGui::PopStyleColor(3);

                // This drag gets an equal share of what's left after reserving room
                // for the buttons + gaps of the components still to come.
                ImGui::SameLine(0, kBtnGap);
                const int remaining = N - i - 1;
                ImGui::SetNextItemWidth((ImGui::GetContentRegionAvail().x - remaining * (btnW + kBtnGap + kItemGap)) /
                                        static_cast<float>(N - i));
                changed |= ImGui::DragFloat("##d", &v[i], speed, 0.0f, 0.0f, fmt);

                ImGui::PopID();
            }
            ImGui::PopID();
            return changed;
        }

        /// Overload for glm vectors: VecRow(id, myVec3, resetTo, ...).
        template<glm::length_t N>
        bool VecRow(const char *id, glm::vec<N, float> &v, const float *resetTo, float speed = 0.1f,
                    const char *fmt = "%.3f")
        {
            return VecRow<N>(id, glm::value_ptr(v), resetTo, speed, fmt);
        }

        void ColorSwatch(const Vec4 &c)
        {
            Vec2 sz{ImGui::GetContentRegionAvail().x, 8.0f};
            Vec2 cp      = ImGui::GetCursorScreenPos();
            uint32_t col = PackCol32(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255),
                                     static_cast<int>(c.b * 255), 255);
            ImGui::GetWindowDrawList()->AddRectFilled(cp, Vec2(cp.x + sz.x, cp.y + sz.y), col);
            ImGui::Dummy(sz);
            ImGui::Spacing();
        }

        // Draws a full-width intensity bar (e.g. light intensity preview).
        static void IntensityBar(float value, float maxValue, const Vec3 &colour)
        {
            Vec2 cp     = ImGui::GetCursorScreenPos();
            float barW  = ImGui::GetContentRegionAvail().x;
            float fillW = barW * glm::clamp(value / maxValue, 0.0f, 1.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(cp, Vec2(cp.x + barW, cp.y + 4), PackCol32(30, 30, 30, 255), 2);
            ImGui::GetWindowDrawList()->AddRectFilled(cp, Vec2(cp.x + fillW, cp.y + 4),
                                                      PackCol32(static_cast<int>(colour.r * 220),
                                                                static_cast<int>(colour.g * 220),
                                                                static_cast<int>(colour.b * 220), 200),
                                                      2);
            ImGui::Dummy(Vec2(barW, 4));
            ImGui::Spacing();
        }

    private:
        static bool DrawSectionHeader(const char *label)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, WidgetTheme::kHeaderBg);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Vec4(0.18f, 0.18f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, Vec4(0.22f, 0.22f, 0.28f, 1.0f));

            Vec2 p  = ImGui::GetCursorScreenPos();
            float h = ImGui::GetFrameHeight();
            ImGui::GetWindowDrawList()->AddRectFilled(p, Vec2(p.x + 3.0f, p.y + h), PackCol32(0, 138, 255, 200));
            ImGui::SetCursorScreenPos(Vec2(p.x + 6.0f, p.y));

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Vec2(4, 4));
            bool open = ImGui::CollapsingHeader(label);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            return open;
        }

        const char *m_sectionLabel;
        bool m_open      = false;
        bool m_tableOpen = false;
    };
} // namespace SF::Engine
