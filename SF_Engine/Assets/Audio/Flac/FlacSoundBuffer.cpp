#include "FlacSoundBuffer.hpp"
#include "../AudioInput.hpp"

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.hpp"

#include <iostream>
#include <vector>

namespace SF::Engine
{
    void FlacSoundBuffer::Load(SoundBuffer &soundBuffer, const InputDataVariant &input)
    {
        const auto src = AudioInput::Resolve(input);
        if (!src)
            return;

        drflac *flac = src->IsFile() ? drflac_open_file(src->path.c_str(), nullptr)
                                     : drflac_open_memory(src->memory.data(), src->memory.size(), nullptr);
        if (!flac)
        {
            std::cerr << "Failed to open FLAC: " << src->Describe() << std::endl;
            return;
        }

        const uint32_t channels   = flac->channels;
        const uint32_t sampleRate = flac->sampleRate;

        if (channels < 1 || channels > 2)
        {
            std::cerr << "Unsupported FLAC channel count (" << channels << ")" << std::endl;
            drflac_close(flac);
            return;
        }

        std::vector<int16_t> pcm(static_cast<size_t>(flac->totalPCMFrameCount) * channels);
        const drflac_uint64 framesRead = drflac_read_pcm_frames_s16(flac, flac->totalPCMFrameCount, pcm.data());
        drflac_close(flac);

        pcm.resize(static_cast<size_t>(framesRead) * channels);
        soundBuffer.SetPCM(
                {.Samples = std::move(pcm), .SampleRate = sampleRate, .ChannelCount = static_cast<uint8_t>(channels)});
    }
} // namespace SF::Engine
