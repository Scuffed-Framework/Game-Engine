#include "ForeignVideo.hpp"

#include "ApvRawSource.hpp"
#include "H264RawSource.hpp"
#include "VideoImportCommon.hpp"
#include "VpxRawSource.hpp"

#include <1stPartyLibs/ISOBMFF/Mp4Demuxer.hpp>
#include <1stPartyLibs/Matroska/Demuxer.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

namespace SF::Engine
{
    using namespace VideoImport;

    namespace
    {
        // MP4 / MOV
        class Mp4PacketSource final : public IPacketSource
        {
        public:
            Mp4PacketSource(ISOBMFF::Mp4Demuxer &demux, const ISOBMFF::Mp4Track &track) : demux_(demux), track_(track)
            {
            }

            bool Next(CompressedPacket &out) override
            {
                if (index_ >= track_.Samples.size())
                    return false;
                const auto &s = track_.Samples[index_++];
                if (!demux_.ReadSample(s, out.Data))
                    return false; // truncated file: treat as end of stream
                out.Pts = s.Pts;
                out.Key = s.Sync;
                return true;
            }

        private:
            ISOBMFF::Mp4Demuxer &demux_;
            const ISOBMFF::Mp4Track &track_;
            size_t index_ = 0;
        };

        // AV1 in MP4 is already AV1: only the container changes.
        void RemuxMp4Av1(const ISOBMFF::Mp4Demuxer &demux, const ISOBMFF::Mp4Track &t, const File &outMkv)
        {
            if (t.Timescale == 0)
                throw std::runtime_error("MP4: track has no timescale");

            VideoInfo info;
            info.Width        = t.Width;
            info.Height       = t.Height;
            info.TimebaseNum  = 1;
            info.TimebaseDen  = 1000;     // ms ticks, the Matroska default
            info.CodecPrivate = t.Config; // av1C payload is exactly what Matroska wants as CodecPrivate
            {
                const auto [Num, Den] =
                        FrameRateFrom(static_cast<uint64_t>(t.Samples.size()) * t.Timescale, t.TotalDelta);
                info.FpsNum = Num;
                info.FpsDen = Den;
            }
            info.BitDepth = 8;
            if (t.Config.size() >= 3 && (t.Config[2] & 0x40))
                info.BitDepth = (t.Config[2] & 0x20) ? 12 : 10;

            std::vector<Av1Packet> packets;
            packets.reserve(t.Samples.size());
            int64_t minPts = std::numeric_limits<int64_t>::max();
            for (const auto &s: t.Samples)
            {
                Av1Packet p;
                if (!demux.ReadSample(s, p.Data))
                    break;
                p.Pts      = std::llround(static_cast<double>(s.Pts) * 1000.0 / static_cast<double>(t.Timescale));
                p.KeyFrame = s.Sync;
                minPts     = std::min(minPts, p.Pts);
                packets.push_back(std::move(p));
            }
            if (packets.empty())
                throw std::runtime_error("MP4: no AV1 samples could be read");

            VideoBuffer buffer(outMkv, false);
            buffer.SetInfo(info);
            for (auto &p: packets)
            {
                p.Pts -= minPts; // start the timeline at zero
                buffer.AddPacket(std::move(p));
            }

            WriteAtomic(outMkv, [&](const File &tmp) { MkvVideoBuffer::Write(buffer, tmp); });
        }

        // Raw .apv files: repeated { au_size (BE32), access unit (starts with 'aPv1') }
        class ApvFilePacketSource final : public IPacketSource
        {
        public:
            explicit ApvFilePacketSource(std::istream &in) : in_(in) {}

            bool Next(CompressedPacket &out) override
            {
                uint8_t h[4];
                if (!in_.read(reinterpret_cast<char *>(h), 4))
                    return false;
                const uint32_t size = (static_cast<uint32_t>(h[0]) << 24) | (static_cast<uint32_t>(h[1]) << 16) |
                                      (static_cast<uint32_t>(h[2]) << 8) | h[3];
                if (size == 0 || size > (256u << 20))
                    throw std::runtime_error("APV: invalid access unit size " + std::to_string(size));
                out.Data.resize(size);
                if (!in_.read(reinterpret_cast<char *>(out.Data.data()), static_cast<std::streamsize>(size)))
                    return false; // truncated last AU
                out.Pts = index_++;
                out.Key = true;
                return true;
            }

        private:
            std::istream &in_;
            int64_t index_ = 0;
        };

        // Matroska / WebM
        class MkvPacketSource final : public IPacketSource
        {
        public:
            MkvPacketSource(Matroska::Demuxer &demux, const uint64_t videoTrackIndex)
            {
                for (;;)
                {
                    std::optional<Matroska::Packet> pkt;
                    const int r = demux.read_packet(pkt);
                    if (r == 0)
                        break;
                    if (r < 0)
                        throw std::runtime_error("Matroska: demux error");
                    if (pkt->track != videoTrackIndex)
                        continue;

                    CompressedPacket p;
                    for (const auto &[data, duration]: pkt->frames)
                        p.Data.insert(p.Data.end(), data.begin(), data.end());
                    p.Pts = static_cast<int64_t>(pkt->timecode);
                    p.Key = pkt->keyframe == Matroska::KeyframeState::True ||
                            (pkt->keyframe == Matroska::KeyframeState::Unknown && !pkt->hasReferenceBlock);
                    packets_.push_back(std::move(p));
                }
            }

            bool Next(CompressedPacket &out) override
            {
                if (index_ >= packets_.size())
                    return false;
                out = std::move(packets_[index_++]);
                return true;
            }

            [[nodiscard]] Rational EstimateFps(const Matroska::Track &track, const uint64_t timecodeScaleNs) const
            {
                if (track.defaultDuration.count() > 0)
                    return FrameRateFrom(1000000000ull, static_cast<uint64_t>(track.defaultDuration.count()));
                if (packets_.size() < 2)
                    return {.Num = 30, .Den = 1};

                int64_t lo = std::numeric_limits<int64_t>::max();
                int64_t hi = std::numeric_limits<int64_t>::min();
                for (const auto &p: packets_)
                {
                    lo = std::min(lo, p.Pts);
                    hi = std::max(hi, p.Pts);
                }
                if (hi <= lo)
                    return {.Num = 30, .Den = 1};

                const uint64_t spanNs = static_cast<uint64_t>(hi - lo) * timecodeScaleNs;
                return FrameRateFrom(static_cast<uint64_t>(packets_.size() - 1) * 1000000000ull, spanNs);
            }

        private:
            std::vector<CompressedPacket> packets_;
            size_t index_ = 0;
        };

        // remuxAv1 = .webm sources: an AV1 stream is rewrapped as .mkv.
        // !remuxAv1 = .mkv sources: AV1 is already native, so return false and write nothing.
        bool ImportMatroska(const File &src, const File &outMkv, const bool remuxAv1)
        {
            std::ifstream in(src.GetFullPath(), std::ios::binary);
            if (!in)
                throw std::runtime_error("Matroska: cannot open " + src.GetFullPath());

            Matroska::Demuxer demux(in);
            if (!demux.initialize())
                throw std::runtime_error("Matroska: not a Matroska/WebM stream");

            int videoIdx = -1;
            for (size_t i = 0; i < demux.track_count(); ++i)
                if (demux.track(static_cast<unsigned>(i)).type == Matroska::TrackType::Video)
                {
                    videoIdx = static_cast<int>(i);
                    break;
                }
            if (videoIdx < 0)
                throw std::runtime_error("Matroska: no video track");

            const Matroska::Track &track = demux.track(static_cast<unsigned>(videoIdx));
            const std::string &codec     = track.codecID;

            if (codec == "V_AV1")
            {
                if (!remuxAv1)
                    return false;

                VideoBuffer buffer(src, false);
                MkvVideoBuffer::Load(buffer, src);
                WriteAtomic(outMkv, [&](const File &tmp) { MkvVideoBuffer::Write(buffer, tmp); });
                return true;
            }

            const bool isH264 = codec == "V_MPEG4/ISO/AVC";
            const bool isVp9  = codec == "V_VP9";
            const bool isVp8  = codec == "V_VP8";
            if (!isH264 && !isVp9 && !isVp8)
            {
                if (codec == "V_MPEGH/ISO/HEVC")
                    throw std::runtime_error("Matroska: HEVC isn't supported (no HEVC decoder is linked)");
                throw std::runtime_error("Matroska: unsupported video codec '" + codec + "'");
            }

            std::vector<uint8_t> codecPrivate;
            if (const auto *video = std::get_if<Matroska::VideoTrack>(&track.specific))
                codecPrivate.assign(video->codecPrivate.begin(), video->codecPrivate.end());

            MkvPacketSource packets(demux, static_cast<uint64_t>(videoIdx));
            const Rational fps = packets.EstimateFps(track, demux.timecode_scale());

            if (isH264)
            {
                H264RawSource raw(packets, codecPrivate, fps);
                EncodeToAv1Mkv(raw, outMkv);
            } else
            {
                VpxRawSource raw(packets, isVp9, fps);
                EncodeToAv1Mkv(raw, outMkv);
            }
            return true;
        }
    } // namespace

    bool MkvVideoBuffer::Import(const File &src, const File &outMkv) { return ImportMatroska(src, outMkv, false); }

    bool WebmVideoImporter::Import(const File &src, const File &outMkv) { return ImportMatroska(src, outMkv, true); }

    bool ApvVideoImporter::Import(const File &src, const File &outMkv)
    {
        std::ifstream in(src.GetFullPath(), std::ios::binary);
        if (!in)
            throw std::runtime_error("APV: cannot open " + src.GetFullPath());

        ApvFilePacketSource packets(in);
        ApvRawSource raw(packets, Rational{.Num = 30, .Den = 1});
        EncodeToAv1Mkv(raw, outMkv);
        return true;
    }

    bool Mp4VideoImporter::Import(const File &src, const File &outMkv)
    {
        using ISOBMFF::FourCC;

        std::ifstream in(src.GetFullPath(), std::ios::binary);
        if (!in)
            throw std::runtime_error("MP4: cannot open " + src.GetFullPath());

        ISOBMFF::Mp4Demuxer demux(in);
        if (!demux.Initialize())
            throw std::runtime_error("MP4: no moov box or no tracks (not an MP4/MOV file?)");

        const ISOBMFF::Mp4Track *track    = nullptr;
        const ISOBMFF::Mp4Track *anyVideo = nullptr;
        for (const auto &t: demux.Tracks())
        {
            if (t.Handler != FourCC("vide") || t.Samples.empty())
                continue;
            if (!anyVideo)
                anyVideo = &t;
            if (t.Codec == FourCC("avc1") || t.Codec == FourCC("avc3") || t.Codec == FourCC("vp09") ||
                t.Codec == FourCC("av01") || t.Codec == FourCC("apv1"))
            {
                track = &t;
                break;
            }
        }

        if (!track)
        {
            if (!anyVideo)
                throw std::runtime_error("MP4: no video track");
            std::string msg = "MP4: unsupported video codec '" + ISOBMFF::FourCCToString(anyVideo->Codec) + "'";
            if (anyVideo->Codec == FourCC("hvc1") || anyVideo->Codec == FourCC("hev1"))
                msg += " (HEVC: no decoder is linked)";
            throw std::runtime_error(msg + ". Supported: H.264, VP9, AV1, APV.");
        }

        if (track->Codec == FourCC("av01"))
        {
            RemuxMp4Av1(demux, *track, outMkv);
            return true;
        }

        if (track->Timescale == 0)
            throw std::runtime_error("MP4: track has no timescale");
        const Rational fps =
                FrameRateFrom(static_cast<uint64_t>(track->Samples.size()) * track->Timescale, track->TotalDelta);

        Mp4PacketSource packets(demux, *track);
        if (track->Codec == FourCC("vp09"))
        {
            VpxRawSource raw(packets, true, fps);
            EncodeToAv1Mkv(raw, outMkv);
        } else if (track->Codec == FourCC("apv1"))
        {
            ApvRawSource raw(packets, fps);
            EncodeToAv1Mkv(raw, outMkv);
        } else
        {
            H264RawSource raw(packets, track->Config, fps);
            EncodeToAv1Mkv(raw, outMkv);
        }
        return true;
    }
} // namespace SF::Engine
