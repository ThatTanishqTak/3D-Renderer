#include "AI/FrameDatasetRecorder.h"

#include "Core/Utilities.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <string_view>
#include <system_error>

namespace Trident
{
    namespace AI
    {
        FrameDatasetRecorder::FrameDatasetRecorder() = default;

        void FrameDatasetRecorder::SetCaptureDirectory(const std::filesystem::path& directory)
        {
            m_CaptureDirectory = directory;
            m_DirectoryPrepared = false;
        }

        void FrameDatasetRecorder::EnableCapture(bool enable)
        {
            m_CaptureEnabled = enable;
            if (!m_CaptureEnabled)
            {
                Reset();
            }
        }

        void FrameDatasetRecorder::Reset()
        {
            m_PendingSamples.clear();
            m_NextSampleIndex = 0;
        }

        void FrameDatasetRecorder::RecordInputFrame(std::span<const float> frameData, VkExtent2D extent, uint32_t channelCount, std::span<const int64_t> tensorShape)
        {
            if (!m_CaptureEnabled)
            {
                return;
            }

            if (extent.width == 0 || extent.height == 0 || channelCount == 0)
            {
                TR_CORE_WARN("FrameDatasetRecorder rejected a frame because the extent or channel count was invalid ({}x{}, channels={}).", extent.width, extent.height, channelCount);
                return;
            }

            const size_t l_ElementCount = static_cast<size_t>(extent.width) * static_cast<size_t>(extent.height) * static_cast<size_t>(channelCount);
            if (frameData.size() != l_ElementCount)
            {
                TR_CORE_WARN("FrameDatasetRecorder expected {} elements but received {}. Skipping capture for this frame.", l_ElementCount, frameData.size());
                return;
            }

            EnsureDirectoryReady();

            const uint64_t l_Index = m_NextSampleIndex++;
            const std::filesystem::path l_InputPath = BuildInputPath(l_Index);
            const std::filesystem::path l_MetadataPath = BuildMetadataPath(l_Index);

            // Ensure the persisted tensor always reflects the canonical NHWC layout with an explicit batch dimension.
            std::array<int64_t, 4> l_DefaultShape =
            {
                1,
                static_cast<int64_t>(extent.height),
                static_cast<int64_t>(extent.width),
                static_cast<int64_t>(channelCount)
            };
            const std::span<const int64_t> l_ShapeSpan = tensorShape.empty() ? std::span<const int64_t>{ l_DefaultShape } : tensorShape;

            if (!WriteNpyFile(l_InputPath, frameData, l_ShapeSpan))
            {
                TR_CORE_ERROR("FrameDatasetRecorder failed to persist frame input '{}'", l_InputPath.string());
                return;
            }

            WriteMetadataFile(l_MetadataPath, extent, channelCount, l_ShapeSpan, "BGRA", true);

            PendingSample l_Pending{};
            l_Pending.m_Index = l_Index;
            m_PendingSamples.push_back(l_Pending);
        }

        void FrameDatasetRecorder::RecordAiOutput(std::span<const float> outputData, std::span<const int64_t> outputShape)
        {
            if (!m_CaptureEnabled)
            {
                return;
            }

            if (outputData.empty())
            {
                TR_CORE_WARN("FrameDatasetRecorder received an empty AI output. The sample will be skipped.");
                return;
            }

            if (m_PendingSamples.empty())
            {
                TR_CORE_WARN("FrameDatasetRecorder cannot match an AI output because no pending frame exists. Ensure inputs are captured before outputs.");
                return;
            }

            PendingSample l_Pending = m_PendingSamples.front();
            m_PendingSamples.pop_front();

            EnsureDirectoryReady();

            std::vector<int64_t> l_Shape;
            if (!outputShape.empty())
            {
                l_Shape.assign(outputShape.begin(), outputShape.end());
            }
            else
            {
                l_Shape = { static_cast<int64_t>(outputData.size()) };
            }

            // Normalise the output tensor shape so offline tooling can rely on an explicit batch dimension and channels-last layout.
            if (l_Shape.size() == 3)
            {
                l_Shape.insert(l_Shape.begin(), 1);
            }
            else if (l_Shape.size() >= 4)
            {
                l_Shape[0] = (l_Shape[0] <= 0) ? 1 : l_Shape[0];
            }
            else if (l_Shape.size() == 1)
            {
                l_Shape = { 1, l_Shape[0], 1, 1 };
            }

            const std::filesystem::path l_OutputPath = BuildOutputPath(l_Pending.m_Index);
            if (!WriteNpyFile(l_OutputPath, outputData, l_Shape))
            {
                TR_CORE_ERROR("FrameDatasetRecorder failed to persist AI output '{}'", l_OutputPath.string());
            }
        }

        std::filesystem::path FrameDatasetRecorder::BuildInputPath(uint64_t index) const
        {
            const std::filesystem::path l_InputDirectory = m_CaptureDirectory / "inputs";
            char l_Buffer[32];
            std::snprintf(l_Buffer, sizeof(l_Buffer), "frame_%06llu.npy", static_cast<unsigned long long>(index));
            return l_InputDirectory / l_Buffer;
        }

        std::filesystem::path FrameDatasetRecorder::BuildOutputPath(uint64_t index) const
        {
            const std::filesystem::path l_OutputDirectory = m_CaptureDirectory / "outputs";
            char l_Buffer[32];
            std::snprintf(l_Buffer, sizeof(l_Buffer), "ai_%06llu.npy", static_cast<unsigned long long>(index));
            return l_OutputDirectory / l_Buffer;
        }

        std::filesystem::path FrameDatasetRecorder::BuildMetadataPath(uint64_t index) const
        {
            const std::filesystem::path l_MetadataDirectory = m_CaptureDirectory / "metadata";
            char l_Buffer[32];
            std::snprintf(l_Buffer, sizeof(l_Buffer), "frame_%06llu.json", static_cast<unsigned long long>(index));
            return l_MetadataDirectory / l_Buffer;
        }

        void FrameDatasetRecorder::EnsureDirectoryReady()
        {
            if (m_DirectoryPrepared)
            {
                return;
            }

            if (m_CaptureDirectory.empty())
            {
                m_CaptureDirectory = std::filesystem::current_path() / "DatasetCapture";
            }

            const std::filesystem::path l_InputDirectory = m_CaptureDirectory / "inputs";
            const std::filesystem::path l_OutputDirectory = m_CaptureDirectory / "outputs";
            const std::filesystem::path l_MetadataDirectory = m_CaptureDirectory / "metadata";

            std::error_code l_Errors;
            std::filesystem::create_directories(l_InputDirectory, l_Errors);
            if (l_Errors)
            {
                TR_CORE_ERROR("FrameDatasetRecorder failed to prepare input directory '{}': {}", l_InputDirectory.string(), l_Errors.message());
            }
            l_Errors.clear();

            std::filesystem::create_directories(l_OutputDirectory, l_Errors);
            if (l_Errors)
            {
                TR_CORE_ERROR("FrameDatasetRecorder failed to prepare output directory '{}': {}", l_OutputDirectory.string(), l_Errors.message());
            }
            l_Errors.clear();

            std::filesystem::create_directories(l_MetadataDirectory, l_Errors);
            if (l_Errors)
            {
                TR_CORE_ERROR("FrameDatasetRecorder failed to prepare metadata directory '{}': {}", l_MetadataDirectory.string(), l_Errors.message());
            }

            m_DirectoryPrepared = true;
        }

        std::string FrameDatasetRecorder::BuildShapeString(std::span<const int64_t> shape) const
        {
            std::ostringstream l_Stream;
            l_Stream << '(';
            for (size_t it_Index = 0; it_Index < shape.size(); ++it_Index)
            {
                l_Stream << shape[it_Index];
                const bool l_IsLast = (it_Index + 1) == shape.size();
                if (!l_IsLast)
                {
                    l_Stream << ", ";
                }
            }

            if (shape.size() == 1)
            {
                l_Stream << ',';
            }

            l_Stream << ')';
            return l_Stream.str();
        }

        bool FrameDatasetRecorder::WriteNpyFile(const std::filesystem::path& path, std::span<const float> data, std::span<const int64_t> shape) const
        {
            if (shape.empty())
            {
                TR_CORE_WARN("FrameDatasetRecorder refused to write '{}' because the shape description was empty.", path.string());
                return false;
            }

            size_t l_ExpectedElements = 1;
            for (int64_t it_Dimension : shape)
            {
                if (it_Dimension <= 0)
                {
                    TR_CORE_WARN("FrameDatasetRecorder refused to write '{}' because a shape dimension was non-positive.", path.string());
                    return false;
                }

                l_ExpectedElements *= static_cast<size_t>(it_Dimension);
            }

            if (l_ExpectedElements != data.size())
            {
                TR_CORE_WARN("FrameDatasetRecorder refused to write '{}' because the shape describes {} elements but {} were provided.", path.string(), l_ExpectedElements, data.size());
                return false;
            }

            std::ofstream l_Stream(path, std::ios::binary | std::ios::trunc);
            if (!l_Stream.is_open())
            {
                TR_CORE_ERROR("FrameDatasetRecorder could not open '{}' for writing.", path.string());
                return false;
            }

            const char l_Magic[] = "\x93NUMPY";
            l_Stream.write(l_Magic, sizeof(l_Magic) - 1);

            const uint8_t l_VersionMajor = 1;
            const uint8_t l_VersionMinor = 0;
            l_Stream.write(reinterpret_cast<const char*>(&l_VersionMajor), sizeof(l_VersionMajor));
            l_Stream.write(reinterpret_cast<const char*>(&l_VersionMinor), sizeof(l_VersionMinor));

            const std::string l_ShapeString = BuildShapeString(shape);
            std::string l_Header = "{'descr': '<f4', 'fortran_order': False, 'shape': " + l_ShapeString + ", }";

            const size_t l_HeaderBase = 10; // magic (6) + version (2) + header length field (2)
            size_t l_HeaderLength = l_Header.size() + 1; // account for newline terminator
            const size_t l_Padding = (16 - ((l_HeaderBase + l_HeaderLength) % 16)) % 16;
            l_Header.append(l_Padding, ' ');
            l_Header.push_back('\n');
            l_HeaderLength = l_Header.size();

            if (l_HeaderLength > std::numeric_limits<uint16_t>::max())
            {
                TR_CORE_ERROR("FrameDatasetRecorder cannot write '{}' because the header exceeded the 16-bit length limit.", path.string());
                return false;
            }

            const uint16_t l_HeaderSize = static_cast<uint16_t>(l_HeaderLength);
            l_Stream.write(reinterpret_cast<const char*>(&l_HeaderSize), sizeof(l_HeaderSize));
            l_Stream.write(l_Header.data(), static_cast<std::streamsize>(l_Header.size()));

            l_Stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size() * sizeof(float)));
            l_Stream.close();

            return true;
        }

        void FrameDatasetRecorder::WriteMetadataFile(const std::filesystem::path& path, VkExtent2D extent, uint32_t channelCount, std::span<const int64_t> tensorShape,
            std::string_view colorOrder, bool normalised) const
        {
            std::ofstream l_Stream(path, std::ios::trunc);
            if (!l_Stream.is_open())
            {
                TR_CORE_WARN("FrameDatasetRecorder could not write metadata '{}'.", path.string());
                return;
            }

            // TODO: Extend the metadata schema with scene tags (lighting, motion, effects) to streamline future dataset curation.
            l_Stream << std::fixed << std::setprecision(0);
            l_Stream << "{\n";
            l_Stream << "  \"width\": " << extent.width << ",\n";
            l_Stream << "  \"height\": " << extent.height << ",\n";
            l_Stream << "  \"channels\": " << channelCount << ",\n";
            if (!tensorShape.empty())
            {
                l_Stream << "  \"layout\": \"" << BuildShapeString(tensorShape) << "\",\n";
            }
            l_Stream << "  \"channelsLast\": true,\n";
            l_Stream << "  \"colorOrder\": \"" << colorOrder << "\",\n";
            l_Stream << "  \"normalised\": " << (normalised ? "true" : "false") << "\n";
            l_Stream << "}\n";
        }
    }
}