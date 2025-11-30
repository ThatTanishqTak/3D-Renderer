#include "Renderer/VideoEncoder.h"

#include <algorithm>
#include <cctype>
#include <string>

extern "C"
{
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/opt.h>
    #include <libswscale/swscale.h>
}

namespace Trident
{
    bool VideoEncoder::BeginSession(const std::filesystem::path& outputPath, VkExtent2D extent, uint32_t targetFps)
    {
        ResetSession();

        m_OutputPath = outputPath;
        m_OutputExtent = extent;
        m_TargetFps = (targetFps == 0) ? 30u : targetFps;

        if (m_OutputExtent.width == 0 || m_OutputExtent.height == 0)
        {
            TR_CORE_WARN("Video encoder rejected begin request because the extent was invalid.");

            return false;
        }

        if (!InitialiseCodec())
        {
            TR_CORE_WARN("Video encoder rejected begin request because codec initialisation failed.");

            return false;
        }

        m_SessionActive = true;
        m_SessionStartTime = std::chrono::system_clock::now();
        m_TargetFrameDuration = std::chrono::nanoseconds(1'000'000'000ull / static_cast<uint64_t>(m_TargetFps));
        TR_CORE_INFO("Video encoder session started at {} ({}x{}, {} FPS)", m_OutputPath.string(), m_OutputExtent.width, m_OutputExtent.height, m_TargetFps);

        return true;
    }

    bool VideoEncoder::SubmitFrame(const RecordedFrame& frame)
    {
        if (!m_SessionActive)
        {
            return false;
        }

        if (frame.m_Pixels.empty())
        {
            return false;
        }

        if (frame.m_Extent.width != m_OutputExtent.width || frame.m_Extent.height != m_OutputExtent.height)
        {
            TR_CORE_WARN("Video encoder rejected frame {} because the extent {}x{} does not match the session extent {}x{}.", frame.m_FrameIndex, frame.m_Extent.width, 
                frame.m_Extent.height, m_OutputExtent.width, m_OutputExtent.height);

            return false;
        }

        bool l_Written = false;
        if (m_UsingFfmpegContainer)
        {
            l_Written = WriteFrameToFfmpeg(frame);
        }
        else if (m_UsingY4mContainer)
        {
            l_Written = WriteFrameToY4m(frame);
        }
        else
        {
            TR_CORE_WARN("Video encoder session is inactive because no valid container was initialised. Frame {} was ignored.", frame.m_FrameIndex);

            return false;
        }

        if (!l_Written)
        {
            TR_CORE_WARN("Video encoder failed to write frame {} to {}.", frame.m_FrameIndex, m_OutputPath.string());

            return false;
        }

        ++m_FrameCounter;

        return true;
    }

    bool VideoEncoder::EndSession()
    {
        if (!m_SessionActive)
        {
            return false;
        }

        m_SessionActive = false;

        if (m_UsingFfmpegContainer && m_FfmpegCodecContext != nullptr)
        {
            // Flush any buffered frames before writing the trailer.
            const int32_t l_SendResult = avcodec_send_frame(m_FfmpegCodecContext, nullptr);
            if (l_SendResult >= 0)
            {
                while (true)
                {
                    const int32_t l_ReceiveResult = avcodec_receive_packet(m_FfmpegCodecContext, m_FfmpegPacket);
                    if (l_ReceiveResult == AVERROR(EAGAIN) || l_ReceiveResult == AVERROR_EOF)
                    {
                        break;
                    }

                    if (l_ReceiveResult < 0)
                    {
                        TR_CORE_WARN("Video encoder failed to flush buffered packets for {}.", m_OutputPath.string());

                        break;
                    }

                    m_FfmpegPacket->stream_index = m_FfmpegStream->index;
                    av_packet_rescale_ts(m_FfmpegPacket, m_FfmpegCodecContext->time_base, m_FfmpegStream->time_base);
                    av_write_frame(m_FfmpegFormatContext, m_FfmpegPacket);
                    av_packet_unref(m_FfmpegPacket);
                }
            }

            av_write_trailer(m_FfmpegFormatContext);
        }
        else if (m_OutputStream.is_open())
        {
            m_OutputStream.flush();
            m_OutputStream.close();
        }

        TR_CORE_INFO("Video encoder finalized output at {} ({} frames)", m_OutputPath.string(), m_FrameCounter);

        ResetSession();

        return true;
    }

    bool VideoEncoder::InitialiseCodec()
    {
        // Validate extension to guard against writing raw buffers to unsupported containers.
        const std::string l_Extension = m_OutputPath.extension().string();
        std::string l_NormalisedExtension = l_Extension;
        std::transform(l_NormalisedExtension.begin(), l_NormalisedExtension.end(), l_NormalisedExtension.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });

        const bool l_UsingFFmpeg = (l_NormalisedExtension == ".mp4" || l_NormalisedExtension == ".mov" || l_NormalisedExtension == ".avi");

        if (l_UsingFFmpeg)
        {
            // Use FFmpeg to handle compressed containers on supported platforms.
            m_UsingFfmpegContainer = InitialiseFfmpegEncoder();

            if (!m_UsingFfmpegContainer)
            {
                return false;
            }

            return true;
        }

        if (l_NormalisedExtension != ".y4m")
        {
            TR_CORE_WARN("Video encoder only supports Y4M output. Please choose a .y4m extension instead of '{}'.", l_NormalisedExtension);

            return false;
        }

        m_OutputStream.open(m_OutputPath, std::ios::binary | std::ios::trunc);
        if (!m_OutputStream.is_open())
        {
            TR_CORE_WARN("Video encoder could not open output file {}", m_OutputPath.string());

            return false;
        }

        m_UsingY4mContainer = WriteY4mHeader();

        return m_UsingY4mContainer;
    }

    bool VideoEncoder::InitialiseFfmpegEncoder()
    {
        // Prepare FFmpeg contexts for writing an H.264 stream to common containers.
        const std::string l_OutputString = m_OutputPath.string();

        const int32_t l_FormatResult = avformat_alloc_output_context2(&m_FfmpegFormatContext, nullptr, nullptr, l_OutputString.c_str());
        if (l_FormatResult < 0 || m_FfmpegFormatContext == nullptr)
        {
            TR_CORE_WARN("Video encoder could not allocate an FFmpeg format context for {}.", l_OutputString);

            CleanupFfmpegEncoder();
            return false;
        }

        const AVCodec* l_Codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (l_Codec == nullptr)
        {
            TR_CORE_WARN("Video encoder could not locate the H.264 encoder.");

            CleanupFfmpegEncoder();
            return false;
        }

        m_FfmpegStream = avformat_new_stream(m_FfmpegFormatContext, l_Codec);
        if (m_FfmpegStream == nullptr)
        {
            TR_CORE_WARN("Video encoder could not create an FFmpeg stream for {}.", l_OutputString);

            CleanupFfmpegEncoder();
            return false;
        }

        m_FfmpegCodecContext = avcodec_alloc_context3(l_Codec);
        if (m_FfmpegCodecContext == nullptr)
        {
            TR_CORE_WARN("Video encoder could not allocate an FFmpeg codec context for {}.", l_OutputString);

            CleanupFfmpegEncoder();
            return false;
        }

        m_FfmpegCodecContext->codec_id = AV_CODEC_ID_H264;
        m_FfmpegCodecContext->width = static_cast<int32_t>(m_OutputExtent.width);
        m_FfmpegCodecContext->height = static_cast<int32_t>(m_OutputExtent.height);
        m_FfmpegCodecContext->time_base = { 1, static_cast<int32_t>(m_TargetFps) };
        m_FfmpegCodecContext->framerate = { static_cast<int32_t>(m_TargetFps), 1 };
        m_FfmpegCodecContext->bit_rate = 8'000'000;
        m_FfmpegCodecContext->gop_size = static_cast<int32_t>(m_TargetFps);
        m_FfmpegCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;

        if (m_FfmpegFormatContext->oformat != nullptr && (m_FfmpegFormatContext->oformat->flags & AVFMT_GLOBALHEADER) != 0)
        {
            m_FfmpegCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        int32_t l_Result = avcodec_open2(m_FfmpegCodecContext, l_Codec, nullptr);
        if (l_Result < 0)
        {
            TR_CORE_WARN("Video encoder could not open the H.264 encoder for {} (error {}).", l_OutputString, l_Result);

            CleanupFfmpegEncoder();
            return false;
        }

        l_Result = avcodec_parameters_from_context(m_FfmpegStream->codecpar, m_FfmpegCodecContext);
        if (l_Result < 0)
        {
            TR_CORE_WARN("Video encoder could not copy codec parameters for {} (error {}).", l_OutputString, l_Result);

            CleanupFfmpegEncoder();
            return false;
        }

        m_FfmpegStream->time_base = m_FfmpegCodecContext->time_base;

        if (!(m_FfmpegFormatContext->oformat->flags & AVFMT_NOFILE))
        {
            l_Result = avio_open(&m_FfmpegFormatContext->pb, l_OutputString.c_str(), AVIO_FLAG_WRITE);
            if (l_Result < 0)
            {
                TR_CORE_WARN("Video encoder could not open {} for writing (error {}).", l_OutputString, l_Result);

                CleanupFfmpegEncoder();
                return false;
            }
        }

        l_Result = avformat_write_header(m_FfmpegFormatContext, nullptr);
        if (l_Result < 0)
        {
            TR_CORE_WARN("Video encoder could not write the FFmpeg header for {} (error {}).", l_OutputString, l_Result);

            CleanupFfmpegEncoder();
            return false;
        }

        m_FfmpegSwsContext = sws_getContext(static_cast<int32_t>(m_OutputExtent.width), static_cast<int32_t>(m_OutputExtent.height), AV_PIX_FMT_RGBA,
            static_cast<int32_t>(m_OutputExtent.width), static_cast<int32_t>(m_OutputExtent.height), m_FfmpegCodecContext->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (m_FfmpegSwsContext == nullptr)
        {
            TR_CORE_WARN("Video encoder could not create a scaling context for {}.", l_OutputString);

            CleanupFfmpegEncoder();
            return false;
        }

        m_FfmpegFrame = av_frame_alloc();
        if (m_FfmpegFrame == nullptr)
        {
            TR_CORE_WARN("Video encoder could not allocate a frame for {}.", l_OutputString);

            CleanupFfmpegEncoder();
            return false;
        }

        m_FfmpegFrame->format = m_FfmpegCodecContext->pix_fmt;
        m_FfmpegFrame->width = m_FfmpegCodecContext->width;
        m_FfmpegFrame->height = m_FfmpegCodecContext->height;
        l_Result = av_frame_get_buffer(m_FfmpegFrame, 32);
        if (l_Result < 0)
        {
            TR_CORE_WARN("Video encoder could not allocate frame buffers for {} (error {}).", l_OutputString, l_Result);

            CleanupFfmpegEncoder();
            return false;
        }

        m_FfmpegPacket = av_packet_alloc();
        if (m_FfmpegPacket == nullptr)
        {
            TR_CORE_WARN("Video encoder could not allocate a packet for {}.", l_OutputString);

            CleanupFfmpegEncoder();
            return false;
        }

        m_UsingFfmpegContainer = true;

        TR_CORE_INFO("Video encoder configured FFmpeg H.264 output at {}x{} ({} FPS) to {}.", m_OutputExtent.width, m_OutputExtent.height, m_TargetFps, l_OutputString);

        return true;
    }

    bool VideoEncoder::WriteY4mHeader()
    {
        // Provide a simple container header so the output is playable by standard Y4M readers.
        const uint64_t l_FpsNumerator = static_cast<uint64_t>(m_TargetFps);
        const uint64_t l_FpsDenominator = 1;
        m_OutputStream << "YUV4MPEG2 W" << m_OutputExtent.width
            << " H" << m_OutputExtent.height
            << " F" << l_FpsNumerator << ":" << l_FpsDenominator
            << " Ip A0:0 C444" << '\n';

        if (!m_OutputStream.good())
        {
            return false;
        }

        return true;
    }

    bool VideoEncoder::WriteFrameToY4m(const RecordedFrame& frame)
    {
        // Prepend each frame with the required header marker for Y4M streams.
        m_OutputStream << "FRAME" << '\n';

        if (!m_OutputStream.good())
        {
            return false;
        }

        // Convert RGBA data to YUV444 planar layout so that each frame is playable.
        std::vector<uint8_t> l_YuvBuffer;
        l_YuvBuffer.resize(static_cast<size_t>(frame.m_Extent.width) * static_cast<size_t>(frame.m_Extent.height) * 3ull);
        ConvertRgbaToYuv444(frame.m_Pixels, frame.m_Extent, l_YuvBuffer);

        m_OutputStream.write(reinterpret_cast<const char*>(l_YuvBuffer.data()), static_cast<std::streamsize>(l_YuvBuffer.size()));

        // Track timing to ensure frames are emitted with a consistent cadence.
        const std::chrono::system_clock::time_point l_CaptureTime = (frame.m_Timestamp.time_since_epoch().count() == 0)
            ? std::chrono::system_clock::now()
            : frame.m_Timestamp;
        const std::chrono::nanoseconds l_Elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(l_CaptureTime - m_SessionStartTime);
        const uint64_t l_ExpectedFrames = static_cast<uint64_t>(l_Elapsed / m_TargetFrameDuration);
        if (l_ExpectedFrames > (m_FrameCounter + 1))
        {
            TR_CORE_WARN("Video encoder detected timing drift. Captured {} frames but {} were expected for the elapsed time. The output will continue using the provided cadence.", m_FrameCounter + 1, l_ExpectedFrames);
        }

        return m_OutputStream.good();
    }

    bool VideoEncoder::WriteFrameToFfmpeg(const RecordedFrame& frame)
    {
        if (m_FfmpegCodecContext == nullptr || m_FfmpegFrame == nullptr || m_FfmpegPacket == nullptr || m_FfmpegSwsContext == nullptr)
        {
            return false;
        }

        int32_t l_Result = av_frame_make_writable(m_FfmpegFrame);
        if (l_Result < 0)
        {
            TR_CORE_WARN("Video encoder could not prepare the frame buffer for writing (error {}).", l_Result);

            return false;
        }

        uint8_t* l_Data[4] = {};
        int32_t l_Linesize[4] = {};
        l_Data[0] = const_cast<uint8_t*>(frame.m_Pixels.data());
        l_Linesize[0] = static_cast<int32_t>(m_OutputExtent.width * 4u);

        sws_scale(m_FfmpegSwsContext, l_Data, l_Linesize, 0, static_cast<int32_t>(m_OutputExtent.height), m_FfmpegFrame->data, m_FfmpegFrame->linesize);

        m_FfmpegFrame->pts = static_cast<int64_t>(m_FrameCounter);

        l_Result = avcodec_send_frame(m_FfmpegCodecContext, m_FfmpegFrame);
        if (l_Result < 0)
        {
            TR_CORE_WARN("Video encoder could not send frame {} to the encoder (error {}).", frame.m_FrameIndex, l_Result);

            return false;
        }

        while (l_Result >= 0)
        {
            l_Result = avcodec_receive_packet(m_FfmpegCodecContext, m_FfmpegPacket);
            if (l_Result == AVERROR(EAGAIN) || l_Result == AVERROR_EOF)
            {
                break;
            }
            else if (l_Result < 0)
            {
                TR_CORE_WARN("Video encoder could not receive packet for frame {} (error {}).", frame.m_FrameIndex, l_Result);

                return false;
            }

            m_FfmpegPacket->stream_index = m_FfmpegStream->index;
            av_packet_rescale_ts(m_FfmpegPacket, m_FfmpegCodecContext->time_base, m_FfmpegStream->time_base);

            l_Result = av_write_frame(m_FfmpegFormatContext, m_FfmpegPacket);
            av_packet_unref(m_FfmpegPacket);
            if (l_Result < 0)
            {
                TR_CORE_WARN("Video encoder could not write packet for frame {} (error {}).", frame.m_FrameIndex, l_Result);

                return false;
            }
        }

        return true;
    }

    void VideoEncoder::ResetSession()
    {
        if (m_OutputStream.is_open())
        {
            m_OutputStream.close();
        }

        CleanupFfmpegEncoder();

        m_SessionActive = false;
        m_OutputPath.clear();
        m_OutputExtent = { 0, 0 };
        m_TargetFps = 30;
        m_UsingY4mContainer = false;
        m_UsingFfmpegContainer = false;
        m_SessionStartTime = {};
        m_TargetFrameDuration = {};
        m_FrameCounter = 0;
    }

    void VideoEncoder::CleanupFfmpegEncoder()
    {
        if (m_FfmpegPacket != nullptr)
        {
            av_packet_free(&m_FfmpegPacket);
            m_FfmpegPacket = nullptr;
        }

        if (m_FfmpegFrame != nullptr)
        {
            av_frame_free(&m_FfmpegFrame);
            m_FfmpegFrame = nullptr;
        }

        if (m_FfmpegCodecContext != nullptr)
        {
            avcodec_free_context(&m_FfmpegCodecContext);
            m_FfmpegCodecContext = nullptr;
        }

        if (m_FfmpegFormatContext != nullptr)
        {
            if (!(m_FfmpegFormatContext->oformat->flags & AVFMT_NOFILE) && m_FfmpegFormatContext->pb != nullptr)
            {
                avio_closep(&m_FfmpegFormatContext->pb);
            }

            avformat_free_context(m_FfmpegFormatContext);
            m_FfmpegFormatContext = nullptr;
        }

        if (m_FfmpegSwsContext != nullptr)
        {
            sws_freeContext(m_FfmpegSwsContext);
            m_FfmpegSwsContext = nullptr;
        }

        m_FfmpegStream = nullptr;
    }

    uint8_t VideoEncoder::ClampChannel(double value)
    {
        if (value < 0.0)
        {
            return 0;
        }

        if (value > 255.0)
        {
            return 255;
        }

        return static_cast<uint8_t>(value);
    }

    void VideoEncoder::ConvertRgbaToYuv444(const std::vector<uint8_t>& a_InputRgba, VkExtent2D a_Extent, std::vector<uint8_t>& a_OutYuv)
    {
        // Basic RGBA to YUV444 conversion. Alpha channel is discarded because the container expects opaque frames.
        const size_t l_PixelCount = static_cast<size_t>(a_Extent.width) * static_cast<size_t>(a_Extent.height);
        for (size_t l_Index = 0; l_Index < l_PixelCount; ++l_Index)
        {
            const size_t l_RgbaOffset = l_Index * 4ull;
            const uint8_t l_R = a_InputRgba[l_RgbaOffset + 0];
            const uint8_t l_G = a_InputRgba[l_RgbaOffset + 1];
            const uint8_t l_B = a_InputRgba[l_RgbaOffset + 2];

            // Use Rec. 601 full range conversion.
            const double l_Y = 0.299 * static_cast<double>(l_R) + 0.587 * static_cast<double>(l_G) + 0.114 * static_cast<double>(l_B);
            const double l_U = -0.169 * static_cast<double>(l_R) - 0.331 * static_cast<double>(l_G) + 0.5 * static_cast<double>(l_B) + 128.0;
            const double l_V = 0.5 * static_cast<double>(l_R) - 0.419 * static_cast<double>(l_G) - 0.081 * static_cast<double>(l_B) + 128.0;

            a_OutYuv[l_Index] = ClampChannel(l_Y);
            a_OutYuv[l_PixelCount + l_Index] = ClampChannel(l_U);
            a_OutYuv[(2 * l_PixelCount) + l_Index] = ClampChannel(l_V);
        }
    }
}