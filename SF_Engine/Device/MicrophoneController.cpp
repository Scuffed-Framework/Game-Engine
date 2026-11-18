#include "MicrophoneController.hpp"
#include <Engine/Log/Log.hpp>
#include <SDL3/SDL.h>
#include <UtilityClasses/StlExtensions.hpp>

#include <algorithm>
#include <cmath>

namespace SF::Engine
{
    bool Microphone::Open(const uint32_t id)
    {
        Close();

        SDL_AudioSpec spec{};
        spec.format   = SDL_AUDIO_F32;
        spec.channels = 1;
        spec.freq     = kSampleRate;

        const SDL_AudioDeviceID target = id == 0 ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING : id;

        stream = SDL_OpenAudioDeviceStream(target, &spec, nullptr, nullptr);
        if (!stream)
        {
            Log::Error("Failed to open microphone: {}", SDL_GetError());
            return false;
        }

        followDefault = id == 0;
        deviceId      = SDL_GetAudioStreamDevice(stream);
        deviceName    = SafeStr(SDL_GetAudioDeviceName(deviceId));
        QueryFormat();

        SDL_SetAudioStreamGain(stream, gain);
        Pause(mode == MicrophoneRecordMode::Mute);

        Log::Out("Microphone opened: '{}' ({} Hz, {} ch)\n", deviceName, deviceSampleRate, deviceChannels);
        onDeviceChanged();
        return true;
    }

    void Microphone::Close()
    {
        if (stream)
        {
            SDL_DestroyAudioStream(stream); // also closes the device it opened
            stream = nullptr;
        }
        deviceId = 0;
        deviceName.clear();
        pending.clear();
        preRoll.clear();
        level = peak = 0.0f;
        SetTransmitting(false);
    }

    void Microphone::Pause(const bool paused) const
    {
        if (!stream)
            return;
        if (paused)
            SDL_PauseAudioStreamDevice(stream);
        else
            SDL_ResumeAudioStreamDevice(stream);
    }

    void Microphone::QueryFormat()
    {
        SDL_AudioSpec spec{};
        int frames = 0;
        if (SDL_GetAudioDeviceFormat(deviceId, &spec, &frames))
        {
            deviceSampleRate = spec.freq;
            deviceChannels   = spec.channels;
        }
    }

    void Microphone::SetMode(const MicrophoneRecordMode newMode)
    {
        if (newMode == mode)
            return;

        mode          = newMode;
        holdRemaining = 0.0f;

        // Mute really stops capturing, so nothing is recorded behind the user's back.
        Pause(mode == MicrophoneRecordMode::Mute);
        if (mode == MicrophoneRecordMode::Mute)
        {
            SDL_ClearAudioStream(stream);
            pending.clear();
            preRoll.clear();
            level = peak = 0.0f;
            SetTransmitting(false);
        }
    }

    void Microphone::SetGain(const float newGain)
    {
        gain = std::max(newGain, 0.0f);
        if (stream)
            SDL_SetAudioStreamGain(stream, gain);
    }

    float Microphone::GetLevelDb() const { return 20.0f * std::log10(std::max(level, 1e-6f)); }

    void Microphone::SetTransmitting(const bool value)
    {
        if (transmitting == value)
            return;
        transmitting = value;
        onTransmitChanged(value);
    }

    void Microphone::Update()
    {
        if (!stream || mode == MicrophoneRecordMode::Mute)
            return;

        const int availableBytes = SDL_GetAudioStreamAvailable(stream);
        if (availableBytes <= 0)
            return;

        const size_t frames = static_cast<size_t>(availableBytes) / sizeof(float);
        readBuffer.resize(frames);

        const int got = SDL_GetAudioStreamData(stream, readBuffer.data(), static_cast<int>(frames * sizeof(float)));
        if (got <= 0) // returns -1 if failed
            return;

        pending.insert(pending.end(), readBuffer.begin(), readBuffer.begin() + size_t(got) / sizeof(float));

        // Process in fixed 10 ms blocks.
        size_t offset = 0;
        while (pending.size() - offset >= static_cast<size_t>(kBlockFrames))
        {
            ProcessBlock(pending.data() + offset, kBlockFrames);
            offset += kBlockFrames;
        }
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    void Microphone::ProcessBlock(const float *block, const size_t frames)
    {
        float sumSq     = 0.0f;
        float blockPeak = 0.0f;
        for (size_t i = 0; i < frames; i++)
        {
            sumSq += block[i] * block[i];
            blockPeak = std::max(blockPeak, std::fabs(block[i]));
        }
        const float rms = std::sqrt(sumSq / static_cast<float>(frames));
        peak            = blockPeak;
        // Fast attack, slow release so UI meters are readable.
        level = rms > level ? rms : level + (rms - level) * 0.15f;

        const float blockSeconds   = static_cast<float>(frames) / static_cast<float>(kSampleRate);
        const bool wasTransmitting = transmitting;
        bool open                  = false;

        switch (mode)
        {
            case MicrophoneRecordMode::Always:
                open = true;
                break;
            case MicrophoneRecordMode::PushToTalk:
                open = pushToTalk;
                break;
            case MicrophoneRecordMode::VoiceActivated:
            {
                const float db = 20.0f * std::log10(std::max(rms, 1e-6f));
                if (db >= voiceThresholdDb)
                    holdRemaining = voiceHoldSeconds;
                else
                    holdRemaining = std::max(holdRemaining - blockSeconds, 0.0f);
                open = holdRemaining > 0.0f || db >= voiceThresholdDb;
                break;
            }
            case MicrophoneRecordMode::Mute:
                open = false;
                break;
        }

        if (open)
        {
            SetTransmitting(true);
            // Gate just opened: emit the previous block too so the first syllable isn't clipped.
            if (!wasTransmitting && mode == MicrophoneRecordMode::VoiceActivated && !preRoll.empty())
                onSamples(std::span<const float>(preRoll.data(), preRoll.size()));
            onSamples(std::span<const float>(block, frames));
        } else
        {
            SetTransmitting(false);
        }

        preRoll.assign(block, block + frames);
    }

    bool MicrophoneController::Initialize()
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            Log::Out("Failed to init SDL audio subsystem: ", SDL_GetError(), '\n');
            return false;
        }

        // Opening may trigger the OS permission prompt (macOS, Android, iOS).
        // A failure here is not fatal: the device list and hot-plug still work.
        microphone.Open(0);
        return true;
    }

    MicrophoneController::~MicrophoneController()
    {
        microphone.Close();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    void MicrophoneController::Update() { microphone.Update(); }

    std::vector<MicrophoneDeviceInfo> MicrophoneController::GetDevices() const
    {
        std::vector<MicrophoneDeviceInfo> result;

        int count              = 0;
        SDL_AudioDeviceID *ids = SDL_GetAudioRecordingDevices(&count);
        if (!ids)
            return result;

        for (int i = 0; i < count; i++)
        {
            MicrophoneDeviceInfo info;
            info.id        = ids[i];
            info.name      = SafeStr(SDL_GetAudioDeviceName(ids[i]));
            info.isDefault = false; // SDL3 has no per-device default flag; use SelectDevice(0) to follow the default
            result.push_back(std::move(info));
        }
        SDL_free(ids);
        return result;
    }

    bool MicrophoneController::SelectDevice(const uint32_t id) { return microphone.Open(id); }

    bool MicrophoneController::HandleEvent(const SDL_Event &event)
    {
        switch (event.type)
        {
            case SDL_EVENT_AUDIO_DEVICE_ADDED:
            case SDL_EVENT_AUDIO_DEVICE_REMOVED:
            {
                if (!event.adevice.recording)
                    return false; // playback device, not ours

                const bool added = event.type == SDL_EVENT_AUDIO_DEVICE_ADDED;
                MicrophoneDeviceInfo info;
                info.id   = event.adevice.which;
                info.name = added ? SafeStr(SDL_GetAudioDeviceName(info.id)) : std::string();
                if (added)
                    Log::Out("Microphone device added: '{}'\n", info.name);
                else
                    Log::Out("Microphone device removed (id {})\n", info.id);

                onDeviceListChanged(info, added);

                // Our explicitly chosen device vanished: fall back to the system default.
                if (!added && !microphone.IsFollowingDefault() && microphone.GetDeviceId() == info.id)
                    microphone.Open(0);
                return true;
            }

            case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:
                if (!event.adevice.recording)
                    return false;
                if (event.adevice.which == microphone.GetDeviceId())
                    microphone.QueryFormat(); // SDL converts to our stream format automatically
                return true;

            default:
                return false;
        }
    }
} // namespace SF::Engine
