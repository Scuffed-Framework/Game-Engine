#pragma once

// Shared plumbing for foreign video -> AV1 MKV import:
//   container demuxer -> IPacketSource -> decoder (IRawFrameSource) -> Av1MkvEncoder -> .av1.mkv

#include "../Matroska/MkvVideoBuffer.hpp"
#include "../VideoBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace SF::Engine::VideoImport
{
    using namespace std;

    /// One compressed access unit exactly as the container stores it.
    struct CompressedPacket
    {
        vector<uint8_t> Data;
        int64_t Pts = 0; // container ticks, informational (decoders output in presentation order)
        bool Key    = false;
    };

    class IPacketSource
    {
    public:
        virtual ~IPacketSource()                   = default;
        virtual bool Next(CompressedPacket &out) = 0; // false = end of stream
    };

    struct Rational
    {
        uint32_t Num = 30;
        uint32_t Den = 1;
    };

    /// fps = num/den. Snaps to the usual broadcast rates (24, 25, 30, 60, 23.976, 29.97 ...) when within 0.2%.
    /// Falls back to 30/1 for garbage. Import flattens variable frame rate sources to this constant rate.
    inline Rational FrameRateFrom(uint64_t num, uint64_t den)
    {
        if (num == 0 || den == 0)
            return {30, 1};

        const double fps = static_cast<double>(num) / static_cast<double>(den);
        if (!(fps >= 1.0 && fps <= 1000.0))
            return {30, 1};

        static constexpr Rational kStandard[] = {{24000, 1001}, {24, 1},  {25, 1},        {30000, 1001}, {30, 1},
                                                 {48, 1},       {50, 1},  {60000, 1001},  {60, 1},       {120000, 1001},
                                                 {120, 1}};
        for (const auto &s: kStandard)
        {
            const double ref = static_cast<double>(s.Num) / static_cast<double>(s.Den);
            if (abs(fps - ref) / ref < 0.002)
                return s;
        }

        const uint64_t g = gcd(num, den);
        num /= g;
        den /= g;
        if (num <= UINT32_MAX && den <= UINT32_MAX)
            return {static_cast<uint32_t>(num), static_cast<uint32_t>(den)};
        return {static_cast<uint32_t>(llround(fps * 1000.0)), 1000};
    }

    /// Decoder output, any layout the supported decoders can produce.
    struct DecodedPlanes
    {
        const uint8_t *Plane[3] = {};
        uint32_t Stride[3]      = {}; // bytes
        uint32_t Width          = 0;
        uint32_t Height         = 0;
        uint8_t ChromaShiftX    = 1; // 1 = half width chroma (4:2:0 / 4:2:2), 0 = full width (4:4:4 / 4:4:0)
        uint8_t ChromaShiftY    = 1; // 1 = half height chroma (4:2:0 / 4:4:0), 0 = full height (4:2:2 / 4:4:4)
        uint8_t BitDepth        = 8;
        bool Wide               = false; // samples stored as 16-bit little endian
    };

    /// Anything -> planar 4:2:0, 8-bit or 10-bit (16-bit LE container), even dimensions. This is what
    /// Av1MkvEncoder expects. 4:2:0 at 8/10 bit is passed through without a copy (odd dimensions are cropped by
    /// reporting a smaller size over the same buffer).
    class FrameConverter
    {
    public:
        static uint8_t OutBitDepth(const DecodedPlanes &in) { return in.BitDepth > 8 ? 10 : 8; }
        static uint32_t OutWidth(const DecodedPlanes &in) { return in.Width & ~1u; }
        static uint32_t OutHeight(const DecodedPlanes &in) { return in.Height & ~1u; }

        void Convert(const DecodedPlanes &in, RawVideoFrame &out)
        {
            const uint32_t w    = OutWidth(in);
            const uint32_t h    = OutHeight(in);
            const uint8_t depth = OutBitDepth(in);

            out.Width    = w;
            out.Height   = h;
            out.BitDepth = depth;

            const bool is420 = in.ChromaShiftX == 1 && in.ChromaShiftY == 1;
            const bool exact = (in.BitDepth == 8 && !in.Wide) || (in.BitDepth == 10 && in.Wide);
            if (is420 && exact && in.Plane[1] && in.Plane[2])
            {
                for (int i = 0; i < 3; ++i)
                {
                    out.Plane[i]  = in.Plane[i];
                    out.Stride[i] = in.Stride[i];
                }
                return;
            }

            const uint32_t bps = depth > 8 ? 2 : 1;
            const uint32_t cw  = w / 2;
            const uint32_t ch  = h / 2;
            buf_[0].resize(static_cast<size_t>(w) * h * bps);
            buf_[1].resize(static_cast<size_t>(cw) * ch * bps);
            buf_[2].resize(static_cast<size_t>(cw) * ch * bps);

            const int shift = static_cast<int>(in.BitDepth) - static_cast<int>(depth); // >0: drop bits, <0: add bits

            auto read = [&](const uint8_t *base, const uint32_t stride, const uint32_t x, const uint32_t y) -> uint32_t
            {
                if (!in.Wide)
                    return base[static_cast<size_t>(y) * stride + x];
                uint16_t v;
                memcpy(&v, base + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 2, 2);
                return v;
            };
            auto adjust = [&](const uint32_t v) -> uint32_t
            { return shift > 0 ? (v >> shift) : (shift < 0 ? (v << -shift) : v); };
            auto write = [&](vector<uint8_t> &dst, const size_t index, const uint32_t v)
            {
                if (bps == 1)
                    dst[index] = static_cast<uint8_t>(v);
                else
                {
                    const auto s = static_cast<uint16_t>(v);
                    memcpy(dst.data() + index * 2, &s, 2);
                }
            };

            // Luma
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x)
                    write(buf_[0], static_cast<size_t>(y) * w + x, adjust(read(in.Plane[0], in.Stride[0], x, y)));

            // Chroma -> 4:2:0 by box-averaging whatever the source has
            const uint32_t rx = in.ChromaShiftX == 1 ? 1 : 2; // source samples per output sample, horizontally
            const uint32_t ry = in.ChromaShiftY == 1 ? 1 : 2;
            const uint32_t neutral = 1u << (depth - 1);
            for (int c = 1; c < 3; ++c)
            {
                for (uint32_t y = 0; y < ch; ++y)
                    for (uint32_t x = 0; x < cw; ++x)
                    {
                        uint32_t v = neutral;
                        if (in.Plane[c])
                        {
                            uint32_t sum = 0;
                            for (uint32_t j = 0; j < ry; ++j)
                                for (uint32_t i = 0; i < rx; ++i)
                                    sum += read(in.Plane[c], in.Stride[c], x * rx + i, y * ry + j);
                            v = adjust((sum + (rx * ry) / 2) / (rx * ry));
                        }
                        write(buf_[c], static_cast<size_t>(y) * cw + x, v);
                    }
            }

            out.Plane[0]  = buf_[0].data();
            out.Plane[1]  = buf_[1].data();
            out.Plane[2]  = buf_[2].data();
            out.Stride[0] = w * bps;
            out.Stride[1] = cw * bps;
            out.Stride[2] = cw * bps;
        }

    private:
        vector<uint8_t> buf_[3];
    };

    /// Runs `writeTo(tmpFile)` and moves the result into place, so a crash or exception mid-transcode never leaves
    /// a half-written .av1.mkv that the mtime cache check in VideoBuffer would accept as fresh.
    template<typename Fn>
    void WriteAtomic(const File &out, Fn &&writeTo)
    {
        const std::filesystem::path finalPath = out.GetFullPath();
        std::filesystem::path tmpPath         = finalPath;
        tmpPath += ".tmp";

        std::error_code ec;
        try
        {
            writeTo(File(tmpPath));
        } catch (...)
        {
            std::filesystem::remove(tmpPath, ec);
            throw;
        }

        std::filesystem::rename(tmpPath, finalPath, ec);
        if (ec)
        {
            // Some platforms refuse to rename over an existing file.
            std::filesystem::remove(finalPath, ec);
            ec.clear();
            std::filesystem::rename(tmpPath, finalPath, ec);
            if (ec)
                throw runtime_error("Video import: cannot move output into place: " + ec.message());
        }
    }

    inline void EncodeToAv1Mkv(IRawFrameSource &src, const File &outMkv, const Av1EncodeSettings &settings = {})
    {
        WriteAtomic(outMkv, [&](const File &tmp) { Av1MkvEncoder::Encode(src, tmp, settings); });
    }
} // namespace SF::Engine::VideoImport
