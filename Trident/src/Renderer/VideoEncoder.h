#pragma once

#include "Core/Utilities.h"

#include <vector>
#include <filesystem>
#include <chrono>

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
            std::vector<uint8_t> m_Pixels; ///< Raw RGBA byte payload for the frame.
            VkExtent2D m_Extent{ 0, 0 };   ///< Resolution of the supplied frame.
            std::chrono::system_clock::time_point m_Timestamp{}; ///< Capture timestamp.
            uint32_t m_FrameIndex = 0;     ///< Swapchain image index used for the frame.
            uint32_t m_ViewportId = 0;     ///< Viewport identifier associated with the frame.
        };

        VideoEncoder() = default;

        bool BeginSession(const std::filesystem::path& outputPath, VkExtent2D extent, uint32_t targetFps);
        bool SubmitFrame(const RecordedFrame& frame);
        bool EndSession();

        bool IsSessionActive() const { return m_SessionActive; }

    private:
        bool m_SessionActive = false;
        std::filesystem::path m_OutputPath{};
        VkExtent2D m_OutputExtent{ 0, 0 };
        uint32_t m_TargetFps = 30;
    };
}