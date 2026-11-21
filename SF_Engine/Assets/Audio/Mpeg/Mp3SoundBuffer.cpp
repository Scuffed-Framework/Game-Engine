#include "Mp3SoundBuffer.hpp"
#include "../AudioInput.hpp"

#define DR_MP3_IMPLEMENTATION
#include <3rdPartyLibs/dr_mp3.hpp>

#include <iostream>
#include <vector>

namespace SF::Engine
{
    void Mp3SoundBuffer::Load(SoundBuffer &soundBuffer, const InputDataVariant &input)
    {
        const auto src = AudioInput::Resolve(input);
        if (!src)
            return;

        drmp3 mp3;
        const drmp3_bool32 opened = src->IsFile()
                                            ? drmp3_init_file(&mp3, src->path.c_str(), nullptr)
                                            : drmp3_init_memory(&mp3, src->memory.data(), src->memory.size(), nullptr);
        if (!opened)
        {
            std::cerr << "Failed to open MP3: " << src->Describe() << std::endl;
            return;
        }

        const uint32_t channels   = mp3.channels;
        const uint32_t sampleRate = mp3.sampleRate;

        if (channels < 1 || channels > 2)
        {
            std::cerr << "Unsupported MP3 channel count (" << channels << ")" << std::endl;
            drmp3_uninit(&mp3);
            return;
        }

        // Counting scans the whole stream, so rewind afterward (replaces the old close + reopen).
        const drmp3_uint64 frameCount = drmp3_get_pcm_frame_count(&mp3);
        if (frameCount == 0 || !drmp3_seek_to_pcm_frame(&mp3, 0))
        {
            drmp3_uninit(&mp3);
            return;
        }

        std::vector<int16_t> pcm(static_cast<size_t>(frameCount) * channels);
        const drmp3_uint64 framesRead = drmp3_read_pcm_frames_s16(&mp3, frameCount, pcm.data());
        drmp3_uninit(&mp3);

        pcm.resize(static_cast<size_t>(framesRead) * channels);
        soundBuffer.SetPCM(
                {.Samples = std::move(pcm), .SampleRate = sampleRate, .ChannelCount = static_cast<uint8_t>(channels)});
    }
} // namespace SF::Engine
