#pragma once

// Registrars for foreign video containers. Each one only provides an Import hook (foreign -> AV1 MKV);
// loading always goes through the native .mkv path in VideoBuffer::Load.

#include "../VideoBuffer.hpp"

namespace SF::Engine
{
    /// .mp4 / .m4v / .mov: H.264 (openh264), VP9 (libvpx), APV (openapv) -> AV1 transcode; AV1 -> remux.
    class Mp4VideoImporter : public VideoBuffer::Registrar<Mp4VideoImporter>
    {
        inline static const bool Registered = Register(".mp4", ".m4v", ".mov");
        friend class VideoBuffer;

    public:
        /// Always writes outMkv (returns true) or throws.
        static bool Import(const File &src, const File &outMkv);
    };

    /// .webm: VP8/VP9 (libvpx) -> AV1 transcode; AV1 -> remux into .mkv.
    class WebmVideoImporter : public VideoBuffer::Registrar<WebmVideoImporter>
    {
        inline static const bool Registered = Register(".webm");
        friend class VideoBuffer;

    public:
        /// Always writes outMkv (returns true) or throws.
        static bool Import(const File &src, const File &outMkv);
    };

    /// .apv: raw APV bitstream (repeated { au_size BE32, access unit }) -> AV1 transcode via openapv.
    /// The raw format has no frame rate, so 30 fps is assumed.
    class ApvVideoImporter : public VideoBuffer::Registrar<ApvVideoImporter>
    {
        inline static const bool Registered = Register(".apv");
        friend class VideoBuffer;

    public:
        /// Always writes outMkv (returns true) or throws.
        static bool Import(const File &src, const File &outMkv);
    };
} // namespace SF::Engine
