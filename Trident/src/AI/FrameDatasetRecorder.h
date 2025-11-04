#pragma once

#include <deque>
#include <filesystem>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

namespace Trident
{
    namespace AI
    {
        /**
         * @brief Utility responsible for persisting rendered frames and AI outputs to disk for dataset creation.
         *
         * The recorder receives input tensors straight from the renderer and their matching AI outputs so
         * offline training pipelines can build a supervised dataset. It writes NumPy compatible files so Python
         * tooling can easily consume the captured information. Future improvements might include background I/O
         * threads to minimise stalls during heavy capture sessions.
         */
        class FrameDatasetRecorder
        {
        public:
            FrameDatasetRecorder();

            /**
             * @brief Set the root directory that will contain the capture artefacts.
             */
            void SetCaptureDirectory(const std::filesystem::path& directory);

            /**
             * @brief Toggle whether capture should persist tensors to disk.
             */
            void EnableCapture(bool enable);

            /**
             * @brief Clear pending samples and reset file numbering.
             */
            void Reset();

            /**
             * @brief Retrieve the directory currently used for capture output.
             */
            const std::filesystem::path& GetCaptureDirectory() const { return m_CaptureDirectory; }

            /**
             * @brief Record the frame that is about to be submitted to the AI system.
             */
            void RecordInputFrame(std::span<const float> frameData, VkExtent2D extent, uint32_t channelCount, std::span<const int64_t> tensorShape = {});

            /**
             * @brief Record the AI output tensor that corresponds to the next pending frame capture.
             */
            void RecordAiOutput(std::span<const float> outputData, std::span<const int64_t> outputShape);

        private:
            struct PendingSample
            {
                uint64_t m_Index = 0;                                ///< Monotonic identifier shared between input and output files.
            };

            std::filesystem::path m_CaptureDirectory{};              ///< Root folder used for dataset capture.
            bool m_CaptureEnabled = false;                           ///< Flag indicating whether capture is active.
            bool m_DirectoryPrepared = false;                        ///< Tracks whether the directory hierarchy exists.
            uint64_t m_NextSampleIndex = 0;                          ///< Counter used when naming capture files.
            std::deque<PendingSample> m_PendingSamples;              ///< Queue linking inputs to their eventual outputs.

            std::filesystem::path BuildInputPath(uint64_t index) const;
            std::filesystem::path BuildOutputPath(uint64_t index) const;
            std::filesystem::path BuildMetadataPath(uint64_t index) const;
            void EnsureDirectoryReady();
            std::string BuildShapeString(std::span<const int64_t> shape) const;
            bool WriteNpyFile(const std::filesystem::path& path, std::span<const float> data, std::span<const int64_t> shape) const;
            void WriteMetadataFile(const std::filesystem::path& path, VkExtent2D extent, uint32_t channelCount, std::span<const int64_t> tensorShape, 
                std::string_view colorOrder, bool normalised) const;
        };
    }
}