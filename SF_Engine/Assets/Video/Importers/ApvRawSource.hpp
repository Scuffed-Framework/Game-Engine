#pragma once

#include "VideoImportCommon.hpp"

#include <oapv/oapv.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace SF::Engine::VideoImport
{
    /**
     * APV -> raw frames via openapv (API set 1: per-PBU decoding).
     *
     * Each packet is one access unit. It must start with the 'aPv1' signature; if it doesn't (ISO-BMFF samples may omit
     * it) the signature is prepended. Only the first primary frame PBU of each AU is decoded; alpha, depth, preview and
     * metadata PBUs are skipped. APV is intra-only, so every packet is a keyframe.
     *
     * Output goes through FrameConverter, so 4:2:2 / 4:4:4 / 4:0:0 and 10/12/16-bit sources become 4:2:0 at 8 or 10 bit
     * for the AV1 encoder. 4:2:2 -> 4:2:0 and 12-bit -> 10-bit are lossy.
     */
    class ApvRawSource final : public IRawFrameSource
    {
    public:
        ApvRawSource(IPacketSource &packets, const Rational fps) : packets_(packets), fps_(fps)
        {
            oapvd_cdesc_t cdesc{};
            cdesc.threads = OAPV_CDESC_THREADS_AUTO;

            int err  = OAPV_OK;
            decoder_ = oapvd_create(&cdesc, &err);
            if (!decoder_ || OAPV_FAILED(err))
                throw runtime_error("openapv: oapvd_create failed (code " + to_string(err) + ")");

            try
            {
                pending_ = DecodeOne();
                if (!pending_)
                    throw runtime_error("openapv: no decodable frames in the stream");
            } catch (...)
            {
                Close();
                throw;
            }
        }

        ~ApvRawSource() override { Close(); }

        ApvRawSource(const ApvRawSource &)            = delete;
        ApvRawSource &operator=(const ApvRawSource &) = delete;

        [[nodiscard]] uint32_t Width() const override { return FrameConverter::OutWidth(first_); }
        [[nodiscard]] uint32_t Height() const override { return FrameConverter::OutHeight(first_); }
        [[nodiscard]] uint32_t FpsNum() const override { return fps_.Num; }
        [[nodiscard]] uint32_t FpsDen() const override { return fps_.Den; }
        [[nodiscard]] uint8_t BitDepth() const override { return FrameConverter::OutBitDepth(first_); }

        bool Next(RawVideoFrame &out) override
        {
            if (!pending_)
            {
                if (ended_)
                    return false;
                pending_ = DecodeOne();
                if (!pending_)
                {
                    ended_ = true;
                    return false;
                }
            }
            pending_ = false;

            conv_.Convert(cur_, out);
            out.Pts = index_++;
            return true;
        }

    private:
        IPacketSource &packets_;
        Rational fps_;
        oapvd_t decoder_ = nullptr;

        // Output image, reused while the frame format stays the same.
        oapv_imgb_t imgb_{};
        vector<uint8_t> planes_[OAPV_MAX_CC];
        int imgW_ = 0, imgH_ = 0, imgCs_ = 0;

        vector<uint8_t> au_; // scratch: AU with signature

        DecodedPlanes cur_;
        DecodedPlanes first_;
        FrameConverter conv_;
        bool pending_   = false;
        bool ended_     = false;
        bool haveFirst_ = false;
        bool produced_  = false;
        int errors_     = 0;
        int64_t index_  = 0;

        void Close()
        {
            if (decoder_)
            {
                oapvd_delete(decoder_);
                decoder_ = nullptr;
            }
        }

        static uint32_t BE32(const uint8_t *p)
        {
            return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                   (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
        }

        void EnsureImage(const oapv_frm_info_t &fi)
        {
            if (imgW_ == fi.w && imgH_ == fi.h && imgCs_ == fi.cs)
                return;

            const int fmt   = OAPV_CS_GET_FORMAT(fi.cs);
            const int bytes = OAPV_CS_GET_BYTE_DEPTH(fi.cs);
            if (fmt == OAPV_CF_PLANAR2 || fmt == OAPV_CF_UNKNOWN)
                throw runtime_error("openapv: unsupported output color format " + to_string(fmt));

            const int np = fmt == OAPV_CF_YCBCR400 ? 1 : (fmt == OAPV_CF_YCBCR4444 ? 4 : 3);
            const int xs = (fmt == OAPV_CF_YCBCR420 || fmt == OAPV_CF_YCBCR422 || fmt == OAPV_CF_YCBCR422W) ? 1 : 0;
            const int ys = fmt == OAPV_CF_YCBCR420 ? 1 : 0;

            imgb_ = {};
            for (auto &p: planes_)
                p.clear();

            imgb_.cs = fi.cs;
            imgb_.np = np;
            for (int i = 0; i < np; ++i)
            {
                const bool chroma = (i == 1 || i == 2);
                const int sx      = chroma ? xs : 0;
                const int sy      = chroma ? ys : 0;
                const int aw      = ((fi.w + OAPV_MB_W - 1) & ~(OAPV_MB_W - 1)) >> sx;
                const int ah      = ((fi.h + OAPV_MB_H - 1) & ~(OAPV_MB_H - 1)) >> sy;

                planes_[i].assign(static_cast<size_t>(aw) * bytes * ah, 0);
                imgb_.a[i]     = planes_[i].data();
                imgb_.baddr[i] = planes_[i].data();
                imgb_.w[i]     = (fi.w + sx) >> sx;
                imgb_.h[i]     = (fi.h + sy) >> sy;
                imgb_.aw[i]    = aw;
                imgb_.ah[i]    = ah;
                imgb_.s[i]     = aw * bytes;
                imgb_.e[i]     = ah;
                imgb_.bsize[i] = aw * bytes * ah;
            }

            imgW_  = fi.w;
            imgH_  = fi.h;
            imgCs_ = fi.cs;
        }

        void Capture(const oapv_frm_info_t &fi)
        {
            const int fmt   = OAPV_CS_GET_FORMAT(fi.cs);
            const int bytes = OAPV_CS_GET_BYTE_DEPTH(fi.cs);

            cur_ = {};
            for (int i = 0; i < imgb_.np && i < 3; ++i)
            {
                cur_.Plane[i]  = static_cast<const uint8_t *>(imgb_.a[i]);
                cur_.Stride[i] = static_cast<uint32_t>(imgb_.s[i]);
            }
            cur_.Width  = static_cast<uint32_t>(fi.w);
            cur_.Height = static_cast<uint32_t>(fi.h);
            cur_.ChromaShiftX =
                    (fmt == OAPV_CF_YCBCR420 || fmt == OAPV_CF_YCBCR422 || fmt == OAPV_CF_YCBCR422W) ? 1 : 0;
            cur_.ChromaShiftY = fmt == OAPV_CF_YCBCR420 ? 1 : 0;
            cur_.BitDepth     = static_cast<uint8_t>(OAPV_CS_GET_BIT_DEPTH(fi.cs));
            cur_.Wide         = bytes > 1;

            if (!haveFirst_)
            {
                first_     = cur_;
                haveFirst_ = true;
            } else if (cur_.Width != first_.Width || cur_.Height != first_.Height)
                throw runtime_error("openapv: resolution changed mid-stream, which import doesn't support");
        }

        /// Decodes until one frame is available in cur_. false = end of stream.
        bool DecodeOne()
        {
            static constexpr uint8_t kSignature[4] = {'a', 'P', 'v', '1'};

            for (;;)
            {
                CompressedPacket pkt;
                if (!packets_.Next(pkt))
                    return false;

                if (pkt.Data.size() >= 4 && memcmp(pkt.Data.data(), kSignature, 4) == 0)
                    au_ = std::move(pkt.Data);
                else
                {
                    au_.assign(kSignature, kSignature + 4);
                    au_.insert(au_.end(), pkt.Data.begin(), pkt.Data.end());
                }

                size_t pos = 4; // past the signature; then repeated { pbu_size (BE32), pbu }
                while (pos + 4 <= au_.size())
                {
                    const uint32_t pbuSize = BE32(au_.data() + pos);
                    pos += 4;
                    if (pbuSize == 0 || pos + pbuSize > au_.size())
                        break;
                    uint8_t *pbu = au_.data() + pos;
                    pos += pbuSize;

                    oapv_pbu_info_t pi{};
                    if (OAPV_FAILED(oapvd_info_pbu(pbu, static_cast<int>(pbuSize), &pi)) ||
                        pi.pbu_type != OAPV_PBU_TYPE_PRIMARY_FRAME)
                        continue;

                    oapv_frm_info_t fi{};
                    if (OAPV_FAILED(oapvd_info_frame(pbu, static_cast<int>(pbuSize), &fi)))
                        continue;
                    EnsureImage(fi);

                    oapv_bitb_t bitb{};
                    bitb.addr  = pbu;
                    bitb.ssize = static_cast<int>(pbuSize);
                    bitb.bsize = static_cast<int>(pbuSize);

                    oapvd_stat_t stat{};
                    if (const int r = oapvd_decode_frame(decoder_, &bitb, &imgb_, &stat); OAPV_FAILED(r))
                    {
                        if (!produced_ && ++errors_ > 16)
                            throw runtime_error("openapv: decode failed (code " + to_string(r) + ")");
                        continue;
                    }

                    produced_ = true;
                    Capture(fi);
                    return true;
                }
            }
        }
    };
} // namespace SF::Engine::VideoImport
