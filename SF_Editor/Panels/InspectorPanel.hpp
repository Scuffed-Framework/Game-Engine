#pragma once
#include <EntityComponentSystem/Component.hpp>
#include <EntityComponentSystem/Entity.hpp>
#include <Gui/ImGui/StaticPanel.hpp>
#include <Gui/ImGui/UIRegistry.hpp>
#include <Gui/ImGui/ocornut/imgui.h>
#include <functional>
#include <string>

namespace SF::Reflection
{
    struct ClassData;
    struct FieldMeta;
} // namespace SF::Reflection

namespace SF::Engine
{
    class WidgetBuilder;
    class EntityRegistry;

    class InspectorPanel : public StaticSingleInstancePanel<InspectorPanel>
    {
    public:
        using DrawFn = bool (*)(WidgetBuilder &, void *value, const Reflection::FieldMeta &);

        InspectorPanel()
        {
            reg = UIRegistry::Get().Register([this] { Draw(); });
        };
        ~InspectorPanel() { UIRegistry::Get().Unregister(reg); };

        void Draw();
        void SetEntity(Entity *entity);
        [[nodiscard]] Entity *GetEntity() const { return m_entity; }
        void Refresh();

    private:
        void DrawEntityProperties() const;
        void DrawComponents();
        void DrawAddComponentMenu();
        void OnComponentEdited(Component &component);

        Entity *m_entity           = nullptr;
        EntityId m_entityId        = 0;
        EntityRegistry *m_registry = nullptr;
        bool m_needsRefresh        = true;
        size_t reg;

        std::string m_componentFilter;
        bool m_focusFilter = false;
    };
} // namespace SF::Engine
