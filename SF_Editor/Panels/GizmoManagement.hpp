#pragma once
#include <EntityComponentSystem/Entity.hpp>
#include <Gui/ImGui/ImGuizmoIncludes.hpp>
#include <Gui/ImGui/StaticPanel.hpp>
#include <Gui/ImGui/UIRegistry.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <Math/Transform.hpp>

namespace SF::Engine
{
    class GizmoManagement : public StaticSingleInstancePanel<GizmoManagement>
    {
    public:
        struct CameraMatrices
        {
            glm::mat4 view{1.f};
            glm::mat4 proj{1.f};
        };

        GizmoManagement()
        {
            reg                                   = UIRegistry::Get().Register([this] { Draw(); });
            window_class.DockingAlwaysTabBar      = false;
            window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
        }
        ~GizmoManagement() { UIRegistry::Get().Unregister(reg); }

        void SetTarget(Entity *e) { target = e; }
        [[nodiscard]] Entity *GetTarget() const { return target; }

        void SetViewportProvider(std::function<Vec4()> fn) { viewportProvider = std::move(fn); } // x,y,w,h

    private:
        size_t reg     = 0;
        Entity *target = nullptr;
        std::function<Vec4()> viewportProvider;

        ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE mode           = ImGuizmo::LOCAL;
        bool snapEnabled              = false;
        float snapTranslate           = 0.5f;
        float snapRotate              = 15.f;
        float snapScale               = 0.1f;

        float barContentWidth = 0.f;

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;

        ImGuiWindowClass window_class;

        void Draw()
        {
            DrawManagementPanel();
            DrawGizmos();
        }

        void DrawManagementPanel()
        {
            // only used the first time (no saved ini entry); afterwards ImGui owns the position/dock state
            ImGui::SetNextWindowPos({8.f, ImGui::GetFrameHeight() + 8.f}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowClass(&window_class);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4.f, 4.f});

            if (ImGui::Begin("###GizmoManager", nullptr, window_flags))
            {
                constexpr float gripW = 14.f;
                const ImGuiIO &io     = ImGui::GetIO();

                // grip: Dummy (not a button) so the click falls through and drags/docks the window
                ImGui::Dummy({gripW, ImGui::GetFrameHeight()});
                {
                    const bool hot = ImGui::IsItemHovered();
                    if (hot)
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    const Vec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    const float cx     = (a.x + b.x) * 0.5f;
                    const uint32_t col = ImGui::GetColorU32(hot ? ImGuiCol_Text : ImGuiCol_TextDisabled);
                    ImDrawList *dl     = ImGui::GetWindowDrawList();
                    dl->AddLine({cx - 2.f, a.y + 4.f}, {cx - 2.f, b.y - 4.f}, col, 1.5f);
                    dl->AddLine({cx + 2.f, a.y + 4.f}, {cx + 2.f, b.y - 4.f}, col, 1.5f);
                }
                ImGui::SameLine(0.f, 2.f);

                // docked: fill the available width and scroll; floating: size to content
                const bool docked  = ImGui::IsWindowDocked();
                const float childW = docked ? 0.f : barContentWidth + 2.f;

                ImGui::BeginChild("##tools", {childW, ImGui::GetFrameHeight()}, false,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                {
                    if (ImGui::IsWindowHovered())
                    {
                        if (const float wheel = io.MouseWheel + io.MouseWheelH; wheel != 0.f)
                            ImGui::SetScrollX(ImGui::GetScrollX() - wheel * 32.f);
                    }

                    auto opButton = [&](const char *label, const ImGuizmo::OPERATION op)
                    {
                        const bool active = operation == op;
                        if (active)
                            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        if (ImGui::Button(label))
                            operation = op;
                        if (active)
                            ImGui::PopStyleColor();
                    };

                    opButton("Move##op", ImGuizmo::TRANSLATE);
                    ImGui::SameLine();
                    opButton("Rotate##op", ImGuizmo::ROTATE);
                    ImGui::SameLine();
                    opButton("Scale##op", ImGuizmo::SCALE);
                    ImGui::SameLine();
                    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                    ImGui::SameLine();
                    if (ImGui::Button(mode == ImGuizmo::LOCAL ? "Local###mode" : "Global###mode"))
                        mode = (mode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
                    ImGui::SameLine();
                    ImGui::Checkbox("Snap", &snapEnabled);

                    barContentWidth = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX();
                }
                ImGui::EndChild();
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }

        void DrawGizmos() const
        {
            const auto &cam = CameraController::Get().GetEditorCamera();
            if (!target || !viewportProvider)
                return;
            const auto *win = WindowManager::Get()->GetWindow(0);
            if (!win)
                return;


            const Vec4 vp = viewportProvider();

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList()); // draws over the scene, under panels
            ImGuizmo::SetRect(vp.x, vp.y, vp.z, vp.w);

            // TODO: world matrix of the target, and a way to write a local TRS back.
            auto *t = target->GetComponent<Transform>();
            if (!t)
                return;
            Mat4 world = t->ToWorldMatrix();

            float snap[3] = {0, 0, 0};
            if (snapEnabled)
            {
                const float v = operation == ImGuizmo::TRANSLATE ? snapTranslate
                                : operation == ImGuizmo::ROTATE  ? snapRotate
                                                                 : snapScale;
                snap[0] = snap[1] = snap[2] = v;
            }

            // TODO: MULTI WINDOW SUPPORT
            Mat4 projection = cam.GetProjection(win->GetAspectRatio());
            projection[1][1] *= -1.f; // undo the Vulkan Y flip for ImGuizmo only

            const bool changed =
                    ImGuizmo::Manipulate(glm::value_ptr(cam.GetView()), glm::value_ptr(projection), operation, mode,
                                         glm::value_ptr(world), nullptr, snapEnabled ? snap : nullptr);
            if (!changed || !ImGuizmo::IsUsing())
                return;

            glm::mat4 local = world;
            if (Entity *p = target->GetParent())
                if (const Transform *pt = p->GetComponent<Transform>())
                    local = glm::inverse(pt->ToWorldMatrix()) * world;

            switch (operation)
            {
                case ImGuizmo::TRANSLATE:
                    t->SetLocalPosition(Vec3(local[3]));
                    break;

                case ImGuizmo::SCALE:
                {
                    t->SetLocalScale(
                            {glm::length(Vec3(local[0])), glm::length(Vec3(local[1])), glm::length(Vec3(local[2]))});
                    break;
                }

                case ImGuizmo::ROTATE:
                {
                    const Vec3 s{glm::length(Vec3(local[0])), glm::length(Vec3(local[1])), glm::length(Vec3(local[2]))};
                    if (s.x < 1e-6f || s.y < 1e-6f || s.z < 1e-6f)
                        return;

                    glm::mat4 rotM{1.f};
                    rotM[0] = Vec4(Vec3(local[0]) / s.x, 0.f);
                    rotM[1] = Vec4(Vec3(local[1]) / s.y, 0.f);
                    rotM[2] = Vec4(Vec3(local[2]) / s.z, 0.f);

                    float yaw, pitch, roll;
                    glm::extractEulerAngleYXZ(rotM, yaw, pitch, roll);
                    const Vec3 next{glm::degrees(pitch), glm::degrees(yaw), glm::degrees(roll)};

                    const Vec3 cur = t->GetLocalRotation();
                    auto nearest   = [](const float c, const float n) { return c + std::remainder(n - c, 360.f); };
                    t->SetLocalRotation({nearest(cur.x, next.x), nearest(cur.y, next.y), nearest(cur.z, next.z)});
                    break;
                }

                default:
                    break;
            }
        }
    };
} // namespace SF::Engine
