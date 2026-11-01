#pragma once

#include "VideoImportCommon.hpp"

#include <wels/codec_api.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace SF::Engine::VideoImport
{
    /**
     * H.264 -> raw frames via openh264. Input packets are AVCC (length-prefixed NALs, as stored in MP4 / MKV);
     * `avcC` is the decoder configuration record (SPS/PPS). Empty avcC = parameter sets are in-band.
     *
     * openh264 limits: 8-bit 4:2:0 progressive/interlaced-as-frames only. High 10 / 4:2:2 / 4:4:4 streams fail with a
     * descriptive exception once no frame has come out after a number of bitstream errors.
     */
    class H264RawSource final : public IRawFrameSource
    {
    public:
        H264RawSource(IPacketSource &packets, const vector<uint8_t> &avcC, const Rational fps) :
            packets_(packets), fps_(fps)
        {
            ParseAvcC(avcC);

            if (WelsCreateDecoder(&decoder_) != 0 || !decoder_)
                throw runtime_error("openh264: WelsCreateDecoder failed");

            try
            {
                SDecodingParam param{};
                param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
                param.eEcActiveIdc                = ERROR_CON_DISABLE;
                if (decoder_->Initialize(&param) != 0)
                    throw runtime_error("openh264: decoder Initialize failed");

                int logLevel = WELS_LOG_QUIET;
                decoder_->SetOption(DECODER_OPTION_TRACE_LEVEL, &logLevel);

                if (!paramSets_.empty())
                {
                    unsigned char *dst[3] = {};
                    SBufferInfo info{};
                    decoder_->DecodeFrame2(paramSets_.data(), static_cast<int>(paramSets_.size()), dst, &info);
                }

                // The encoder needs the frame size up front, so decode the first frame now and hold it.
                pending_ = DecodeOne();
                if (!pending_)
                    throw runtime_error("openh264: no decodable frames in the stream");
            } catch (...)
            {
                Close();
                throw;
            }
        }

        ~H264RawSource() override { Close(); }

        H264RawSource(const H264RawSource &)            = delete;
        H264RawSource &operator=(const H264RawSource &) = delete;

        [[nodiscard]] uint32_t Width() const override { return FrameConverter::OutWidth(first_); }
        [[nodiscard]] uint32_t Height() const override { return FrameConverter::OutHeight(first_); }
        [[nodiscard]] uint32_t FpsNum() const override { return fps_.Num; }
        [[nodiscard]] uint32_t FpsDen() const override { return fps_.Den; }
        [[nodiscard]] uint8_t BitDepth() const override { return 8; }

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
        ISVCDecoder *decoder_ = nullptr;

        uint8_t nalLengthSize_ = 4;
        vector<uint8_t> paramSets_; // Annex B SPS+PPS from avcC
        vector<uint8_t> annexb_;    // scratch

        DecodedPlanes cur_;   // last decoded frame; pointers stay valid until the next decode call
        DecodedPlanes first_; // dimensions of the first frame
        FrameConverter conv_;
        bool pending_       = false; // cur_ holds a frame nobody has consumed yet
        bool flushing_      = false;
        bool ended_         = false;
        bool haveFirst_     = false;
        bool produced_      = false;
        int errors_         = 0;
        int64_t index_      = 0;

        static constexpr uint8_t kStartCode[4] = {0, 0, 0, 1};

        void Close()
        {
            if (decoder_)
            {
                decoder_->Uninitialize();
                WelsDestroyDecoder(decoder_);
                decoder_ = nullptr;
            }
        }

        void ParseAvcC(const vector<uint8_t> &avcC)
        {
            paramSets_.clear();
            if (avcC.size() < 7 || avcC[0] != 1)
                return;

            nalLengthSize_ = static_cast<uint8_t>((avcC[4] & 0x03) + 1);

            size_t pos          = 5;
            const size_t numSps = avcC[pos++] & 0x1F;
            auto readSet        = [&]() -> bool
            {
                if (pos + 2 > avcC.size())
                    return false;
                const size_t len = (static_cast<size_t>(avcC[pos]) << 8) | avcC[pos + 1];
                pos += 2;
                if (pos + len > avcC.size())
                    return false;
                paramSets_.insert(paramSets_.end(), kStartCode, kStartCode + 4);
                paramSets_.insert(paramSets_.end(), avcC.begin() + static_cast<ptrdiff_t>(pos),
                                  avcC.begin() + static_cast<ptrdiff_t>(pos + len));
                pos += len;
                return true;
            };

            for (size_t i = 0; i < numSps; ++i)
                if (!readSet())
                    return;
            if (pos >= avcC.size())
                return;
            const size_t numPps = avcC[pos++];
            for (size_t i = 0; i < numPps; ++i)
                if (!readSet())
                    return;
        }

        void ToAnnexB(const vector<uint8_t> &sample)
        {
            annexb_.clear();
            annexb_.reserve(sample.size() + 16);
            size_t pos = 0;
            while (pos + nalLengthSize_ <= sample.size())
            {
                size_t len = 0;
                for (uint8_t i = 0; i < nalLengthSize_; ++i)
                    len = (len << 8) | sample[pos + i];
                pos += nalLengthSize_;
                if (len == 0 || pos + len > sample.size())
                    break; // truncated / corrupt, keep what we have
                annexb_.insert(annexb_.end(), kStartCode, kStartCode + 4);
                annexb_.insert(annexb_.end(), sample.begin() + static_cast<ptrdiff_t>(pos),
                               sample.begin() + static_cast<ptrdiff_t>(pos + len));
                pos += len;
            }
        }

        void Capture(unsigned char *const dst[3], const SBufferInfo &info)
        {
            const auto &sb = info.UsrData.sSystemBuffer;

            cur_              = {};
            cur_.Plane[0]     = dst[0];
            cur_.Plane[1]     = dst[1];
            cur_.Plane[2]     = dst[2];
            cur_.Stride[0]    = static_cast<uint32_t>(sb.iStride[0]);
            cur_.Stride[1]    = static_cast<uint32_t>(sb.iStride[1]);
            cur_.Stride[2]    = static_cast<uint32_t>(sb.iStride[1]);
            cur_.Width        = static_cast<uint32_t>(sb.iWidth);
            cur_.Height       = static_cast<uint32_t>(sb.iHeight);
            cur_.ChromaShiftX = 1;
            cur_.ChromaShiftY = 1;
            cur_.BitDepth     = 8;
            cur_.Wide         = false;

            if (!haveFirst_)
            {
                first_     = cur_;
                haveFirst_ = true;
            } else if (cur_.Width != first_.Width || cur_.Height != first_.Height)
                throw runtime_error("openh264: resolution changed mid-stream, which import doesn't support");
        }

        /// Decodes until one frame is available in cur_. false = end of stream.
        bool DecodeOne()
        {
            for (;;)
            {
                unsigned char *dst[3] = {};
                SBufferInfo info{};

                if (!flushing_)
                {
                    CompressedPacket pkt;
                    if (!packets_.Next(pkt))
                    {
                        flushing_ = true;
                        continue;
                    }

                    ToAnnexB(pkt.Data);
                    if (annexb_.empty())
                        continue;

                    const int state = static_cast<int>(
                            decoder_->DecodeFrame2(annexb_.data(), static_cast<int>(annexb_.size()), dst, &info));
                    if (state & (dsInvalidArgument | dsOutOfMemory | dsInitialOptExpected))
                        throw runtime_error("openh264: decoder failure (state " + to_string(state) + ")");

                    if (info.iBufferStatus != 1)
                    {
                        if (!produced_ && state != dsErrorFree && state != dsFramePending && ++errors_ > 64)
                            throw runtime_error("openh264 can't decode this stream. It only supports 8-bit 4:2:0 "
                                                "H.264 (no High 10 / 4:2:2 / 4:4:4), or the stream is corrupt.");
                        continue;
                    }
                } else
                {
                    decoder_->FlushFrame(dst, &info);
                    if (info.iBufferStatus != 1)
                        return false;
                }

                produced_ = true;
                Capture(dst, info);
                return true;
            }
        }
    };
} // namespace SF::Engine::VideoImport
