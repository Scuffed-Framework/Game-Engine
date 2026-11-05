#pragma once
#include <3rdPartyLibs/Rocket.hpp>
#include <Engine/Module.hpp>
#include <LowLevel/Bitmask.hpp>
#include <Platform/Input/ButtonCodes.hpp>

#include <LowLevel/Reflection/Static/StaticReflection.hpp>

#include <Rendering/Material/Color/Color.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Forward declarations so SDL stays out of the public header.
struct SDL_Joystick;
union SDL_Event;

namespace SF::Engine
{
    class JoystickController;
    using namespace std;

    enum class JoystickPositionValue : uint8_t
    {
        Centered = 0,
        Up       = 1, // == SDL_HAT_UP
        Right    = 2, // == SDL_HAT_RIGHT
        Down     = 4, // == SDL_HAT_DOWN
        Left     = 8  // == SDL_HAT_LEFT
    };
    ENABLE_BITMASK_OPERATORS(JoystickPositionValue)

    enum class JoystickPort : uint8_t
    {
        Port1  = 0,
        Port2  = 1,
        Port3  = 2,
        Port4  = 3,
        Port5  = 4,
        Port6  = 5,
        Port7  = 6,
        Port8  = 7,
        Port9  = 8,
        Port10 = 9,
        Port11 = 10,
        Port12 = 11,
        Port13 = 12,
        Port14 = 13,
        Port15 = 14,
        Port16 = 15
    };

    using JoystickAxis   = uint8_t;
    using JoystickButton = uint8_t;
    using JoystickHat    = uint8_t;

    enum class JoystickType : uint8_t
    {
        Unknown,
        Gamepad,
        Wheel,
        ArcadeStick,
        FlightStick,
        DancePad,
        Guitar,
        DrumKit,
        Throttle
    };

    enum class JoystickConnection : uint8_t
    {
        Invalid,
        Unknown,
        Wired,
        Wireless
    };

    enum class JoystickPowerState : uint8_t
    {
        Error,
        Unknown,
        OnBattery,
        NoBattery,
        Charging,
        Charged
    };

    struct JoystickPower
    {
        JoystickPowerState state = JoystickPowerState::Unknown;
        int percent              = -1; // 0-100, -1 if unknown
    };

    // Capabilities reported by SDL_GetJoystickProperties()
    struct JoystickCapabilities
    {
        bool monoLed       = false;
        bool rgbLed        = false;
        bool playerLed     = false;
        bool rumble        = false;
        bool triggerRumble = false;
    };

    // Static identification data of a device
    struct JoystickInfo
    {
        string name;
        string path;
        string serial;
        string guid; // SDL GUID string form
        uint16_t vendorId        = 0;
        uint16_t productId       = 0;
        uint16_t productVersion  = 0;
        uint16_t firmwareVersion = 0;
        JoystickType type        = JoystickType::Unknown;
        bool isGamepad           = false; // SDL has a gamepad mapping for it
        bool isVirtual           = false;
    };

    struct VirtualJoystickDesc
    {
        string name          = "Virtual Joystick";
        JoystickType type    = JoystickType::Unknown;
        uint16_t axisCount   = 0;
        uint16_t buttonCount = 0;
        uint16_t hatCount    = 0;
    };

    class Joystick
    {
        friend class JoystickController;

    public:
        ~Joystick() { Close(); }

        void Update();

        JoystickPort GetPort() const { return port; }
        bool IsConnected() const { return connected; }
        uint32_t GetInstanceId() const { return instanceId; } // SDL_JoystickID

        const JoystickInfo &GetInfo() const { return info; }
        string GetName() const { return info.name; }
        const JoystickCapabilities &GetCapabilities() const { return caps; }
        JoystickConnection GetConnection() const;
        JoystickPower GetPower() const;

        int GetPlayerIndex() const; // -1 if unassigned
        bool SetPlayerIndex(int playerIndex) const;

        size_t GetAxisCount() const { return axes.size(); }
        size_t GetButtonCount() const { return buttons.size(); }
        size_t GetHatCount() const { return hats.size(); }

        // Normalised to [-1, 1] with the configured dead zone applied.
        float GetAxis(JoystickAxis axis) const;
        // Raw SDL value in [-32768, 32767].
        int16_t GetAxisRaw(JoystickAxis axis) const;
        // Value the axis had when the device was opened (triggers rest at -1).
        float GetAxisInitialState(JoystickAxis axis) const;

        InputAction GetButton(JoystickButton button) const;
        Bitmask<JoystickPositionValue> GetHat(JoystickHat hat) const;

        void SetDeadzone(float deadzone)
        {
            this->deadzone = deadzone < 0.f ? 0.f : (deadzone > 0.99f ? 0.99f : deadzone);
        }
        float GetDeadzone() const { return deadzone; }

        // Intensities in [0, 1]. durationMs is clamped by SDL; 0 stops rumble.
        bool Rumble(float lowFrequency, float highFrequency, uint32_t durationMs) const;
        bool RumbleTriggers(float left, float right, uint32_t durationMs) const;
        void StopRumble() const;

        void SetLedColor(Color col);
        void SetLed(Vec3 col) const;
        Color GetLedColor() const { return color; }

        bool SendEffect(const void *data, size_t size) const;

        bool SetVirtualAxis(JoystickAxis axis, int16_t value) const;
        bool SetVirtualButton(JoystickButton button, bool down) const;
        bool SetVirtualHat(JoystickHat hat, Bitmask<JoystickPositionValue> value) const;

        rocket::signal<void(bool)> &OnConnect() { return onConnect; }
        rocket::signal<void(uint8_t, InputAction)> &OnButton() { return onButton; }
        rocket::signal<void(uint8_t, float)> &OnAxis() { return onAxis; }
        rocket::signal<void(uint8_t, Bitmask<JoystickPositionValue>)> &OnHat() { return onHat; }
        rocket::signal<void(JoystickPower)> &OnBattery() { return onBattery; }

    private:
        bool Open(uint32_t id, JoystickPort slot);
        void Close();

        void HandleAxis(uint8_t axis, int16_t value);
        void HandleButton(uint8_t button, bool down);
        void HandleHat(uint8_t hat, uint8_t value);

        float Normalize(int16_t value) const;

        SDL_Joystick *handle = nullptr;
        uint32_t instanceId  = 0;

        Color color;
        float deadzone = 0.0f;

        JoystickPort port = JoystickPort::Port1;
        bool connected    = false;
        JoystickInfo info;
        JoystickCapabilities caps;

        vector<int16_t> axes;
        vector<int16_t> axesInitial;
        vector<InputAction> buttons;
        vector<Bitmask<JoystickPositionValue>> hats;

        rocket::signal<void(bool)> onConnect;
        rocket::signal<void(uint8_t, InputAction)> onButton;
        rocket::signal<void(uint8_t, float)> onAxis;
        rocket::signal<void(uint8_t, Bitmask<JoystickPositionValue>)> onHat;
        rocket::signal<void(JoystickPower)> onBattery;
    };

    class JoystickController : public ModuleRegistrar<JoystickController>
    {
        REGISTER_MODULE(JoystickController, ModuleStage::Pre);

    public:
        JoystickController();
        ~JoystickController() override;

        void Update() override;

        bool HandleEvent(const SDL_Event &event);

        const Joystick *GetJoystick(JoystickPort port) const;
        Joystick *GetJoystick(JoystickPort port);

        Joystick *FindByInstanceId(uint32_t instanceId);

        size_t GetConnectedCount() const;

        static void SetEventsEnabled(bool enabled);
        static bool AreEventsEnabled();

        uint32_t AttachVirtual(const VirtualJoystickDesc &desc);
        bool DetachVirtual(uint32_t instanceId);

    private:
        void AddJoystick(uint32_t instanceId);
        void RemoveJoystick(uint32_t instanceId);

        array<Joystick, Reflection::enum_count<JoystickPort>()> joysticks;
    };
} // namespace SF::Engine
