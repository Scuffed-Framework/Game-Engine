#pragma once
#include <LowLevel/Reflection/RTTI/SerializeContext.hpp>
#include <functional>
#include <typeindex>
#include <unordered_map>

namespace SF::Engine
{
    class WidgetBuilder;

    bool IsReflected(std::type_index ti);
    bool DrawReflected(std::type_index ti, void *obj, const char *id, float labelW = 90.0f);

    template<typename T>
    bool DrawReflected(T &obj, const char *id, float labelW = 90.0f)
    {
        return DrawReflected(std::type_index(typeid(T)), &obj, id, labelW);
    }

    // ---------------------------------------------------------------------------------------------------------
    // Open registry of field drawers. A reflected field whose C++ type has a drawer here is drawn by it, so a new
    // type (UUID, asset handle, curve, ...) is taught to the inspector right next to its own definition and
    // ReflectedTypes.cpp never has to change.
    //
    //   // in the type's header (inline variable => registered wherever the header is linked in)
    //   SF_REGISTER_FIELD_DRAWER(UUID, [](WidgetBuilder &, UUID &id, const Reflection::FieldMeta &)
    //   {
    //       ImGui::TextDisabled("%s", id.ToString().c_str());
    //       return false; // true if the value changed
    //   });
    //
    // Already automatic, no registration needed: float/int/uint/bool/string/Vec2-4/Quaternion/Rotation, scoped
    // enums (combo of their enumerators), and any class that is itself reflected (drawn as a nested section).
    // ---------------------------------------------------------------------------------------------------------
    using FieldDrawFn = std::function<bool(WidgetBuilder &, void *value, const Reflection::FieldMeta &)>;

    class FieldDrawers
    {
    public:
        static FieldDrawers &Instance();

        void Register(std::type_index type, FieldDrawFn fn) { m_fns[type] = std::move(fn); }

        [[nodiscard]] const FieldDrawFn *Find(std::type_index type) const
        {
            const auto it = m_fns.find(type);
            return it != m_fns.end() ? &it->second : nullptr;
        }

        // fn(WidgetBuilder&, T& value, const FieldMeta&) -> bool changed
        template<typename T, typename F>
        static bool Add(F &&fn)
        {
            Instance().Register(std::type_index(typeid(T)),
                                [fn = std::forward<F>(fn)](WidgetBuilder &wb, void *p, const Reflection::FieldMeta &m)
                                { return fn(wb, *static_cast<T *>(p), m); });
            return true;
        }

    private:
        std::unordered_map<std::type_index, FieldDrawFn> m_fns;
    };
} // namespace SF::Engine

#define SF_FIELD_DRAWER_CAT2(a, b) a##b
#define SF_FIELD_DRAWER_CAT(a, b) SF_FIELD_DRAWER_CAT2(a, b)
#define SF_REGISTER_FIELD_DRAWER(Type, ...)                                                                            \
    inline const bool SF_FIELD_DRAWER_CAT(sfFieldDrawer_, __COUNTER__) =                                               \
            ::SF::Engine::FieldDrawers::Add<Type>(__VA_ARGS__)
