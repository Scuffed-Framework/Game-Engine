#pragma once

#include <Math/Timer.hpp>
#include <UtilityClasses/StreamFactory.hpp>

namespace SF::Engine
{
    class Entity;
    /**
     * @brief Class that represents a functional component attached to entity.
     *
     * Lifecycle (driven by EntityRegistry::Tick(), once per Scene::Update()):
     *   OnEnable() -> Start() -> { Update() each frame } -> OnDisable() -> Shutdown()
     *
     *  - OnEnable/OnDisable fire on every transition of "enabled && owner active in hierarchy".
     *  - Start() runs once, right after the first OnEnable().
     *  - Shutdown() runs once when the component is removed or its entity is destroyed/cleared, only if
     *    Start() ran. OnDisable() fires first if the component was still enabled.
     *  - A component that was never enabled gets none of these (its destructor is all that runs).
     *  Flips of SetEnabled()/Entity::SetActive() are picked up on the next Tick.
     */
    class Component : public StreamFactory<Component>, public Serializable
    {
        SF_RTTI(Component, Serializable, StreamFactory<Component>)
    public:
        virtual ~Component() = default;

        virtual void Start() {}
        virtual void Update() {}
        virtual void OnEnable() {}
        virtual void OnDisable() {}
        virtual void Shutdown() {}

        /**
         * @brief Called every time the fixed interval passes. You need to register this function in Start() using a
         * Timers::Get()->... lambda
         */
        virtual void FixedUpdate(DurationAsNanoseconds /*t*/) {}

        /**
         * @brief reset component stuff (override)
         */
        virtual void Reset() {}

        [[nodiscard]] virtual TypeId GetTypeId() const             = 0;
        [[nodiscard]] virtual std::string_view GetTypeName() const = 0;

        [[nodiscard]] bool IsEnabled() const { return enabled; }
        void SetEnabled(bool enable) { this->enabled = enable; }

        [[nodiscard]] bool IsRemoved() const { return removed; }
        void SetRemoved(bool removed) { this->removed = removed; }

        [[nodiscard]] Entity *GetOwner() const { return owner; }
        void SetOwner(Entity *entity) { this->owner = entity; }

        [[nodiscard]] virtual std::unique_ptr<Component> Clone() const { return nullptr; }

        // Lifecycle driver (called by Entity / EntityRegistry, not by gameplay code)

        /** True once Start() has run. */
        [[nodiscard]] bool HasStarted() const { return life.started; }

        /** True while OnEnable() has fired and OnDisable() hasn't, i.e. Update() should run. */
        [[nodiscard]] bool IsLive() const { return life.enableCalled && life.started && enabled && !removed; }

        /**
         * @brief Reconciles OnEnable/OnDisable with `ownerActive && enabled && !removed`, then runs Start() once.
         */
        void SyncLifecycle(bool ownerActive)
        {
            if (life.shutdownCalled)
                return;

            const bool wantActive = ownerActive && enabled && !removed;
            if (wantActive && !life.enableCalled)
            {
                life.enableCalled = true;
                OnEnable();
            } else if (!wantActive && life.enableCalled)
            {
                life.enableCalled = false;
                OnDisable();
            }

            if (life.enableCalled && !life.started)
            {
                life.started = true;
                Start();
            }
        }

        /**
         * @brief OnDisable (if still enabled) then Shutdown (if started). Idempotent.
         */
        void ShutdownLifecycle()
        {
            if (life.shutdownCalled)
                return;
            life.shutdownCalled = true;

            if (life.enableCalled)
            {
                life.enableCalled = false;
                OnDisable();
            }
            if (life.started)
                Shutdown();
        }

    private:
        // Runtime-only. A copy (Clone(), Transform copies, ...) starts fresh instead of inheriting "already started".
        struct Lifecycle
        {
            bool started        = false;
            bool enableCalled   = false;
            bool shutdownCalled = false;

            Lifecycle() = default;
            Lifecycle(const Lifecycle &) noexcept {}
            Lifecycle &operator=(const Lifecycle &) noexcept { return *this; }
        };

        Lifecycle life;
        bool enabled  = true;
        bool removed  = false;
        Entity *owner = nullptr;

    public:
        void Serialize(XMLNode &node) const override
        {
            XMLNode component = node.AddChild("Component");
            component.SetAttribute("enabled", enabled);
            component.SetAttribute("removed", removed);
            // started / owner are runtime only
        }

        void Deserialize(const XMLNode &node) override
        {
            const XMLNode component = node.GetChild("Component");
            component.GetAttribute("enabled", enabled);
            component.GetAttribute("removed", removed);
        }

        struct FixedState
        {
            std::atomic<uint32_t> pending{0};
            std::atomic_bool stopped{false};
        };
        std::shared_ptr<FixedState> fixed;
        TimerHandle fixedTimer;
        int64_t fixedStepNs = 0;
    };
} // namespace SF::Engine
