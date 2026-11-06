#pragma once
#include <LowLevel/Reflection/RTTI/SerializeContext.hpp>
#include <typeindex>

namespace SF::Engine
{
    using namespace std;
    bool IsReflected(type_index ti);
    bool DrawReflected(type_index ti, void *obj, const char *id, float labelW = 90.0f);

    template<typename T>
    bool DrawReflected(T &obj, const char *id, float labelW = 90.0f)
    {
        return DrawReflected(type_index(typeid(T)), &obj, id, labelW);
    }
} // namespace SF::Engine
