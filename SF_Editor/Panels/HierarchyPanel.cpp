#include "HierarchyPanel.hpp"
#include <Assets/AssetTextures.hpp>
#include <EntityComponentSystem/Entity.hpp>
#include <Gui/ImGui/ocornut/imgui_internal.h>
#include <Gui/ImGui/ocornut/imgui_stdlib.h>
#include <Rendering/RHI/Images/Image.hpp>
#include <Scene/SceneManager.hpp>
#include <UtilityClasses/StlExtensions.hpp>
#include <utility>
#include "Gui/ImGui/CommonGuiFunctions.hpp"
#include "Panels.hpp"

namespace SF::Engine
{
    namespace
    {
        constexpr float kIcon = 16.0f;
        ImTextureID Tex(const Image2d &img) { return img.GetTexID(); }

        void Icon(const Image2d &img) { ImGui::Image(Tex(img), Vec2(kIcon, kIcon), Vec2(0, 0), Vec2(1, 1)); }

        bool IsFolder(const Entity *e) { return e->HasTag("Folder"); } // ASSUMED: use your real tag check

        bool Matches(const Entity *e, const std::string &filter)
        {
            if (filter.empty() || ContainsNoCase(e->GetName(), filter))
                return true;
            return std::ranges::all_of(e->GetChildren(),
                                       [filter](auto &c)
                                       {
                                           if (Matches(c.get(), filter))
                                               return true;
                                           return false;
                                       });
        }
    } // namespace

    void HierarchyPanel::Draw()
    {
        if (!m_iconEye)
        {
            m_iconEye    = GetEyeLogo(64);
            m_iconEyeOff = GetEyeClosedLogo(64);
            m_iconEntity = GetEntityLogo(64);
            m_iconFolder = GetFolderTintedLogo(EmbeddedIcons::kFolderOrange, 64);
            m_iconWorld  = GetWorldLogo(64);
        }

        ImGui::Begin("Hierarchy", &ShowHierarchy);

        // Rounded pill search box
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Vec2(10, 5));
        ImGui::SetNextItemWidth(-1);
        static constexpr std::string hint("Search...");
        InputTextWithHint("##search", &hint, &m_filter);
        ImGui::PopStyleVar(2);
        ImGui::Spacing();

        Scene *scene = SceneManager::Get()->GetScene();

        const float footer              = ImGui::GetFrameHeightWithSpacing();
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                          ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
                                          ImGuiTableFlags_NoSavedSettings;

        if (ImGui::BeginTable("##outliner", 3, flags, Vec2(0, -footer)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##vis", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed, 28.0f);
            ImGui::TableSetupColumn("Item Label", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 140.0f);

            // Header row, with an eye in the first cell
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            ImGui::TableSetColumnIndex(0);
            ImGui::TableHeader("##vis");
            const Vec2 hmin = ImGui::GetItemRectMin();
            const Vec2 hmax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddImage(
                    Tex(*m_iconEye), Vec2((hmin.x + hmax.x - kIcon) * 0.5f, (hmin.y + hmax.y - kIcon) * 0.5f),
                    Vec2((hmin.x + hmax.x + kIcon) * 0.5f, (hmin.y + hmax.y + kIcon) * 0.5f));
            ImGui::TableSetColumnIndex(1);
            ImGui::TableHeader("Item Label");
            ImGui::TableSetColumnIndex(2);
            ImGui::TableHeader("Type");

            // World root row
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            const bool rootOpen =
                    ImGui::TreeNodeEx("##world", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns |
                                                         ImGuiTreeNodeFlags_DrawLinesToNodes);
            ImGui::SameLine();
            Icon(*m_iconWorld);
            ImGui::SameLine();
            ImGui::TextUnformatted(scene->GetName().c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("World");

            if (rootOpen)
            {
                for (const auto &root: scene->GetEntities()->GetRegistry().GetRoots())
                    DrawEntityRow(root.get());
                ImGui::TreePop();
            }
            ImGui::EndTable();
        }

        // Status bar
        ImGui::Separator();
        ImGui::TextDisabled("%d entities", static_cast<int>(scene->GetEntities()->GetSize()));

        ImGui::End();
    }

    void HierarchyPanel::DrawEntityRow(Entity *e)
    {
        if (!e || !Matches(e, m_filter))
            return;

        const auto &children = e->GetChildren();
        const bool hasKids   = !children.empty();
        const bool active    = e->IsActive();
        const bool folder    = IsFolder(e);

        ImGui::TableNextRow();
        ImGui::PushID(e);

        // Column 0: visibility eye (click to toggle active)
        ImGui::TableSetColumnIndex(0);
        {
            const float cx = ImGui::GetContentRegionAvail().x;
            const Vec2 p   = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(Vec2(p.x + (cx - kIcon) * 0.5f, p.y + 1));
            ImGui::InvisibleButton("##eye", Vec2(kIcon, kIcon));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                e->SetActive(!active);

            const int alpha = active ? (hovered ? 255 : 140) : 255;
            ImGui::GetWindowDrawList()->AddImage(Tex(active ? *m_iconEye : *m_iconEyeOff), ImGui::GetItemRectMin(),
                                                 ImGui::GetItemRectMax(), Vec2(0, 0), Vec2(1, 1),
                                                 PackCol32(255, 255, 255, alpha));
        }

        // Column 1: arrow + icon + name
        ImGui::TableSetColumnIndex(1);
        ImGuiTreeNodeFlags tf = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow |
                                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesToNodes;
        if (!hasKids)
            tf |= ImGuiTreeNodeFlags_Leaf;
        if (e->GetId() == m_selectedId)
            tf |= ImGuiTreeNodeFlags_Selected;

        // While searching, expand everything so matches are visible
        if (!m_filter.empty())
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);

        if (!active)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const bool open = ImGui::TreeNodeEx("##node", tf);
        if (!active)
            ImGui::PopStyleColor();

        // Selection
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            SetSelectedEntity(e);
            if (m_onEntitySelected)
                m_onEntitySelected(e);
        }

        // Drag source / reparent target (these apply to the tree node item above)
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            const EntityId id = e->GetId();
            ImGui::SetDragDropPayload("ENTITY", &id, sizeof(EntityId));
            ImGui::TextUnformatted(e->GetName().c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ENTITY"))
            {
                const EntityId draggedId = *static_cast<const EntityId *>(payload->Data);
                GetCurrentScene()->GetEntities()->Reparent(GetCurrentScene()->GetEntities()->FindById(draggedId), e);
            }
            ImGui::EndDragDropTarget();
        }

        // Context menu
        if (ImGui::BeginPopupContextItem("EntityContext"))
        {
            if (ImGui::MenuItem("Create Child"))
                m_needsRefresh = true;
            if (ImGui::MenuItem("Duplicate"))
                m_pendingDuplicate = e;
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del"))
            {
                e->MarkForRemoval();
                if (e->GetId() == m_selectedId)
                    SetSelectedEntity(nullptr);
                m_needsRefresh = true;
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        Icon(folder ? *m_iconFolder : *m_iconEntity);
        ImGui::SameLine();
        if (!active)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(e->GetName().c_str());
        if (!active)
            ImGui::PopStyleColor();

        // Column 2: type
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled("%s", folder ? "Folder" : "Entity");

        if (open)
        {
            for (const auto &c: children)
                DrawEntityRow(c.get());
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void HierarchyPanel::DrawEntityNode(Entity *entity)
    {
        if (!entity)
            return;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth |
                                   ImGuiTreeNodeFlags_OpenOnDoubleClick;

        // Handle leaf nodes
        if (entity->GetChildren().empty())
        {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }

        // Selection highlighting
        if (entity->GetId() == m_selectedId)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        // Use entity ID as unique identifier
        ImGui::PushID(static_cast<int>(entity->GetId()));

        // Entity active state indicator
        ImGui::PushStyleColor(ImGuiCol_Text, entity->IsActive() ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                                : Vec4(0.5f, 0.5f, 0.5f, 1.0f));

        bool opened = ImGui::TreeNodeEx("##EntityNode", flags, "%s", entity->GetName().c_str());

        ImGui::PopStyleColor();

        // Handle selection
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            m_selectedId     = entity->GetId();
            m_selectedEntity = entity;
            if (m_onEntitySelected)
            {
                m_onEntitySelected(entity);
            }
        }

        // Drag and drop source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            const EntityId entityId = entity->GetId();

            ImGui::SetDragDropPayload("ENTITY", &entityId, sizeof(EntityId));
            ImGui::Text("%s", entity->GetName().c_str());
            ImGui::EndDragDropSource();
        }

        // Drag and drop target (for reparenting)
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ENTITY"))
            {
                EntityId draggedId = *static_cast<const EntityId *>(payload->Data);
                GetCurrentScene()->GetEntities()->Reparent(GetCurrentScene()->GetEntities()->FindById(draggedId),
                                                           entity);
            }
            ImGui::EndDragDropTarget();
        }

        // Context menu
        if (ImGui::BeginPopupContextItem("EntityContext"))
        {
            if (ImGui::MenuItem("Create Child"))
            {
                // Create child entity
                m_needsRefresh = true;
            }
            if (ImGui::MenuItem("Duplicate"))
            {
                m_pendingDuplicate = entity;
            }
            if (ImGui::MenuItem("Create Folder"))
            {
                if (auto *folder = GetCurrentScene()->GetEntities()->GetRegistry().CreateEntity("New Folder"))
                {
                    folder->AddTag("Folder");
                    GetCurrentScene()->GetEntities()->Reparent(folder, entity); // same signature as your drag/drop
                }
                m_needsRefresh = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del"))
            {
                entity->MarkForRemoval();
                if (entity->GetId() == m_selectedId)
                {
                    m_selectedId     = 0;
                    m_selectedEntity = nullptr;
                }
                m_needsRefresh = true;
            }
            ImGui::EndPopup();
        }

        if (opened)
        {
            for (auto &child: entity->GetChildren())
            {
                DrawEntityNode(child.get());
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    void HierarchyPanel::DrawRowBackground(float height)
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        ImRect rowRect(window->WorkRect.Min.x, window->DC.CursorPos.y - height, window->WorkRect.Max.x,
                       window->DC.CursorPos.y);

        static int rowCount = 0;
        uint32_t bgColor    = (rowCount++ % 2 == 0) ? ImGui::GetColorU32(Vec4(0.18f, 0.18f, 0.18f, 1.0f))
                                                    : ImGui::GetColorU32(Vec4(0.25f, 0.25f, 0.25f, 1.0f));

        window->DrawList->AddRectFilled(rowRect.Min, rowRect.Max, bgColor);
    }

    void HierarchyPanel::CollectVisibleEntities(SF::Engine::Entity *entity,
                                                std::vector<SF::Engine::Entity *> &outEntities)
    {
        if (entity)
        {
            outEntities.push_back(entity);
            for (auto &child: entity->GetChildren())
            {
                CollectVisibleEntities(child.get(), outEntities);
            }
        }
    }

    void HierarchyPanel::SetOnEntitySelected(std::function<void(SF::Engine::Entity *)> callback)
    {
        m_onEntitySelected = std::move(callback);
    }

    void HierarchyPanel::SetSelectedEntity(SF::Engine::Entity *entity)
    {
        if (entity)
        {
            m_selectedEntity = entity;
            m_selectedId     = entity->GetId();
        } else
        {
            m_selectedEntity = nullptr;
            m_selectedId     = 0;
        }
    }

    void HierarchyPanel::DrawCreateOptions()
    {
        EntityRegistry &registry = SceneManager::Get()->GetScene()->GetEntities()->GetRegistry();
        Scene *scene             = SceneManager::Get()->GetScene();
        if (ImGui::BeginMenu("Create"))
        {
            if (ImGui::BeginMenu("Object"))
            {
                if (ImGui::MenuItem("Empty Entity"))
                {
                    registry.CreateEntity("New Entity");
                    m_needsRefresh = true;
                }
                if (ImGui::MenuItem("Cube"))
                {
                    SceneObject *cube    = scene->AddObject("Cube");
                    cube->meshSourcePath = "__cube__";
                    m_needsRefresh       = true;
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Folder"))
                {
                    if (auto *folder = registry.CreateEntity("New Folder")) // adjust if it returns a ref/unique_ptr
                        folder->AddTag("Folder");
                    m_needsRefresh = true;
                }
                // add more, also add more __mesh__ stuff
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Light"))
            {
                if (ImGui::MenuItem("Directional Light"))
                {
                    scene->AddLight("Directional Light", Lighting::LightType::Directional, {1, 1, 1}, 10, {0, 0, 0},
                                    {});
                    m_needsRefresh = true;
                }
                if (ImGui::MenuItem("Point Light"))
                {
                    scene->AddLight("Point Light", Lighting::LightType::Point, {1, 1, 1}, 10, {0, 0, 0}, {});
                    m_needsRefresh = true;
                }
                if (ImGui::MenuItem("Spot Light"))
                {
                    scene->AddLight("Spot Light", Lighting::LightType::Spot, {1, 1, 1}, 10, {0, 0, 0}, {});
                    m_needsRefresh = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
    }
} // namespace SF::Engine
