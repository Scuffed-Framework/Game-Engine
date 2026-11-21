#pragma once
#include "../SoundBuffer.hpp"

namespace SF::Engine
{
    class OpusSoundBuffer : public SoundBuffer::Registrar<OpusSoundBuffer>
    {
        // .ogg is owned by OggSoundBuffer (it forwards here when the stream is Opus) and .mka by MkaSoundBuffer.
        inline static const bool Registered = Register(".opus");
        friend class SoundBuffer;

    public:
        static void Load(SoundBuffer &soundBuffer, const InputDataVariant &input);
    };
} // namespace SF::Engine