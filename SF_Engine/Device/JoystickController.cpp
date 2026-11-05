#include "JoystickController.hpp"
#include <Engine/Log/Log.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace SF::Engine
{
    namespace
    {
        constexpr size_t kPortCount = Reflection::enum_count<JoystickPort>();

        uint16_t ToU16(const float v) { return static_cast<uint16_t>(std::clamp(v, 0.0f, 1.0f) * 65535.0f + 0.5f); }

        JoystickType ConvertType(const SDL_JoystickType t)
        {
            switch (t)
            {
                case SDL_JOYSTICK_TYPE_GAMEPAD:
                    return JoystickType::Gamepad;
                case SDL_JOYSTICK_TYPE_WHEEL:
                    return JoystickType::Wheel;
                case SDL_JOYSTICK_TYPE_ARCADE_STICK:
                    return JoystickType::ArcadeStick;
                case SDL_JOYSTICK_TYPE_FLIGHT_STICK:
                    return JoystickType::FlightStick;
                case SDL_JOYSTICK_TYPE_DANCE_PAD:
                    return JoystickType::DancePad;
                case SDL_JOYSTICK_TYPE_GUITAR:
                    return JoystickType::Guitar;
                case SDL_JOYSTICK_TYPE_DRUM_KIT:
                    return JoystickType::DrumKit;
                case SDL_JOYSTICK_TYPE_THROTTLE:
                    return JoystickType::Throttle;
                default:
                    return JoystickType::Unknown;
            }
        }

        SDL_JoystickType ConvertType(const JoystickType t)
        {
            switch (t)
            {
                case JoystickType::Gamepad:
                    return SDL_JOYSTICK_TYPE_GAMEPAD;
                case JoystickType::Wheel:
                    return SDL_JOYSTICK_TYPE_WHEEL;
                case JoystickType::ArcadeStick:
                    return SDL_JOYSTICK_TYPE_ARCADE_STICK;
                case JoystickType::FlightStick:
                    return SDL_JOYSTICK_TYPE_FLIGHT_STICK;
                case JoystickType::DancePad:
                    return SDL_JOYSTICK_TYPE_DANCE_PAD;
                case JoystickType::Guitar:
                    return SDL_JOYSTICK_TYPE_GUITAR;
                case JoystickType::DrumKit:
                    return SDL_JOYSTICK_TYPE_DRUM_KIT;
                case JoystickType::Throttle:
                    return SDL_JOYSTICK_TYPE_THROTTLE;
                default:
                    return SDL_JOYSTICK_TYPE_UNKNOWN;
            }
        }

        JoystickPowerState ConvertPower(const SDL_PowerState s)
        {
            switch (s)
            {
                case SDL_POWERSTATE_ON_BATTERY:
                    return JoystickPowerState::OnBattery;
                case SDL_POWERSTATE_NO_BATTERY:
                    return JoystickPowerState::NoBattery;
                case SDL_POWERSTATE_CHARGING:
                    return JoystickPowerState::Charging;
                case SDL_POWERSTATE_CHARGED:
                    return JoystickPowerState::Charged;
                case SDL_POWERSTATE_ERROR:
                    return JoystickPowerState::Error;
                default:
                    return JoystickPowerState::Unknown;
            }
        }

        std::string SafeStr(const char *s) { return s ? std::string(s) : std::string(); }
    } // namespace

    // ------------------------------------------------------------------------
    // Joystick
    // ------------------------------------------------------------------------

    bool Joystick::Open(const uint32_t id, const JoystickPort slot)
    {
        handle = SDL_OpenJoystick(id);
        if (!handle)
        {
            Log::Out("Failed to open joystick ", id, ": ", SDL_GetError(), '\n');
            return false;
        }

        instanceId = id;
        port       = slot;
        connected  = true;

        info.name            = SafeStr(SDL_GetJoystickName(handle));
        info.path            = SafeStr(SDL_GetJoystickPath(handle));
        info.serial          = SafeStr(SDL_GetJoystickSerial(handle));
        info.vendorId        = SDL_GetJoystickVendor(handle);
        info.productId       = SDL_GetJoystickProduct(handle);
        info.productVersion  = SDL_GetJoystickProductVersion(handle);
        info.firmwareVersion = SDL_GetJoystickFirmwareVersion(handle);
        info.type            = ConvertType(SDL_GetJoystickType(handle));
        info.isGamepad       = SDL_IsGamepad(id);
        info.isVirtual       = SDL_IsJoystickVirtual(id);

        char guidStr[33] = {};
        SDL_GUIDToString(SDL_GetJoystickGUID(handle), guidStr, sizeof(guidStr));
        info.guid = guidStr;

        const SDL_PropertiesID props = SDL_GetJoystickProperties(handle);
        caps.monoLed                 = SDL_GetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_MONO_LED_BOOLEAN, false);
        caps.rgbLed                  = SDL_GetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_RGB_LED_BOOLEAN, false);
        caps.playerLed               = SDL_GetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_PLAYER_LED_BOOLEAN, false);
        caps.rumble                  = SDL_GetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false);
        caps.triggerRumble = SDL_GetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_TRIGGER_RUMBLE_BOOLEAN, false);

        // Current state snapshot, so the first event is a real change.
        const int axisCount   = std::max(SDL_GetNumJoystickAxes(handle), 0);
        const int buttonCount = std::max(SDL_GetNumJoystickButtons(handle), 0);
        const int hatCount    = std::max(SDL_GetNumJoystickHats(handle), 0);

        axes.assign(static_cast<size_t>(axisCount), 0);
        axesInitial.assign(static_cast<size_t>(axisCount), 0);
        for (int i = 0; i < axisCount; i++)
        {
            axes[i]        = SDL_GetJoystickAxis(handle, i);
            Sint16 initial = 0;
            if (SDL_GetJoystickAxisInitialState(handle, i, &initial))
                axesInitial[i] = initial;
            else
                axesInitial[i] = axes[i];
        }

        buttons.assign(static_cast<size_t>(buttonCount), InputAction::Release);
        for (int i = 0; i < buttonCount; i++)
            buttons[i] = SDL_GetJoystickButton(handle, i) ? InputAction::Repeat : InputAction::Release;

        hats.assign(static_cast<size_t>(hatCount), Bitmask<JoystickPositionValue>(JoystickPositionValue::Centered));
        for (int i = 0; i < hatCount; i++)
            hats[i] = Bitmask<JoystickPositionValue>(SDL_GetJoystickHat(handle, i));

        return true;
    }

    void Joystick::Close()
    {
        if (handle)
        {
            SDL_CloseJoystick(handle);
            handle = nullptr;
        }
        connected  = false;
        instanceId = 0;
        info       = {};
        caps       = {};
        axes.clear();
        axesInitial.clear();
        buttons.clear();
        hats.clear();
    }

    void Joystick::Update()
    {
        if (!connected)
            return;

        for (auto &button: buttons)
            if (button == InputAction::Press)
                button = InputAction::Repeat;
    }

    float Joystick::Normalize(const int16_t value) const
    {
        const float f = value < 0 ? static_cast<float>(value) / 32768.0f : static_cast<float>(value) / 32767.0f;
        const float a = std::fabs(f);
        if (a <= deadzone)
            return 0.0f;
        // Rescale so output ramps smoothly from 0 at the dead zone edge.
        const float scaled = (a - deadzone) / (1.0f - deadzone);
        return f < 0 ? -scaled : scaled;
    }

    void Joystick::HandleAxis(const uint8_t axis, const int16_t value)
    {
        if (axis >= axes.size())
            axes.resize(axis + 1, 0);

        const float before = Normalize(axes[axis]);
        axes[axis]         = value;
        if (const float after = Normalize(value); before != after)
            onAxis(axis, after);
    }

    void Joystick::HandleButton(const uint8_t button, const bool down)
    {
        if (button >= buttons.size())
            buttons.resize(button + 1, InputAction::Release);

        const InputAction action = down ? InputAction::Press : InputAction::Release;
        buttons[button]          = action;
        onButton(button, action);
    }

    void Joystick::HandleHat(const uint8_t hat, const uint8_t value)
    {
        if (hat >= hats.size())
            hats.resize(hat + 1, Bitmask<JoystickPositionValue>(JoystickPositionValue::Centered));

        hats[hat] = Bitmask<JoystickPositionValue>(value);
        onHat(hat, hats[hat]);
    }

    float Joystick::GetAxis(const JoystickAxis axis) const
    {
        if (connected && axis < axes.size())
            return Normalize(axes[axis]);
        return 0.0f;
    }

    int16_t Joystick::GetAxisRaw(const JoystickAxis axis) const
    {
        if (connected && axis < axes.size())
            return axes[axis];
        return 0;
    }

    float Joystick::GetAxisInitialState(const JoystickAxis axis) const
    {
        if (connected && axis < axesInitial.size())
        {
            const int16_t v = axesInitial[axis];
            return v < 0 ? static_cast<float>(v) / 32768.0f : static_cast<float>(v) / 32767.0f;
        }
        return 0.0f;
    }

    InputAction Joystick::GetButton(const JoystickButton button) const
    {
        if (connected && button < buttons.size())
            return buttons[button];
        return InputAction::Release;
    }

    Bitmask<JoystickPositionValue> Joystick::GetHat(const JoystickHat hat) const
    {
        if (connected && hat < hats.size())
            return hats[hat];
        return JoystickPositionValue::Centered;
    }

    JoystickConnection Joystick::GetConnection() const
    {
        if (!handle)
            return JoystickConnection::Invalid;

        switch (SDL_GetJoystickConnectionState(handle))
        {
            case SDL_JOYSTICK_CONNECTION_WIRED:
                return JoystickConnection::Wired;
            case SDL_JOYSTICK_CONNECTION_WIRELESS:
                return JoystickConnection::Wireless;
            case SDL_JOYSTICK_CONNECTION_UNKNOWN:
                return JoystickConnection::Unknown;
            default:
                return JoystickConnection::Invalid;
        }
    }

    JoystickPower Joystick::GetPower() const
    {
        JoystickPower power;
        if (!handle)
            return power;

        int percent   = -1;
        power.state   = ConvertPower(SDL_GetJoystickPowerInfo(handle, &percent));
        power.percent = percent;
        return power;
    }

    int Joystick::GetPlayerIndex() const { return handle ? SDL_GetJoystickPlayerIndex(handle) : -1; }

    bool Joystick::SetPlayerIndex(const int playerIndex) const
    {
        return handle && SDL_SetJoystickPlayerIndex(handle, playerIndex);
    }

    bool Joystick::Rumble(const float lowFrequency, const float highFrequency, const uint32_t durationMs) const
    {
        return handle && SDL_RumbleJoystick(handle, ToU16(lowFrequency), ToU16(highFrequency), durationMs);
    }

    bool Joystick::RumbleTriggers(const float left, const float right, const uint32_t durationMs) const
    {
        return handle && SDL_RumbleJoystickTriggers(handle, ToU16(left), ToU16(right), durationMs);
    }

    void Joystick::StopRumble() const
    {
        if (!handle)
            return;
        SDL_RumbleJoystick(handle, 0, 0, 0);
        SDL_RumbleJoystickTriggers(handle, 0, 0, 0);
    }

    void Joystick::SetLedColor(const Color col)
    {
        color = col;
        SetLed(col.Swizzle<"rgb">());
    }

    void Joystick::SetLed(const Vec3 col) const
    {
        if (handle && (caps.rgbLed || caps.monoLed))
            SDL_SetJoystickLED(handle, col.r, col.g, col.b);
    }

    bool Joystick::SendEffect(const void *data, const size_t size) const
    {
        return handle && SDL_SendJoystickEffect(handle, data, static_cast<int>(size));
    }

    bool Joystick::SetVirtualAxis(const JoystickAxis axis, const int16_t value) const
    {
        return handle && info.isVirtual && SDL_SetJoystickVirtualAxis(handle, axis, value);
    }

    bool Joystick::SetVirtualButton(const JoystickButton button, const bool down) const
    {
        return handle && info.isVirtual && SDL_SetJoystickVirtualButton(handle, button, down);
    }

    bool Joystick::SetVirtualHat(const JoystickHat hat, const Bitmask<JoystickPositionValue> value) const
    {
        return handle && info.isVirtual && SDL_SetJoystickVirtualHat(handle, hat, value.get());
    }

    JoystickController::JoystickController()
    {
        if (constexpr uint32_t flags = SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC;
            !SDL_InitSubSystem(flags))
        {
            Log::Out("Failed to init SDL joystick subsystem: ", SDL_GetError(), '\n');
            return;
        }

        for (size_t i = 0; i < kPortCount; i++)
            joysticks[i].port = static_cast<JoystickPort>(i);

        // Devices that were already plugged in before we started.
        int count = 0;
        if (SDL_JoystickID *ids = SDL_GetJoysticks(&count))
        {
            for (int i = 0; i < count; i++)
                AddJoystick(ids[i]);
            SDL_free(ids);
        }
    }

    JoystickController::~JoystickController()
    {
        for (auto &joystick: joysticks)
            joystick.Close();
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }

    void JoystickController::Update()
    {
        for (auto &joystick: joysticks)
            joystick.Update();
    }

    void JoystickController::AddJoystick(const uint32_t instanceId)
    {
        // The init-time enumeration and the SDL_EVENT_JOYSTICK_ADDED events can both report a device.
        if (FindByInstanceId(instanceId))
            return;

        for (size_t i = 0; i < kPortCount; i++)
        {
            Joystick &joystick = joysticks[i];
            if (joystick.connected)
                continue;

            if (!joystick.Open(instanceId, static_cast<JoystickPort>(i)))
                return;

            Log::Out("Joystick connected: '", joystick.info.name, "' to port ", i + 1, '\n');
            joystick.onConnect(true);
            return;
        }

        Log::Out("No free joystick port for '", SafeStr(SDL_GetJoystickNameForID(instanceId)), "'\n");
    }

    void JoystickController::RemoveJoystick(const uint32_t instanceId)
    {
        Joystick *joystick = FindByInstanceId(instanceId);
        if (!joystick)
            return;

        Log::Out("Joystick disconnected from port ", static_cast<int>(joystick->port) + 1, '\n');
        joystick->connected = false; // observers see it as gone during the callback
        joystick->onConnect(false);
        joystick->Close();
    }

    bool JoystickController::HandleEvent(const SDL_Event &event)
    {
        switch (event.type)
        {
            case SDL_EVENT_JOYSTICK_ADDED:
                AddJoystick(event.jdevice.which);
                return true;

            case SDL_EVENT_JOYSTICK_REMOVED:
                RemoveJoystick(event.jdevice.which);
                return true;

            case SDL_EVENT_JOYSTICK_AXIS_MOTION:
                if (auto *j = FindByInstanceId(event.jaxis.which))
                    j->HandleAxis(event.jaxis.axis, event.jaxis.value);
                return true;

            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            case SDL_EVENT_JOYSTICK_BUTTON_UP:
                if (auto *j = FindByInstanceId(event.jbutton.which))
                    j->HandleButton(event.jbutton.button, event.jbutton.down);
                return true;

            case SDL_EVENT_JOYSTICK_HAT_MOTION:
                if (auto *j = FindByInstanceId(event.jhat.which))
                    j->HandleHat(event.jhat.hat, event.jhat.value);
                return true;

            case SDL_EVENT_JOYSTICK_BATTERY_UPDATED:
                if (auto *j = FindByInstanceId(event.jbattery.which))
                {
                    JoystickPower power;
                    power.state   = ConvertPower(event.jbattery.state);
                    power.percent = event.jbattery.percent;
                    j->onBattery(power);
                }
                return true;

            case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE:
                return true;

            default:
                return false;
        }
    }

    Joystick *JoystickController::FindByInstanceId(const uint32_t instanceId)
    {
        for (auto &joystick: joysticks)
            if (joystick.connected && joystick.instanceId == instanceId)
                return &joystick;
        return nullptr;
    }

    size_t JoystickController::GetConnectedCount() const
    {
        return static_cast<size_t>(ranges::count_if(joysticks, [](const Joystick &j) { return j.connected; }));
    }

    const Joystick *JoystickController::GetJoystick(const JoystickPort port) const
    {
        return &joysticks[static_cast<size_t>(port)];
    }

    Joystick *JoystickController::GetJoystick(const JoystickPort port) { return &joysticks[static_cast<size_t>(port)]; }

    void JoystickController::SetEventsEnabled(const bool enabled) { SDL_SetJoystickEventsEnabled(enabled); }
    bool JoystickController::AreEventsEnabled() { return SDL_JoystickEventsEnabled(); }

    uint32_t JoystickController::AttachVirtual(const VirtualJoystickDesc &desc)
    {
        SDL_VirtualJoystickDesc sdlDesc;
        SDL_INIT_INTERFACE(&sdlDesc);
        sdlDesc.type     = static_cast<Uint16>(ConvertType(desc.type));
        sdlDesc.naxes    = desc.axisCount;
        sdlDesc.nbuttons = desc.buttonCount;
        sdlDesc.nhats    = desc.hatCount;
        sdlDesc.name     = desc.name.c_str();

        const SDL_JoystickID id = SDL_AttachVirtualJoystick(&sdlDesc);
        if (id == 0)
            Log::Out("Failed to attach virtual joystick: ", SDL_GetError(), '\n');
        return id;
    }

    bool JoystickController::DetachVirtual(const uint32_t instanceId) { return SDL_DetachVirtualJoystick(instanceId); }
} // namespace SF::Engine
