#include "Renderer/VideoEncoder.h"

#include <fstream>

namespace Trident
{
    bool VideoEncoder::BeginSession(const std::filesystem::path& outputPath, VkExtent2D extent, uint32_t targetFps)
    {
        m_OutputPath = outputPath;
        m_OutputExtent = extent;
        m_TargetFps = targetFps;

        if (m_OutputExtent.width == 0 || m_OutputExtent.height == 0)
        {
            TR_CORE_WARN("Video encoder rejected begin request because the extent was invalid.");

            return false;
        }

        m_SessionActive = true;
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

        // Placeholder implementation: append the raw bytes to disk so debug sessions still capture output.
        std::ofstream l_Stream(m_OutputPath, std::ios::binary | std::ios::app);
        if (!l_Stream.is_open())
        {
            TR_CORE_WARN("Video encoder could not open output file {}", m_OutputPath.string());

            return false;
        }

        l_Stream.write(reinterpret_cast<const char*>(frame.m_Pixels.data()), static_cast<std::streamsize>(frame.m_Pixels.size()));

        return true;
    }

    bool VideoEncoder::EndSession()
    {
        if (!m_SessionActive)
        {
            return false;
        }

        m_SessionActive = false;
        TR_CORE_INFO("Video encoder finalized output at {}", m_OutputPath.string());

        return true;
    }
}