#pragma once
#include <string>

#include <Engine/VersionSemantic.hpp>

namespace SF::Engine
{

    struct AppInfo
    {
    public:
        std::string name;
        Version version;
    };
} // namespace SF::Engine
