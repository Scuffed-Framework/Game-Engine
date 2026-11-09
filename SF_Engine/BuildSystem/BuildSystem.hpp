#pragma onced
#include <Engine/Module.hpp>

#include <Engine/InitGame/AppInfo.hpp>

namespace SF::Engine
{
    constexpr void GenerateLinuxDesktopFile(AppInfo) {}

    using namespace std;
    class BuildSystem : public ModuleRegistrar<BuildSystem>
    {
        REGISTER_MODULE(BuildSystem, ModuleStage::Never);

    public:
        ~BuildSystem() override {}
        void Update() override {}
        bool Initialize() override { return true; }
    };
} // namespace SF::Engine
