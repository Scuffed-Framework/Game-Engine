#pragma once

#include "VideoImportCommon.hpp"

#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace SF::Engine::VideoImport
{
    /**
     * VP8 / VP9 -> raw frames via libvpx. Packets are plain VP8/VP9 frames (WebM blocks, MP4 'vp09' samples).
     * Handles 4:2:0 / 4:2:2 / 4:4:0 / 4:4:4 and 8/10/12-bit output (the high bit depth formats need libvpx built with
     * --enable-vp9-highbitdepth); FrameConverter brings everything to 4:2:0 at 8 or 10 bit.
     */
    class VpxRawSource final : public IRawFrameSource
    {
    public:
        VpxRawSource(IPacketSource &packets, const bool vp9, const Rational fps) : packets_(packets), fps_(fps)
        {
            vpx_codec_dec_cfg_t cfg{};
            cfg.threads = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);

            if (vpx_codec_dec_init(&ctx_, vp9 ? vpx_codec_vp9_dx() : vpx_codec_vp8_dx(), &cfg, 0) != VPX_CODEC_OK)
                throw runtime_error("libvpx: decoder init failed");
            open_ = true;

            try
            {
                pending_ = DecodeOne();
                if (!pending_)
                    throw runtime_error("libvpx: no decodable frames in the stream");
            } catch (...)
            {
                Close();
                throw;
            }
        }

        ~VpxRawSource() override { Close(); }

        VpxRawSource(const VpxRawSource &)            = delete;
        VpxRawSource &operator=(const VpxRawSource &) = delete;

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
        vpx_codec_ctx_t ctx_{};
        bool open_ = false;

        vpx_codec_iter_t iter_ = nullptr;
        bool iterActive_       = false;
        bool flushed_          = false;

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
            if (open_)
            {
                vpx_codec_destroy(&ctx_);
                open_ = false;
            }
        }

        void Capture(const vpx_image_t &img)
        {
            cur_ = {};
            for (int i = 0; i < 3; ++i)
            {
                cur_.Plane[i]  = img.planes[i];
                cur_.Stride[i] = static_cast<uint32_t>(img.stride[i]);
            }
            cur_.Width        = img.d_w;
            cur_.Height       = img.d_h;
            cur_.ChromaShiftX = static_cast<uint8_t>(img.x_chroma_shift);
            cur_.ChromaShiftY = static_cast<uint8_t>(img.y_chroma_shift);
            cur_.Wide         = (static_cast<int>(img.fmt) & VPX_IMG_FMT_HIGHBITDEPTH) != 0;
            cur_.BitDepth     = static_cast<uint8_t>(img.bit_depth ? img.bit_depth : 8);

            if (!haveFirst_)
            {
                first_     = cur_;
                haveFirst_ = true;
            } else if (cur_.Width != first_.Width || cur_.Height != first_.Height)
                throw runtime_error("libvpx: resolution changed mid-stream, which import doesn't support");
        }

        /// Decodes until one frame is available in cur_. false = end of stream.
        bool DecodeOne()
        {
            for (;;)
            {
                if (iterActive_)
                {
                    if (const vpx_image_t *img = vpx_codec_get_frame(&ctx_, &iter_))
                    {
                        produced_ = true;
                        Capture(*img);
                        return true;
                    }
                    iterActive_ = false;
                    iter_       = nullptr;
                }

                if (flushed_)
                    return false;

                CompressedPacket pkt;
                if (!packets_.Next(pkt))
                {
                    flushed_ = true;
                    vpx_codec_decode(&ctx_, nullptr, 0, nullptr, 0); // drain
                } else if (vpx_codec_decode(&ctx_, pkt.Data.data(), static_cast<unsigned int>(pkt.Data.size()), nullptr,
                                            0) != VPX_CODEC_OK)
                {
                    if (!produced_ && ++errors_ > 64)
                        throw runtime_error(string("libvpx can't decode this stream: ") + vpx_codec_error(&ctx_));
                    continue;
                }

                iter_       = nullptr;
                iterActive_ = true;
            }
        }
    };
} // namespace SF::Engine::VideoImport
