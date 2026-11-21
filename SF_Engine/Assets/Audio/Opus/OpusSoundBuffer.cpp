#include "OpusSoundBuffer.hpp"
#include "../AudioInput.hpp"

#include <opus/opusfile.h>

#include <iostream>
#include <memory>
#include <vector>

namespace SF::Engine
{
    namespace
    {
        struct OpusFileDeleter
        {
            void operator()(OggOpusFile *f) const { op_free(f); }
        };
    } // namespace

    void OpusSoundBuffer::Load(SoundBuffer &soundBuffer, const InputDataVariant &input)
    {
        const auto src = AudioInput::Resolve(input);
        if (!src)
            return;

        int err = 0;
        const std::unique_ptr<OggOpusFile, OpusFileDeleter> opus(
                src->IsFile() ? op_open_file(src->path.c_str(), &err)
                              : op_open_memory(src->memory.data(), src->memory.size(), &err));
        if (!opus)
        {
            std::cerr << "Failed to open Opus: " << src->Describe() << " (opusfile error " << err << ")" << std::endl;
            return;
        }

        const int channels = op_channel_count(opus.get(), 0);
        // OpenAL only spatializes mono sources, so keep mono as mono. Anything else is
        // downmixed to stereo by op_read_stereo (core AL has no >2 channel formats).
        const bool mono       = (channels == 1);
        const int outChannels = mono ? 1 : 2;

        std::vector<opus_int16> pcm;
        if (const ogg_int64_t totalFrames = op_pcm_total(opus.get(), -1); totalFrames > 0)
            pcm.reserve(static_cast<size_t>(totalFrames) * outChannels);

        // op_read* return frames per channel, and want the buffer size in total int16 values.
        // 5760 frames is 120 ms, the largest Opus packet.
        std::vector<opus_int16> chunk(5760 * outChannels);
        while (true)
        {
            const int frames = mono ? op_read(opus.get(), chunk.data(), static_cast<int>(chunk.size()), nullptr)
                                    : op_read_stereo(opus.get(), chunk.data(), static_cast<int>(chunk.size()));
            if (frames == 0)
                break; // EOF
            if (frames < 0)
            {
                // OP_HOLE means a gap in the data, which is recoverable; anything else is fatal.
                if (frames == OP_HOLE)
                    continue;
                std::cerr << "Opus decode error " << frames << " in " << src->Describe() << std::endl;
                return;
            }
            pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + static_cast<long>(frames) * outChannels);
        }

        if (pcm.empty())
        {
            std::cerr << "Opus decoded to zero samples: " << src->Describe() << std::endl;
            return;
        }

        soundBuffer.SetPCM(
                {.Samples = std::move(pcm), .SampleRate = 48000, .ChannelCount = static_cast<uint8_t>(outChannels)});
    }
} // namespace SF::Engine
