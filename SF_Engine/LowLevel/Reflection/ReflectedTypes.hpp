#pragma once
#include <LowLevel/Reflection/RTTI/SerializeContext.hpp>
#include <typeindex>

namespace SF::Engine
{
    bool IsReflected(std::type_index ti);
    bool DrawReflected(std::type_index ti, void *obj, const char *id, float labelW = 90.0f);

    template<typename T>
    bool DrawReflected(T &obj, const char *id, float labelW = 90.0f)
    {
        return DrawReflected(std::type_index(typeid(T)), &obj, id, labelW);
    }
} // namespace SF::Engine
