#include "WaveSoundBuffer.hpp"
#include "../AudioInput.hpp"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.hpp"

#include <iostream>
#include <vector>

namespace SF::Engine
{
    void WaveSoundBuffer::Load(SoundBuffer &soundBuffer, const InputDataVariant &input)
    {
        const auto src = AudioInput::Resolve(input);
        if (!src)
            return;

        drwav wav;
        const drwav_bool32 opened = src->IsFile()
                                            ? drwav_init_file(&wav, src->path.c_str(), nullptr)
                                            : drwav_init_memory(&wav, src->memory.data(), src->memory.size(), nullptr);
        if (!opened)
        {
            std::cerr << "Failed to open WAV: " << src->Describe() << std::endl;
            return;
        }

        if (wav.channels < 1 || wav.channels > 2)
        {
            std::cerr << "Unsupported WAV channel count (" << wav.channels << ") in " << src->Describe()
                      << ", only mono/stereo supported" << std::endl;
            drwav_uninit(&wav);
            return;
        }

        const uint32_t channels   = wav.channels;
        const uint32_t sampleRate = wav.sampleRate;

        std::vector<int16_t> pcm(static_cast<size_t>(wav.totalPCMFrameCount) * channels);
        const drwav_uint64 framesRead = drwav_read_pcm_frames_s16(&wav, wav.totalPCMFrameCount, pcm.data());
        drwav_uninit(&wav);

        pcm.resize(static_cast<size_t>(framesRead) * channels);
        soundBuffer.SetPCM(
                {.Samples = std::move(pcm), .SampleRate = sampleRate, .ChannelCount = static_cast<uint8_t>(channels)});
    }

    void WaveSoundBuffer::Write(const SoundBuffer &soundBuffer, const std::filesystem::path &file)
    {
        if (!soundBuffer.HasPCM())
        {
            std::cerr << "Write: no PCM available\n";
            return;
        }
        const auto &pcm = soundBuffer.GetPCM();

        drwav_data_format fmt;
        fmt.container     = drwav_container_riff;
        fmt.format        = DR_WAVE_FORMAT_PCM;
        fmt.channels      = pcm.ChannelCount;
        fmt.sampleRate    = pcm.SampleRate;
        fmt.bitsPerSample = 16;

        drwav wav;
        if (!drwav_init_file_write(&wav, file.string().c_str(), &fmt, nullptr))
        {
            std::cerr << "Failed to open for writing: " << file << std::endl;
            return;
        }
        drwav_write_pcm_frames(&wav, pcm.FrameCount(), pcm.Samples.data());
        drwav_uninit(&wav);
    }
} // namespace SF::Engine
