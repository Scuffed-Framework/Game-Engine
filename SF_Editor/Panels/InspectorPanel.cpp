#include "InspectorPanel.hpp"
#include <EntityComponentSystem/Entity.hpp>
#include <Gui/ImGui/Declare_Widget.hpp>
#include <Gui/ImGui/ocornut/imgui_internal.h>
#include <Gui/ImGui/ocornut/imgui_stdlib.h>
#include <LowLevel/Reflection/RTTI/SerializeContext.hpp>
#include <LowLevel/Reflection/ReflectedTypes.hpp>
#include <Math/Transform.hpp>
#include <Scene/SceneManager.hpp>
#include <UtilityClasses/StlExtensions.hpp>
#include "Panels.hpp"

namespace SF::Engine
{
    void InspectorPanel::Draw()
    {
        EntityRegistry &registry = SceneManager::Get()->GetScene()->GetEntities()->GetRegistry();

        m_registry = &registry;

        ImGui::Begin("Inspector", &ShowInspector);

        // Validate entity
        if (m_entity && !registry.IsValid(m_entity))
        {
            m_entity   = nullptr;
            m_entityId = 0;
        }

        if (!m_entity)
        {
            ImGui::TextDisabled("No entity selected");
            ImGui::End();
            return;
        }

        // Refresh entity if needed
        if (m_needsRefresh)
        {
            m_entity       = registry.Find(m_entityId);
            m_needsRefresh = false;
        }

        if (!m_entity)
        {
            ImGui::TextDisabled("Entity not found");
            ImGui::End();
            return;
        }

        DrawEntityProperties();

        ImGui::Separator();
        ImGui::Spacing();

        DrawComponents();

        ImGui::Spacing();
        ImGui::Separator();

        DrawAddComponentMenu();

        ImGui::End();
    }

    void InspectorPanel::DrawEntityProperties() const
    {
        ImGui::PushID("EntityProperties");
        // Name field
        ImGui::PushItemWidth(-1);
        std::string name = m_entity->GetName();
        if (ImGui::InputText("##Name", &name))
        {
            if (m_registry)
            {
                m_registry->RenameEntity(m_entity, name);
            } else
            {
                m_entity->SetName(name);
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Active toggle
        bool active = m_entity->IsActive();
        if (ImGui::Checkbox("Active", &active))
        {
            m_entity->SetActive(active);
        }

        ImGui::PopID();
    }

    void InspectorPanel::DrawComponents()
    {
        if (!m_entity)
            return;

        ImGui::TextColored(Vec4(0.4f, 0.8f, 1.0f, 1.0f), "Components");

        for (auto &[type, component]: m_entity->components)
        {
            if (!component)
                continue;

            ImGui::PushID(component.get());

            bool removed = false;
            if (ImGui::BeginPopupContextItem("ComponentOptions"))
            {
                if (ImGui::MenuItem("Reset"))
                {
                    component->Reset();
                    OnComponentEdited(*component);
                }
                if (ImGui::MenuItem("Remove"))
                    removed = m_entity->RemoveComponentByType(type);
                ImGui::EndPopup();
            }

            if (IsReflected(type))
            {
                if (DrawReflected(type, component.get(), "##w"))
                    OnComponentEdited(*component);
            }

            ImGui::PopID();
            if (removed)
                break;
        }
    }

    void InspectorPanel::OnComponentEdited(Component & /*component*/)
    {
        // Hook for dirty-flag / undo / gizmo refresh.
    }

    void InspectorPanel::DrawAddComponentMenu()
    {
        ImGui::TextColored(Vec4(0.4f, 0.8f, 1.0f, 1.0f), "Add Component");

        if (ImGui::Button("Add Component...", Vec2(-1, 0)))
        {
            ImGui::OpenPopup("AddComponentPopup");
            m_componentFilter.clear();
            m_focusFilter = true;
        }

        // Anchor the popup under the button, same width as the button.
        const Vec2 bmin = ImGui::GetItemRectMin();
        const Vec2 bmax = ImGui::GetItemRectMax();
        ImGui::SetNextWindowPos(Vec2(bmin.x, bmax.y));
        ImGui::SetNextWindowSizeConstraints(Vec2(bmax.x - bmin.x, 0), Vec2(bmax.x - bmin.x, 320));

        if (!ImGui::BeginPopup("AddComponentPopup"))
            return;

        if (m_focusFilter)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusFilter = false;
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Search components...", &m_componentFilter);
        ImGui::Separator();

        // Components the entity already has (one per type) are greyed out.
        std::unordered_set<std::string> have;
        for (auto &c: m_entity->components | views::values)
            if (c)
                have.emplace(c->GetTypeName());

        std::vector<const std::string *> names;
        for (const auto &name: Component::Registry() | views::keys)
            if (ContainsNoCase(name, m_componentFilter))
                names.push_back(&name);
        ranges::sort(names, [](auto *a, auto *b) { return *a < *b; });

        const std::string *picked = nullptr;
        const bool enter          = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);

        ImGui::BeginChild("##complist", Vec2(0, 200));
        for (const std::string *name: names)
        {
            const bool exists = have.contains(*name);
            ImGui::BeginDisabled(exists);
            if (ImGui::Selectable(name->c_str()) || (enter && !picked && !exists))
                picked = name;
            ImGui::EndDisabled();
        }
        if (names.empty())
            ImGui::TextDisabled("No matches");
        ImGui::EndChild();

        if (picked)
        {
            if (auto comp = Component::Create(*picked))
            {
                if (Component *raw = m_entity->AddComponent(std::move(comp)))
                    OnComponentEdited(*raw);
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void InspectorPanel::SetEntity(Entity *entity)
    {
        m_entity = entity;
        if (entity)
        {
            m_entityId = entity->GetId();
        } else
        {
            m_entityId = 0;
        }
        m_needsRefresh = true;
    }

    void InspectorPanel::Refresh() { m_needsRefresh = true; }
} // namespace SF::Engine
