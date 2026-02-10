#include "Audio/Decoders/FfmpegAudioDecoder.h"

#include "Core/Debug/Log.h"

#include <cstring>

#if defined(LT_ENABLE_FFMPEG)

extern "C"
{
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/avutil.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/samplefmt.h>
    #include <libswresample/swresample.h>
}

namespace Limitless::Audio::Decoders
{
    namespace
    {
        struct MemoryReader
        {
            const uint8_t* Data = nullptr;
            size_t Size = 0;
            size_t Offset = 0;
        };

        int ReadPacket(void* opaque, uint8_t* buf, int buf_size)
        {
            auto* r = static_cast<MemoryReader*>(opaque);
            if (!r || !r->Data || r->Size == 0)
            {
                return AVERROR_EOF;
            }

            const size_t remaining = (r->Offset < r->Size) ? (r->Size - r->Offset) : 0;
            if (remaining == 0)
            {
                return AVERROR_EOF;
            }

            const size_t toCopy = std::min<size_t>(static_cast<size_t>(buf_size), remaining);
            std::memcpy(buf, r->Data + r->Offset, toCopy);
            r->Offset += toCopy;
            return static_cast<int>(toCopy);
        }

        int64_t Seek(void* opaque, int64_t offset, int whence)
        {
            auto* r = static_cast<MemoryReader*>(opaque);
            if (!r)
            {
                return AVERROR(EINVAL);
            }

            if (whence == AVSEEK_SIZE)
            {
                return static_cast<int64_t>(r->Size);
            }

            size_t newOffset = r->Offset;
            if (whence == SEEK_SET)
            {
                if (offset < 0)
                {
                    return AVERROR(EINVAL);
                }
                newOffset = static_cast<size_t>(offset);
            }
            else if (whence == SEEK_CUR)
            {
                const int64_t cur = static_cast<int64_t>(r->Offset);
                const int64_t next = cur + offset;
                if (next < 0)
                {
                    return AVERROR(EINVAL);
                }
                newOffset = static_cast<size_t>(next);
            }
            else if (whence == SEEK_END)
            {
                const int64_t end = static_cast<int64_t>(r->Size);
                const int64_t next = end + offset;
                if (next < 0)
                {
                    return AVERROR(EINVAL);
                }
                newOffset = static_cast<size_t>(next);
            }
            else
            {
                return AVERROR(EINVAL);
            }

            if (newOffset > r->Size)
            {
                return AVERROR(EINVAL);
            }

            r->Offset = newOffset;
            return static_cast<int64_t>(r->Offset);
        }

        std::string AvErr(int err)
        {
            char buf[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(err, buf, sizeof(buf));
            return std::string(buf);
        }

        // Decode to float32 stereo @ TargetSampleRateHz.
        Result<std::shared_ptr<AudioClip>> DecodeInternal(AVFormatContext* fmt, const std::string& debugName, const FfmpegAudioDecoder::DecodeSettings& settings)
        {
            int audioStreamIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (audioStreamIndex < 0)
            {
                return Result<std::shared_ptr<AudioClip>>(ErrorCode::NotSupported, "FFmpeg: no audio stream found");
            }

            AVStream* stream = fmt->streams[audioStreamIndex];
            if (!stream || !stream->codecpar)
            {
                return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: invalid stream codec parameters");
            }

            const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!codec)
            {
                return Result<std::shared_ptr<AudioClip>>(ErrorCode::NotSupported, "FFmpeg: unsupported codec");
            }

            AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
            if (!codecCtx)
            {
                return Result<std::shared_ptr<AudioClip>>(ErrorCode::OutOfMemory, "FFmpeg: avcodec_alloc_context3 failed");
            }

            auto cleanupCodecCtx = [&]() {
                avcodec_free_context(&codecCtx);
            };

            int err = avcodec_parameters_to_context(codecCtx, stream->codecpar);
            if (err < 0)
            {
                cleanupCodecCtx();
                return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: parameters_to_context failed: " + AvErr(err));
            }

            err = avcodec_open2(codecCtx, codec, nullptr);
            if (err < 0)
            {
                cleanupCodecCtx();
                return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: avcodec_open2 failed: " + AvErr(err));
            }

            AVPacket* pkt = av_packet_alloc();
            AVFrame* frame = av_frame_alloc();
            if (!pkt || !frame)
            {
                if (pkt) av_packet_free(&pkt);
                if (frame) av_frame_free(&frame);
                cleanupCodecCtx();
                return Result<std::shared_ptr<AudioClip>>(ErrorCode::OutOfMemory, "FFmpeg: packet/frame alloc failed");
            }

            auto cleanupPktFrame = [&]() {
                av_packet_free(&pkt);
                av_frame_free(&frame);
            };

            // Setup resampler to float32 stereo @ target rate.
            SwrContext* swr = swr_alloc();
            if (!swr)
            {
                cleanupPktFrame();
                cleanupCodecCtx();
                return Result<std::shared_ptr<AudioClip>>(ErrorCode::OutOfMemory, "FFmpeg: swr_alloc failed");
            }

            auto cleanupSwr = [&]() {
                swr_free(&swr);
            };

            const AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
            const int outRate = static_cast<int>(settings.TargetSampleRateHz);
            const AVSampleFormat outFmt = AV_SAMPLE_FMT_FLT;

            AVChannelLayout inLayout = codecCtx->ch_layout;
            if (inLayout.nb_channels == 0)
            {
                // Some files omit layout; derive from channel count.
                const int channels = (stream->codecpar && stream->codecpar->ch_layout.nb_channels > 0)
                    ? stream->codecpar->ch_layout.nb_channels
                    : (codecCtx->ch_layout.nb_channels > 0 ? codecCtx->ch_layout.nb_channels : 2);
                av_channel_layout_default(&inLayout, channels);
            }

            err = swr_alloc_set_opts2(&swr,
                                     &outLayout, outFmt, outRate,
                                     &inLayout, codecCtx->sample_fmt, codecCtx->sample_rate,
                                     0, nullptr);
            if (err < 0)
            {
                cleanupSwr();
                cleanupPktFrame();
                cleanupCodecCtx();
                return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: swr_alloc_set_opts2 failed: " + AvErr(err));
            }

            err = swr_init(swr);
            if (err < 0)
            {
                cleanupSwr();
                cleanupPktFrame();
                cleanupCodecCtx();
                return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: swr_init failed: " + AvErr(err));
            }

            auto clip = std::make_shared<AudioClip>();
            clip->SampleRateHz = settings.TargetSampleRateHz;
            clip->ChannelCount = 2;

            // Decode loop.
            while ((err = av_read_frame(fmt, pkt)) >= 0)
            {
                if (pkt->stream_index != audioStreamIndex)
                {
                    av_packet_unref(pkt);
                    continue;
                }

                err = avcodec_send_packet(codecCtx, pkt);
                av_packet_unref(pkt);
                if (err < 0)
                {
                    cleanupSwr();
                    cleanupPktFrame();
                    cleanupCodecCtx();
                    return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: send_packet failed: " + AvErr(err));
                }

                while ((err = avcodec_receive_frame(codecCtx, frame)) >= 0)
                {
                    // Convert this frame to float stereo.
                    const int inSamples = frame->nb_samples;
                    const int outSamplesMax = swr_get_out_samples(swr, inSamples);
                    if (outSamplesMax <= 0)
                    {
                        av_frame_unref(frame);
                        continue;
                    }

                    // Allocate temporary output buffer (per-frame) would be bad.
                    // Instead, convert directly into the clip vector by growing once per frame.
                    const size_t oldSize = clip->Samples.size();
                    clip->Samples.resize(oldSize + static_cast<size_t>(outSamplesMax) * 2);

                    uint8_t* outPlanes[1] = { reinterpret_cast<uint8_t*>(clip->Samples.data() + oldSize) };
                    const int outSamples = swr_convert(swr,
                                                       outPlanes, outSamplesMax,
                                                       const_cast<const uint8_t**>(frame->extended_data), inSamples);
                    if (outSamples < 0)
                    {
                        cleanupSwr();
                        cleanupPktFrame();
                        cleanupCodecCtx();
                        return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: swr_convert failed: " + AvErr(outSamples));
                    }

                    // Trim to the actual amount written.
                    clip->Samples.resize(oldSize + static_cast<size_t>(outSamples) * 2);
                    av_frame_unref(frame);
                }

                if (err != AVERROR(EAGAIN) && err != AVERROR_EOF)
                {
                    cleanupSwr();
                    cleanupPktFrame();
                    cleanupCodecCtx();
                    return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: receive_frame failed: " + AvErr(err));
                }
            }

            // Flush decoder.
            err = avcodec_send_packet(codecCtx, nullptr);
            if (err >= 0)
            {
                while ((err = avcodec_receive_frame(codecCtx, frame)) >= 0)
                {
                    const int inSamples = frame->nb_samples;
                    const int outSamplesMax = swr_get_out_samples(swr, inSamples);
                    if (outSamplesMax > 0)
                    {
                        const size_t oldSize = clip->Samples.size();
                        clip->Samples.resize(oldSize + static_cast<size_t>(outSamplesMax) * 2);

                        uint8_t* outPlanes[1] = { reinterpret_cast<uint8_t*>(clip->Samples.data() + oldSize) };
                        const int outSamples = swr_convert(swr,
                                                           outPlanes, outSamplesMax,
                                                           const_cast<const uint8_t**>(frame->extended_data), inSamples);
                        if (outSamples < 0)
                        {
                            cleanupSwr();
                            cleanupPktFrame();
                            cleanupCodecCtx();
                            return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: swr_convert failed during flush: " + AvErr(outSamples));
                        }

                        clip->Samples.resize(oldSize + static_cast<size_t>(outSamples) * 2);
                    }
                    av_frame_unref(frame);
                }
            }

            cleanupSwr();
            cleanupPktFrame();
            cleanupCodecCtx();

            if (clip->Samples.empty())
            {
                return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: decoded clip is empty");
            }

            LT_CORE_INFO("AudioClip decoded ({}): {:.2f}s, {} Hz, {} ch, {} frames",
                         debugName, clip->GetDurationSeconds(), clip->SampleRateHz, clip->ChannelCount, clip->GetFrameCount());

            return clip;
        }
    }

    Result<std::shared_ptr<AudioClip>> FfmpegAudioDecoder::DecodeFromFile(const std::string& absolutePath, const DecodeSettings& settings)
    {
        if (absolutePath.empty())
        {
            return Result<std::shared_ptr<AudioClip>>(ErrorCode::InvalidArgument, "FFmpeg: empty path");
        }

        AVFormatContext* fmt = nullptr;
        const int err = avformat_open_input(&fmt, absolutePath.c_str(), nullptr, nullptr);
        if (err < 0 || !fmt)
        {
            return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileNotFound, "FFmpeg: open_input failed: " + AvErr(err));
        }

        auto cleanupFmt = [&]() {
            avformat_close_input(&fmt);
        };

        int infoErr = avformat_find_stream_info(fmt, nullptr);
        if (infoErr < 0)
        {
            cleanupFmt();
            return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: find_stream_info failed: " + AvErr(infoErr));
        }

        auto decoded = DecodeInternal(fmt, absolutePath, settings);
        cleanupFmt();
        return decoded;
    }

    Result<std::shared_ptr<AudioClip>> FfmpegAudioDecoder::DecodeFromMemory(const uint8_t* bytes, size_t byteCount, const std::string& debugName, const DecodeSettings& settings)
    {
        if (!bytes || byteCount == 0)
        {
            return Result<std::shared_ptr<AudioClip>>(ErrorCode::InvalidArgument, "FFmpeg: empty memory buffer");
        }

        AVFormatContext* fmt = avformat_alloc_context();
        if (!fmt)
        {
            return Result<std::shared_ptr<AudioClip>>(ErrorCode::OutOfMemory, "FFmpeg: alloc_context failed");
        }

        MemoryReader reader{};
        reader.Data = bytes;
        reader.Size = byteCount;
        reader.Offset = 0;

        // FFmpeg requires an internal IO buffer it can write into.
        // 32 KiB is typical.
        constexpr int ioBufferSize = 32 * 1024;
        uint8_t* ioBuffer = static_cast<uint8_t*>(av_malloc(ioBufferSize));
        if (!ioBuffer)
        {
            avformat_free_context(fmt);
            return Result<std::shared_ptr<AudioClip>>(ErrorCode::OutOfMemory, "FFmpeg: av_malloc io buffer failed");
        }

        AVIOContext* avio = avio_alloc_context(ioBuffer, ioBufferSize, 0, &reader, &ReadPacket, nullptr, &Seek);
        if (!avio)
        {
            av_free(ioBuffer);
            avformat_free_context(fmt);
            return Result<std::shared_ptr<AudioClip>>(ErrorCode::OutOfMemory, "FFmpeg: avio_alloc_context failed");
        }

        fmt->pb = avio;
        fmt->flags |= AVFMT_FLAG_CUSTOM_IO;

        int err = avformat_open_input(&fmt, nullptr, nullptr, nullptr);
        if (err < 0)
        {
            avio_context_free(&avio); // also frees ioBuffer
            avformat_free_context(fmt);
            return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: open_input (memory) failed: " + AvErr(err));
        }

        int infoErr = avformat_find_stream_info(fmt, nullptr);
        if (infoErr < 0)
        {
            avformat_close_input(&fmt);
            avio_context_free(&avio);
            return Result<std::shared_ptr<AudioClip>>(ErrorCode::FileCorrupted, "FFmpeg: find_stream_info (memory) failed: " + AvErr(infoErr));
        }

        auto decoded = DecodeInternal(fmt, debugName, settings);

        avformat_close_input(&fmt);
        avio_context_free(&avio);
        return decoded;
    }
}

#else

namespace Limitless::Audio::Decoders
{
    Result<std::shared_ptr<AudioClip>> FfmpegAudioDecoder::DecodeFromFile(const std::string&, const DecodeSettings&)
    {
        return Result<std::shared_ptr<AudioClip>>(ErrorCode::NotSupported, "FFmpeg decoding not enabled in this build (LT_ENABLE_FFMPEG not defined)");
    }

    Result<std::shared_ptr<AudioClip>> FfmpegAudioDecoder::DecodeFromMemory(const uint8_t*, size_t, const std::string&, const DecodeSettings&)
    {
        return Result<std::shared_ptr<AudioClip>>(ErrorCode::NotSupported, "FFmpeg decoding not enabled in this build (LT_ENABLE_FFMPEG not defined)");
    }
}

#endif

