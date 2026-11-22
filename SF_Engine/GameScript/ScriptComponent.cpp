#include "ScriptComponent.hpp"
#include <GameScript/ScriptEngine.hpp>

#include "Math/Timer.hpp"

#include <algorithm>

namespace SF::Engine
{
    namespace
    {
        // Upper bound of FixedUpdate calls per frame; extra ticks are dropped to avoid a spiral of death.
        constexpr uint32_t kMaxFixedCatchUp    = 8;
        constexpr int64_t kFallbackFixedStepNs = 20'000'000; // 20 ms
    } // namespace

    ScriptComponent::~ScriptComponent()
    {
        StopFixedTimer();
        if (object)
        {
            object->Release();
            object = nullptr;
        }
    }

    void ScriptComponent::Start()
    {
        if (!script.Load(payload))
            return;

        object = script.Instantiate();
        if (!object)
            return;

        // Methods are optional: a script may define only Update, etc.
        const auto *type = object->GetObjectType();
        startFn          = type->GetMethodByDecl("void Start()");
        updateFn         = type->GetMethodByDecl("void Update()");
        ResetFn          = type->GetMethodByDecl("void Reset()");
        OnEnableFn       = type->GetMethodByDecl("void OnEnable()");
        OnDisableFn      = type->GetMethodByDecl("void OnDisable()");
        FixedUpdateFn    = type->GetMethodByDecl("void FixedUpdate(float)"); // default value is read separately
        ShutdownFn       = type->GetMethodByDecl("void Shutdown()");

        if (startFn)
            ScriptEngine::Get()->Execute(startFn, object);
        StartFixedTimer();
    }

    void ScriptComponent::StartFixedTimer()
    {
        if (!object || !FixedUpdateFn || fixed)
            return;

        // The step comes from the script: void FixedUpdate(float timeNs = 20000000)
        fixedStepNs = GetDefaultArg<int64_t>(FixedUpdateFn).value_or(kFallbackFixedStepNs);
        if (fixedStepNs <= 0)
            fixedStepNs = kFallbackFixedStepNs;

        // The timer thread only touches this shared state, never the component or the script object.
        fixed      = std::make_shared<FixedState>();
        fixedTimer = Timers::Get()->RepeatUntil(
                [state = fixed] { state->pending.fetch_add(1, std::memory_order_relaxed); },
                std::chrono::nanoseconds(fixedStepNs), [state = fixed] { return state->stopped.load(); });
    }

    void ScriptComponent::StopFixedTimer()
    {
        if (fixed)
        {
            fixed->stopped = true; // the timer's stop condition sees this on its next wake
            fixed.reset();
        }
        if (fixedTimer)
        {
            fixedTimer->Destroy();
            fixedTimer.reset();
        }
    }

    void ScriptComponent::Update()
    {
        if (!object)
            return;

        // FixedUpdate runs here, on the main thread, before Update.
        if (fixed && FixedUpdateFn)
        {
            const uint32_t due = std::min(fixed->pending.exchange(0), kMaxFixedCatchUp);
            const auto step    = static_cast<float>(fixedStepNs);
            for (uint32_t i = 0; i < due; ++i)
                ScriptEngine::Get()->Execute(FixedUpdateFn, object,
                                             [step](asIScriptContext *ctx) { ctx->SetArgFloat(0, step); });
        }

        if (updateFn)
            ScriptEngine::Get()->Execute(updateFn, object);
    }

    void ScriptComponent::Reset()
    {
        if (object && ResetFn)
            ScriptEngine::Get()->Execute(ResetFn, object);
    }

    void ScriptComponent::OnEnable()
    {
        StartFixedTimer();
        if (object && OnEnableFn)
            ScriptEngine::Get()->Execute(OnEnableFn, object);
    }

    void ScriptComponent::OnDisable()
    {
        StopFixedTimer(); // repeat-until: the timer ends as soon as the component is disabled
        if (object && OnDisableFn)
            ScriptEngine::Get()->Execute(OnDisableFn, object);
    }

    void ScriptComponent::FixedUpdate() {}

    void ScriptComponent::Shutdown()
    {
        StopFixedTimer();
        if (object && ShutdownFn)
            ScriptEngine::Get()->Execute(ShutdownFn, object);
    }
} // namespace SF::Engine
