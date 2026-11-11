#pragma once

#include <bitset>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <LowLevel/Reflection/RTTI/RTTI.hpp>
#include <UtilityClasses/NoCopy.hpp>
#include <UtilityClasses/TypeInformation.hpp>

#ifdef Always
    #undef Always
#endif

#ifdef InitSuccess
    #undef InitSuccess
#endif
#define InitSuccess true

#ifdef InitFailure
    #undef InitFailure
#endif
#define InitFailure false

namespace SF::Engine
{

    // Forward declaration
    class Module;

    /**
     * @brief Module update stages - defined before ModuleFactory to avoid circular dependency
     */
    enum class ModuleStage : uint8_t
    {
        Never,  // Module is never updated (utility module)
        Always, // Module is always updated (critical systems)
        Pre,    // Early update (input, events)
        Normal, // Standard update (game logic)
        Post,   // Late update (physics, cleanup)
        Render  // Rendering stage
    };

    /**
     * @brief Dictates at what point the Module is initialized.
     */
    enum class ModuleStartupStage : uint8_t
    {
        PreEngineInitComplete,
        OnEngineInit, // default
        ManualStartup,
        Normal,
        PostMainWindowInit,
        Custom // add a way to provide lambda calling at some place
    };

    /**
     * @brief Concept to ensure a type is derived from Module
     */
    template<typename T>
    concept ModuleDerived = std::is_base_of_v<Module, T> && !std::is_same_v<Module, T>;

    /**
     * @brief Factory for creating and managing module instances
     */
    template<typename Base>
    class ModuleFactory
    {
    public:
        /**
         * @brief Creation information for a module
         */
        struct CreateInfo
        {
            std::function<std::unique_ptr<Base>()> createFunc{};
            ModuleStage stage{};
            ModuleStartupStage start_stage = ModuleStartupStage::OnEngineInit;
            std::vector<TypeId> dependencies{};
            std::string_view name{}; // For debugging and logging
        };

        using RegistryMap = std::unordered_map<TypeId, CreateInfo>;

        virtual ~ModuleFactory() = default;

        /**
         * @brief Get the global module registry
         */
        static RegistryMap &Registry()
        {
            static RegistryMap impl;
            return impl;
        }

        /**
         * @brief Helper for specifying module dependencies
         */
        template<ModuleDerived... Args>
        class Requires // Ensure this is in a public section
        {
        public:
            [[nodiscard]] std::vector<TypeId> Get() const
            {
                std::vector<TypeId> dependencies;
                dependencies.reserve(sizeof...(Args));
                (dependencies.emplace_back(TypeInfo<Base>::template GetTypeId<Args>()), ...);
                return dependencies;
            }
        };

        /**
         * @brief Base registrar class for modules
         */
        template<typename T>
        class Registrar : public Base
        {
        public: // Change this from protected to public
            virtual ~Registrar()
            {
                if (static_cast<T *>(this) == s_instance)
                    s_instance = nullptr;
            }

            static T *Get() noexcept { return s_instance; }
            static bool Exists() noexcept { return s_instance != nullptr; }

            // Move Register into the public section so derived classes can call it via the macro
            template<typename... Args>
            static bool Register(ModuleStage stage, const ModuleStartupStage startup,
                                 Requires<Args...> dependencies = {})
            {
                s_registeredStage        = stage;
                s_registeredStartupStage = startup;
                s_registeredName         = typeid(T).name();

                ModuleFactory::Registry()[TypeInfo<Base>::template GetTypeId<T>()] = {
                        []() -> std::unique_ptr<Base>
                        {
                            s_instance = new T();
                            return std::unique_ptr<Base>(s_instance);
                        },
                        stage, s_registeredStartupStage, dependencies.Get(), s_registeredName};

                return true;
            }

            inline static auto s_registeredStage        = ModuleStage::Never;
            inline static auto s_registeredStartupStage = ModuleStartupStage::OnEngineInit;
            inline static std::string_view s_registeredName;

        private:
            inline static T *s_instance = nullptr;
        };

        template<typename T, typename... Args>
        static bool RegisterModule(ModuleStage stage, ModuleStartupStage startup, Requires<Args...> deps = {})
        {
            return Registrar<T>::Register(stage, startup, deps);
        }

        template<typename T, typename... Args>
        struct AutoRegister
        {
            AutoRegister(ModuleStage stage, ModuleStartupStage startup, Requires<Args...> deps = {})
            {
                Registrar<T>::Register(stage, startup, deps);
            }
        };
    };

    /**
     * @brief Base class for all engine modules
     */
    class Module : public ModuleFactory<Module>, NoCopy
    {
        SF_RTTI_BASE(Module)
    public:
        /**
         * @brief Module update stages (alias to ModuleStage)
         */
        using Stage = ModuleStage;

        using StartupStage = ModuleStartupStage;

        /**
         * @brief Stage and type identifier pair
         */
        using StageIndex   = std::pair<Stage, TypeId>;
        using StartupIndex = std::pair<StartupStage, TypeId>;

        virtual ~Module() = default;

        /**
         * @brief Update function called by the engine
         */
        virtual void Update() = 0;

        /**
         * @brief Optional initialization function
         * @return true if initialization succeeded, false otherwise
         */
        virtual bool Initialize() { return true; }

        /**
         * @brief Optional cleanup function
         */
        virtual void Shutdown() {}

        /**
         * @brief Get the module's update stage
         */
        [[nodiscard]] virtual Stage GetStage() const = 0;

        /**
         * @brief Get the module's start up stage
         */
        [[nodiscard]] virtual StartupStage GetStartupStage() const = 0;

        /**
         * @brief Get the module's type ID
         */
        [[nodiscard]] virtual TypeId GetTypeId() const = 0;

        /**
         * @brief Get the module's name (for debugging)
         */
        [[nodiscard]] virtual std::string_view GetName() const = 0;
    };

    // Explicit template instantiation
    template class TypeInformation<Module>;

    /**
     * @brief Helper base class that implements Module's pure virtual methods using CRTP
     * All modules should inherit from ModuleRegistrar<YourModule> instead of
     * ModuleRegistrar<YourModule>
     */
    template<typename T>
    class ModuleRegistrar : public Module::Registrar<T>
    {
    public:
        [[nodiscard]] ModuleStage GetStage() const override { return ModuleRegistrar<T>::s_registeredStage; }

        [[nodiscard]] ModuleStartupStage GetStartupStage() const override
        {
            return ModuleRegistrar<T>::s_registeredStartupStage;
        }

        [[nodiscard]] TypeId GetTypeId() const override { return TypeInfo<Module>::template GetTypeId<T>(); }

        [[nodiscard]] std::string_view GetName() const override { return ModuleRegistrar<T>::s_registeredName; }
    };

    /**
     * @brief Filter for selectively including/excluding modules
     */
    class ModuleFilter
    {
    public:
        ModuleFilter() { m_includeAll = true; }

        template<ModuleDerived T>
        [[nodiscard]] bool Check() const noexcept
        {
            return Check(TypeInfo<Module>::GetTypeId<T>());
        }

        [[nodiscard]] bool Check(TypeId typeId) const noexcept
        {
            if (m_includeAll)
                return !m_excluded.contains(typeId);
            return m_included.contains(typeId);
        }

        template<ModuleDerived T>
        ModuleFilter &Exclude() noexcept
        {
            m_excluded.insert(TypeInfo<Module>::GetTypeId<T>());
            m_includeAll = true;
            return *this;
        }

        template<ModuleDerived T>
        ModuleFilter &Include() noexcept
        {
            m_included.insert(TypeInfo<Module>::GetTypeId<T>());
            m_includeAll = false;
            return *this;
        }

        ModuleFilter &ExcludeAll() noexcept
        {
            m_includeAll = false;
            m_included.clear();
            return *this;
        }
        ModuleFilter &IncludeAll() noexcept
        {
            m_includeAll = true;
            m_excluded.clear();
            return *this;
        }

    private:
        bool m_includeAll = true;
        std::unordered_set<TypeId> m_included;
        std::unordered_set<TypeId> m_excluded;
    };

    /**
     * @brief Helper macro for registering modules
     * Usage: REGISTER_MODULE(MyModule, ModuleStage::Normal, Module::Requires<Dep1, Dep2>{})
     */
#define REGISTER_MODULE(ModuleClass, UpdateStage, ...)                                                                 \
    inline static bool ModuleClass##_registered =                                                                      \
            ModuleClass::Register(UpdateStage, ModuleStartupStage::OnEngineInit, ##__VA_ARGS__)

#define REGISTER_MODULE_EX(ModuleClass, UpdateStage, StartupStage, ...)                                                \
    inline static bool ModuleClass##_registered = ModuleClass::Register(UpdateStage, StartupStage, ##__VA_ARGS__)

} // namespace SF::Engine
