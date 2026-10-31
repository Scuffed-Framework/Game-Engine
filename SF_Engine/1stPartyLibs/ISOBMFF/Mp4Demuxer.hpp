#pragma once

// Minimal ISO-BMFF (MP4 / MOV / M4V) demuxer. Read-only, video-oriented.
//
//  - Builds a flat per-track sample table from moov/trak/mdia/minf/stbl (stts, ctts, stsc, stsz, stco/co64, stss).
//  - Sample payloads are read on demand from the stream, so mdat is never loaded into memory.
//  - Only tracks whose handler is 'vide' get a sample table. Everything else is listed but left empty.
//  - NOT supported (throws / ignored): fragmented MP4 (moof), stz2, edit lists (edts is ignored),
//    external data references (dref), encrypted tracks.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <istream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace SF::ISOBMFF
{
    using namespace std;

    constexpr uint32_t FourCC(const char (&s)[5])
    {
        return (static_cast<uint32_t>(static_cast<uint8_t>(s[0])) << 24) |
               (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 16) |
               (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 8) |
               static_cast<uint32_t>(static_cast<uint8_t>(s[3]));
    }

    inline string FourCCToString(const uint32_t v)
    {
        string s(4, ' ');
        for (int i = 0; i < 4; ++i)
            s[static_cast<size_t>(i)] = static_cast<char>((v >> (24 - 8 * i)) & 0xFF);
        return s;
    }

    struct Mp4Sample
    {
        uint64_t Offset = 0; // absolute file offset
        uint32_t Size   = 0;
        int64_t Dts     = 0; // media timescale ticks
        int64_t Pts     = 0; // Dts + composition offset (ctts)
        bool Sync       = true;
    };

    struct Mp4Track
    {
        uint32_t Id         = 0;
        uint32_t Handler    = 0; // 'vide', 'soun', ...
        uint32_t Codec      = 0; // first sample entry fourcc: 'avc1', 'hvc1', 'vp09', 'av01', ...
        uint32_t Timescale  = 0;
        uint64_t TotalDelta = 0; // sum of all stts deltas (media ticks)
        uint16_t Width      = 0;
        uint16_t Height     = 0;
        uint32_t ConfigType = 0;   // 'avcC', 'hvcC', 'av1C', 'vpcC', 'apvC' (first one found in the sample entry)
        vector<uint8_t> Config;    // payload of that box (no box header)
        vector<Mp4Sample> Samples; // decode order
    };

    class Mp4Demuxer
    {
    public:
        explicit Mp4Demuxer(istream &in) : in_(in) {}

        /// false = not an ISO-BMFF stream / no moov. Throws on corrupt or unsupported (fragmented) files.
        bool Initialize()
        {
            tracks_.clear();
            fragmented_ = false;

            in_.clear();
            in_.seekg(0, ios::end);
            const auto endPos = in_.tellg();
            if (endPos < 0)
                return false;
            fileSize_ = static_cast<uint64_t>(endPos);

            bool foundMoov = false;
            ForEachBox(0, fileSize_,
                       [&](const Box &b)
                       {
                           if (b.Type == FourCC("moov"))
                           {
                               foundMoov = true;
                               ParseMoov(b);
                           }
                       });

            if (!foundMoov)
                return false;
            if (fragmented_)
                throw runtime_error("MP4: fragmented MP4 (moof) isn't supported yet");
            return !tracks_.empty();
        }

        [[nodiscard]] const vector<Mp4Track> &Tracks() const { return tracks_; }

        /// Reads one sample's payload. false = truncated file.
        bool ReadSample(const Mp4Sample &s, vector<uint8_t> &out) const
        {
            out.resize(s.Size);
            in_.clear();
            in_.seekg(static_cast<streamoff>(s.Offset));
            in_.read(reinterpret_cast<char *>(out.data()), static_cast<streamsize>(s.Size));
            return static_cast<bool>(in_);
        }

    private:
        struct Box
        {
            uint32_t Type;
            uint64_t Start;   // offset of the box header
            uint64_t Size;    // total size including header
            uint64_t Payload; // offset of first payload byte
            [[nodiscard]] uint64_t End() const { return Start + Size; }
        };

        struct Cursor
        {
            const uint8_t *p;
            size_t n;
            size_t pos = 0;

            void Need(const size_t k) const
            {
                if (pos + k > n)
                    throw runtime_error("MP4: truncated box");
            }
            uint8_t U8()
            {
                Need(1);
                return p[pos++];
            }
            uint16_t U16()
            {
                Need(2);
                const auto v = static_cast<uint16_t>((p[pos] << 8) | p[pos + 1]);
                pos += 2;
                return v;
            }
            uint32_t U32()
            {
                Need(4);
                const uint32_t v = (static_cast<uint32_t>(p[pos]) << 24) | (static_cast<uint32_t>(p[pos + 1]) << 16) |
                                   (static_cast<uint32_t>(p[pos + 2]) << 8) | static_cast<uint32_t>(p[pos + 3]);
                pos += 4;
                return v;
            }
            uint64_t U64()
            {
                const uint64_t hi = U32();
                const uint64_t lo = U32();
                return (hi << 32) | lo;
            }
            void Skip(const size_t k)
            {
                Need(k);
                pos += k;
            }
            [[nodiscard]] size_t Left() const { return n - pos; }
            // Validates an entry count read from the file against the bytes actually left.
            void CheckCount(const uint64_t count, const size_t entrySize) const
            {
                if (count > Left() / entrySize)
                    throw runtime_error("MP4: table entry count exceeds box size");
            }
        };

        istream &in_;
        uint64_t fileSize_ = 0;
        bool fragmented_   = false;
        vector<Mp4Track> tracks_;

        template<typename Fn>
        void ForEachBox(const uint64_t begin, const uint64_t end, Fn &&fn)
        {
            uint64_t pos = begin;
            while (pos + 8 <= end)
            {
                in_.clear();
                in_.seekg(static_cast<streamoff>(pos));
                uint8_t h[16] = {};
                in_.read(reinterpret_cast<char *>(h), 8);
                if (!in_)
                    break;

                uint64_t size = (static_cast<uint64_t>(h[0]) << 24) | (static_cast<uint64_t>(h[1]) << 16) |
                                (static_cast<uint64_t>(h[2]) << 8) | h[3];
                const uint32_t type = (static_cast<uint32_t>(h[4]) << 24) | (static_cast<uint32_t>(h[5]) << 16) |
                                      (static_cast<uint32_t>(h[6]) << 8) | h[7];
                uint64_t headerSize = 8;

                if (size == 1) // 64-bit largesize
                {
                    in_.read(reinterpret_cast<char *>(h + 8), 8);
                    if (!in_)
                        break;
                    size = 0;
                    for (int i = 8; i < 16; ++i)
                        size = (size << 8) | h[i];
                    headerSize = 16;
                } else if (size == 0) // extends to end of enclosing box / file
                    size = end - pos;

                if (size < headerSize)
                    break;
                if (pos + size > end)
                    size = end - pos; // tolerate a truncated final box

                fn(Box{.Type = type, .Start = pos, .Size = size, .Payload = pos + headerSize});
                pos += size;
            }
        }

        vector<uint8_t> Payload(const Box &b) const
        {
            const uint64_t size = b.End() - b.Payload;
            if (size > (1ull << 30))
                throw runtime_error("MP4: box too large");
            vector<uint8_t> data(static_cast<size_t>(size));
            if (!data.empty())
            {
                in_.clear();
                in_.seekg(static_cast<streamoff>(b.Payload));
                in_.read(reinterpret_cast<char *>(data.data()), static_cast<streamsize>(data.size()));
                if (!in_)
                    throw runtime_error("MP4: truncated file");
            }
            return data;
        }

        void ParseMoov(const Box &moov)
        {
            ForEachBox(moov.Payload, moov.End(),
                       [&](const Box &b)
                       {
                           if (b.Type == FourCC("trak"))
                               ParseTrak(b);
                           else if (b.Type == FourCC("mvex"))
                               fragmented_ = true;
                       });
        }

        void ParseTrak(const Box &trak)
        {
            Mp4Track t;
            ForEachBox(trak.Payload, trak.End(),
                       [&](const Box &b)
                       {
                           if (b.Type == FourCC("tkhd"))
                           {
                               const auto d = Payload(b);
                               Cursor c{.p = d.data(), .n = d.size()};
                               const uint8_t version = c.U8();
                               c.Skip(3);
                               c.Skip(version == 1 ? 16 : 8); // creation + modification time
                               t.Id = c.U32();
                           } else if (b.Type == FourCC("mdia"))
                               ParseMdia(b, t);
                       });
            tracks_.push_back(std::move(t));
        }

        void ParseMdia(const Box &mdia, Mp4Track &t)
        {
            ForEachBox(mdia.Payload, mdia.End(),
                       [&](const Box &b)
                       {
                           if (b.Type == FourCC("mdhd"))
                           {
                               const auto d = Payload(b);
                               Cursor c{.p = d.data(), .n = d.size()};
                               const uint8_t version = c.U8();
                               c.Skip(3);
                               c.Skip(version == 1 ? 16 : 8);
                               t.Timescale = c.U32();
                           } else if (b.Type == FourCC("hdlr"))
                           {
                               const auto d = Payload(b);
                               Cursor c{.p = d.data(), .n = d.size()};
                               c.Skip(8); // version/flags + pre_defined
                               t.Handler = c.U32();
                           } else if (b.Type == FourCC("minf"))
                           {
                               ForEachBox(b.Payload, b.End(),
                                          [&](const Box &m)
                                          {
                                              if (m.Type == FourCC("stbl"))
                                                  ParseStbl(m, t);
                                          });
                           }
                       });
        }

        static bool IsConfigBox(const uint32_t t)
        {
            return t == FourCC("avcC") || t == FourCC("hvcC") || t == FourCC("av1C") || t == FourCC("vpcC") ||
                   t == FourCC("apvC");
        }

        static void ParseVideoStsd(const vector<uint8_t> &d, Mp4Track &t)
        {
            Cursor c{.p = d.data(), .n = d.size()};
            c.Skip(4); // version/flags
            if (c.U32() == 0)
                return; // entry_count

            const size_t entryStart  = c.pos;
            const uint32_t entrySize = c.U32();
            t.Codec                  = c.U32();
            const size_t entryEnd    = min(d.size(), entryStart + static_cast<size_t>(entrySize));

            c.Skip(6 + 2); // reserved + data_reference_index
            c.Skip(16);    // pre_defined, reserved, pre_defined[3]
            t.Width  = c.U16();
            t.Height = c.U16();
            c.Skip(4 + 4 + 4 + 2 + 32 + 2 +
                   2); // h/v resolution, reserved, frame_count, compressorname, depth, pre_defined

            while (c.pos + 8 <= entryEnd)
            {
                const size_t boxStart = c.pos;
                const uint32_t bsize  = c.U32();
                const uint32_t btype  = c.U32();
                if (bsize < 8 || boxStart + bsize > entryEnd)
                    break;
                const size_t payload = bsize - 8;
                if (IsConfigBox(btype))
                {
                    t.ConfigType = btype;
                    t.Config.assign(d.begin() + static_cast<ptrdiff_t>(c.pos),
                                    d.begin() + static_cast<ptrdiff_t>(c.pos + payload));
                    return;
                }
                c.Skip(payload);
            }
        }

        void ParseStbl(const Box &stbl, Mp4Track &t)
        {
            if (t.Handler != FourCC("vide"))
                return;

            struct StscRun
            {
                uint32_t FirstChunk;
                uint32_t PerChunk;
            };

            vector<pair<uint32_t, uint32_t>> stts; // count, delta
            vector<pair<uint32_t, int32_t>> ctts;  // count, offset
            vector<StscRun> stsc;
            vector<uint32_t> sizes;
            vector<uint64_t> chunkOffsets;
            vector<uint32_t> syncSamples;
            uint32_t fixedSize   = 0;
            uint32_t sampleCount = 0;
            bool hasStss         = false;

            ForEachBox(stbl.Payload, stbl.End(),
                       [&](const Box &b)
                       {
                           if (const uint32_t type = b.Type; type == FourCC("stsd"))
                           {
                               ParseVideoStsd(Payload(b), t);
                           } else if (type == FourCC("stts"))
                           {
                               const auto d = Payload(b);
                               Cursor c{.p = d.data(), .n = d.size()};
                               c.Skip(4);
                               const uint32_t n = c.U32();
                               c.CheckCount(n, 8);
                               stts.reserve(n);
                               for (uint32_t i = 0; i < n; ++i)
                               {
                                   const uint32_t count = c.U32();
                                   const uint32_t delta = c.U32();
                                   stts.emplace_back(count, delta);
                               }
                           } else if (type == FourCC("ctts"))
                           {
                               const auto d = Payload(b);
                               Cursor c{.p = d.data(), .n = d.size()};
                               c.Skip(4);
                               const uint32_t n = c.U32();
                               c.CheckCount(n, 8);
                               ctts.reserve(n);
                               for (uint32_t i = 0; i < n; ++i)
                               {
                                   const uint32_t count = c.U32();
                                   const auto offset =
                                           static_cast<int32_t>(c.U32()); // v0 values fit in int32 in practice
                                   ctts.emplace_back(count, offset);
                               }
                           } else if (type == FourCC("stsc"))
                           {
                               const auto d = Payload(b);
                               Cursor c{.p = d.data(), .n = d.size()};
                               c.Skip(4);
                               const uint32_t n = c.U32();
                               c.CheckCount(n, 12);
                               stsc.reserve(n);
                               for (uint32_t i = 0; i < n; ++i)
                               {
                                   const uint32_t first = c.U32();
                                   const uint32_t per   = c.U32();
                                   c.Skip(4); // sample_description_index
                                   stsc.push_back({.FirstChunk = first, .PerChunk = per});
                               }
                           } else if (type == FourCC("stsz"))
                           {
                               const auto d = Payload(b);
                               Cursor c{.p = d.data(), .n = d.size()};
                               c.Skip(4);
                               fixedSize   = c.U32();
                               sampleCount = c.U32();
                               if (fixedSize == 0)
                               {
                                   c.CheckCount(sampleCount, 4);
                                   sizes.reserve(sampleCount);
                                   for (uint32_t i = 0; i < sampleCount; ++i)
                                       sizes.push_back(c.U32());
                               }
                           } else if (type == FourCC("stz2"))
                           {
                               throw runtime_error("MP4: compact sample sizes (stz2) aren't supported");
                           } else if (type == FourCC("stco"))
                           {
                               const auto d = Payload(b);
                               Cursor c{.p = d.data(), .n = d.size()};
                               c.Skip(4);
                               const uint32_t n = c.U32();
                               c.CheckCount(n, 4);
                               chunkOffsets.reserve(n);
                               for (uint32_t i = 0; i < n; ++i)
                                   chunkOffsets.push_back(c.U32());
                           } else if (type == FourCC("co64"))
                           {
                               const auto d = Payload(b);
                               Cursor c{.p = d.data(), .n = d.size()};
                               c.Skip(4);
                               const uint32_t n = c.U32();
                               c.CheckCount(n, 8);
                               chunkOffsets.reserve(n);
                               for (uint32_t i = 0; i < n; ++i)
                                   chunkOffsets.push_back(c.U64());
                           } else if (type == FourCC("stss"))
                           {
                               const auto d = Payload(b);
                               Cursor c{.p = d.data(), .n = d.size()};
                               c.Skip(4);
                               const uint32_t n = c.U32();
                               c.CheckCount(n, 4);
                               hasStss = true;
                               syncSamples.reserve(n);
                               for (uint32_t i = 0; i < n; ++i)
                                   syncSamples.push_back(c.U32());
                           }
                       });

            const size_t count = sampleCount;
            if (count == 0)
                return;
            if (sizes.empty())
                sizes.assign(count, fixedSize);
            if (sizes.size() != count || stsc.empty() || chunkOffsets.empty())
                throw runtime_error("MP4: incomplete sample tables");

            auto &samples = t.Samples;
            samples.assign(count, Mp4Sample{});

            // Timing
            size_t idx  = 0;
            int64_t dts = 0;
            for (const auto &[cnt, delta]: stts)
                for (uint32_t k = 0; k < cnt && idx < count; ++k, ++idx)
                {
                    samples[idx].Dts = dts;
                    samples[idx].Pts = dts;
                    dts += delta;
                }
            if (idx != count)
                throw runtime_error("MP4: stts is shorter than the sample count");
            t.TotalDelta = static_cast<uint64_t>(dts);

            idx = 0;
            for (const auto &[cnt, offset]: ctts)
                for (uint32_t k = 0; k < cnt && idx < count; ++k, ++idx)
                    samples[idx].Pts = samples[idx].Dts + offset;

            // Chunk -> sample file offsets
            idx        = 0;
            size_t run = 0;
            for (size_t chunk = 0; chunk < chunkOffsets.size() && idx < count; ++chunk)
            {
                while (run + 1 < stsc.size() && stsc[run + 1].FirstChunk <= chunk + 1)
                    ++run;
                uint64_t off = chunkOffsets[chunk];
                for (uint32_t k = 0; k < stsc[run].PerChunk && idx < count; ++k, ++idx)
                {
                    samples[idx].Offset = off;
                    samples[idx].Size   = sizes[idx];
                    off += sizes[idx];
                }
            }
            if (idx != count)
                throw runtime_error("MP4: sample-to-chunk table doesn't cover every sample");

            // Sync samples (no stss = every sample is a sync sample)
            if (hasStss)
            {
                for (auto &s: samples)
                    s.Sync = false;
                for (const uint32_t n: syncSamples)
                    if (n >= 1 && n <= count)
                        samples[n - 1].Sync = true;
            }
        }
    };
} // namespace SF::ISOBMFF
