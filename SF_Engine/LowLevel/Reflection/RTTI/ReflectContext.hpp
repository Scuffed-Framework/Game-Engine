#pragma once

#include "RTTI.hpp"

namespace SF::Reflection
{
    class ReflectContext
    {
        SF_RTTI_BASE(ReflectContext)
    public:
        virtual ~ReflectContext() = default;
    };
} // namespace SF::Reflection
