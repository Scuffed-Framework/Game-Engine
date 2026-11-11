#pragma once
#include <3rdPartyLibs/Rocket.hpp>
#include <Engine/Module.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct SDL_AudioStream;
union SDL_Event;

#ifdef Always
    #undef Always
#endif

namespace SF::Engine
{


    enum class MicrophoneRecordMode : uint8_t
    {
        Always,         // always transmitting
        PushToTalk,     // transmitting only while SetPushToTalk(true)
        VoiceActivated, // transmitting while input level is above the threshold (+ hold time)
        Mute            // device paused, nothing captured
    };

    struct MicrophoneDeviceInfo
    {
        std::uint32_t id = 0; // SDL_AudioDeviceID
        std::string name;
        bool isDefault = false;
    };

    class Microphone
    {
        friend class MicrophoneController;

    public:
        static constexpr int kSampleRate  = 48000; // Opus friendly
        static constexpr int kBlockFrames = 480;   // 10 ms blocks for gating / metering

        ~Microphone() { Close(); }

        // Pulls captured audio, runs the gate and emits OnSamples. Called by the controller.
        void Update();

        bool IsOpen() const { return stream != nullptr; }
        std::uint32_t GetDeviceId() const { return deviceId; }
        const std::string &GetDeviceName() const { return deviceName; }

        bool IsFollowingDefault() const { return followDefault; }
        int GetDeviceSampleRate() const { return deviceSampleRate; }
        int GetDeviceChannels() const { return deviceChannels; }

        void SetMode(MicrophoneRecordMode newMode);
        MicrophoneRecordMode GetMode() const { return mode; }

        void SetPushToTalk(bool held) { pushToTalk = held; }
        bool IsPushToTalkHeld() const { return pushToTalk; }

        // Threshold in dBFS (e.g. -40). Used by VoiceActivated.
        void SetVoiceThreshold(float dB) { voiceThresholdDb = dB; }
        float GetVoiceThreshold() const { return voiceThresholdDb; }

        // How long the gate stays open after the level drops below the threshold.
        void SetVoiceHoldTime(float seconds) { voiceHoldSeconds = seconds < 0.f ? 0.f : seconds; }
        float GetVoiceHoldTime() const { return voiceHoldSeconds; }

        // True while audio is being passed on to OnSamples.
        bool IsTransmitting() const { return transmitting; }

        // Software gain applied by SDL (1 = unity, may exceed 1).
        void SetGain(float newGain);
        float GetGain() const { return gain; }

        // Smoothed RMS in [0, 1], measured before gating (works for UI meters in every mode but Mute).
        float GetLevel() const { return level; }
        float GetLevelDb() const;
        // Peak of the most recent block in [0, 1].
        float GetPeak() const { return peak; }

        // Gated mono float PCM, delivered on the thread that calls Update().
        rocket::signal<void(std::span<const float>)> &OnSamples() { return onSamples; }
        rocket::signal<void(bool)> &OnTransmitChanged() { return onTransmitChanged; }
        rocket::signal<void()> &OnDeviceChanged() { return onDeviceChanged; }

    private:
        // deviceId 0 == follow the system default recording device.
        bool Open(uint32_t id);
        void Close();
        void Pause(bool paused) const;
        void QueryFormat();
        void SetTransmitting(bool value);
        void ProcessBlock(const float *block, size_t frames);

        SDL_AudioStream *stream = nullptr;
        std::uint32_t deviceId  = 0;
        std::string deviceName;
        bool followDefault   = true;
        int deviceSampleRate = 0;
        int deviceChannels   = 0;

        MicrophoneRecordMode mode = MicrophoneRecordMode::Always;
        bool pushToTalk           = false;
        float voiceThresholdDb    = -40.0f;
        float voiceHoldSeconds    = 0.3f;
        float holdRemaining       = 0.0f;

        float gain        = 1.0f;
        float level       = 0.0f;
        float peak        = 0.0f;
        bool transmitting = false;

        std::vector<float> readBuffer;
        std::vector<float> pending; // carries a partial block between updates
        std::vector<float> preRoll; // last block, emitted when the gate opens so onsets aren't clipped

        rocket::signal<void(std::span<const float>)> onSamples;
        rocket::signal<void(bool)> onTransmitChanged;
        rocket::signal<void()> onDeviceChanged;
    };

    class MicrophoneController : public ModuleRegistrar<MicrophoneController>
    {
        REGISTER_MODULE(MicrophoneController, ModuleStage::Pre);

    public:
        bool Initialize() override;
        void Update() override;
        ~MicrophoneController() override;

        // Forward every SDL_Event from the engine's event loop. Returns true if consumed.
        bool HandleEvent(const SDL_Event &event);

        Microphone &GetMicrophone() { return microphone; }
        const Microphone &GetMicrophone() const { return microphone; }

        // Currently available capture devices.
        std::vector<MicrophoneDeviceInfo> GetDevices() const;

        // Switch capture device. id 0 follows the system default.
        bool SelectDevice(uint32_t id);

        rocket::signal<void(const MicrophoneDeviceInfo &, bool)> &OnDeviceListChanged() { return onDeviceListChanged; }

    private:
        Microphone microphone;
        rocket::signal<void(const MicrophoneDeviceInfo &, bool)> onDeviceListChanged; // (device, added)
    };
} // namespace SF::Engine
