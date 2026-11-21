#include "OggSoundBuffer.hpp"
#include <climits>
#include "../AudioInput.hpp"
#include "../Opus/OpusSoundBuffer.hpp"

extern "C"
{
#include <3rdPartyLibs/stb_vorbis.c>
}

#include <iostream>
#include <vector>

namespace SF::Engine
{
    void OggSoundBuffer::Load(SoundBuffer &soundBuffer, const InputDataVariant &input)
    {
        const auto src = AudioInput::Resolve(input);
        if (!src)
            return;

        // .ogg is also a valid container for Opus; stb_vorbis can't decode that.
        if (AudioInput::LooksLikeOggOpus(*src))
        {
            OpusSoundBuffer::Load(soundBuffer, input);
            return;
        }

        int channels = 0, sampleRate = 0;
        short *output = nullptr;
        int framesPerChannel;

        if (src->IsFile())
        {
            framesPerChannel = stb_vorbis_decode_filename(src->path.c_str(), &channels, &sampleRate, &output);
        } else
        {
            if (src->memory.size() > static_cast<size_t>(INT_MAX))
            {
                std::cerr << "OGG memory block too large for stb_vorbis" << std::endl;
                return;
            }
            framesPerChannel = stb_vorbis_decode_memory(src->memory.data(), static_cast<int>(src->memory.size()),
                                                        &channels, &sampleRate, &output);
        }

        if (framesPerChannel <= 0 || output == nullptr)
        {
            std::cerr << "Failed to decode OGG: " << src->Describe() << std::endl;
            return;
        }

        if (channels < 1 || channels > 2)
        {
            std::cerr << "Unsupported OGG channel count (" << channels << "), only mono/stereo supported" << std::endl;
            free(output);
            return;
        }

        std::vector<int16_t> pcm(output, output + static_cast<size_t>(framesPerChannel) * channels);
        free(output); // stb allocates with malloc; we own a copy now

        soundBuffer.SetPCM({.Samples      = std::move(pcm),
                            .SampleRate   = static_cast<uint32_t>(sampleRate),
                            .ChannelCount = static_cast<uint8_t>(channels)});
    }
} // namespace SF::Engine
