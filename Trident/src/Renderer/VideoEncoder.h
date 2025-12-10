#pragma once

#include "Core/Utilities.h"

#include <vector>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

extern "C"
{
    struct AVCodecContext;
    struct AVFormatContext;
    struct AVFrame;
    struct AVPacket;
    struct AVStream;
    struct SwsContext;
}

namespace Trident
{
    /**
     * @brief Minimal helper that streams raw viewport frames to a video container.
     */
    class VideoEncoder
    {
    public:
        struct RecordedFrame
        {
            std::vector<uint8_t> m_Pixels; // Raw RGBA byte payload for the frame.
            VkExtent2D m_Extent{ 0, 0 };   // Resolution of the supplied frame.
            std::chrono::system_clock::time_point m_Timestamp{}; // Capture timestamp.
            uint32_t m_FrameIndex = 0;     // Swapchain image index used for the frame.
            uint32_t m_ViewportId = 0;     // Viewport identifier associated with the frame.
        };

        VideoEncoder() = default;

        bool BeginSession(const std::filesystem::path& outputPath, VkExtent2D extent, uint32_t targetFps);
        bool SubmitFrame(const RecordedFrame& frame);
        bool EndSession();

        bool IsSessionActive() const { return m_SessionActive; }

    private:
        bool InitialiseCodec();
        bool InitialiseFfmpegEncoder();
        bool WriteY4mHeader();
        bool WriteFrameToY4m(const RecordedFrame& frame);
        bool WriteFrameToFfmpeg(const RecordedFrame& frame);
        bool StartFfmpegWorker();
        void StopFfmpegWorker();
        void FfmpegWorkerLoop();
        void CleanupFfmpegEncoder();
        void ResetSession();
        static uint8_t ClampChannel(double value);
        static void ConvertRgbaToYuv444(const std::vector<uint8_t>& inputRGBA, VkExtent2D extent, std::vector<uint8_t>& outYUV);

        bool m_SessionActive = false;
        std::filesystem::path m_OutputPath{};
        VkExtent2D m_OutputExtent{ 0, 0 };
        uint32_t m_TargetFps = 24;
        bool m_UsingY4mContainer = false;
        bool m_UsingFfmpegContainer = false;
        std::ofstream m_OutputStream{};
        std::chrono::system_clock::time_point m_SessionStartTime{};
        std::chrono::nanoseconds m_TargetFrameDuration{};
        uint64_t m_FrameCounter = 0;

        AVFormatContext* m_FfmpegFormatContext = nullptr;
        AVCodecContext* m_FfmpegCodecContext = nullptr;
        AVStream* m_FfmpegStream = nullptr;
        SwsContext* m_FfmpegSwsContext = nullptr;
        AVFrame* m_FfmpegFrame = nullptr;
        AVPacket* m_FfmpegPacket = nullptr;

        std::thread m_FfmpegWorkerThread{};
        std::condition_variable m_FfmpegQueueCondition{};
        std::mutex m_FfmpegQueueMutex{};
        std::queue<RecordedFrame> m_FfmpegFrameQueue{};
        std::condition_variable m_FfmpegWorkerStateCondition{};
        std::mutex m_FfmpegWorkerStateMutex{};
        bool m_FfmpegWorkerShouldStop = false;
        bool m_FfmpegWorkerRunning = false;
        bool m_FfmpegWorkerInitialised = false;
        bool m_FfmpegWorkerReady = false;
        bool m_FfmpegWorkerSessionSuccess = true;
    };
}