#pragma once

#include <deque>
#include <filesystem>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <thread>

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
            ~FrameDatasetRecorder();

            /**
             * @brief Set the root directory that will contain the capture artefacts.
             */
            void SetCaptureDirectory(const std::filesystem::path& directory);

            /**
             * @brief Toggle whether capture should persist tensors to disk.
             */
            void EnableCapture(bool enable);

            /**
             * @brief Adjust how often frames are sampled so capture can reduce pressure on the main thread.
             */
            void SetSampleInterval(uint32_t interval);

            /**
             * @brief Inspect the current sample interval used by the recorder.
             */
            uint32_t GetSampleInterval() const { return m_CaptureSampleInterval; }

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
            enum class JobType
            {
                Input,
                Output
            };

            struct CaptureJob
            {
                JobType m_Type = JobType::Input;                        ///< Identifies which persistence path to execute.
                uint64_t m_Index = 0;                                   ///< Monotonic identifier shared between input and output files.
                std::filesystem::path m_TargetPath{};                   ///< Destination file path for the NPY payload.
                std::filesystem::path m_MetadataPath{};                 ///< Destination for the JSON metadata (input frames only).
                std::vector<float> m_Data;                              ///< Copy of the tensor data so background threads own the memory.
                std::vector<int64_t> m_Shape;                           ///< Shape description accompanying the tensor.
                VkExtent2D m_Extent{ 0, 0 };                            ///< Captured extent for metadata files.
                uint32_t m_ChannelCount = 0;                            ///< Channel count for metadata files.
                std::string m_ColorOrder;                               ///< Channel ordering label persisted to metadata.
                bool m_Normalised = false;                              ///< Tracks whether the tensor was normalised before capture.
            };

            struct PendingSample
            {
                uint64_t m_Index = 0;                                ///< Monotonic identifier shared between input and output files.
            };

            std::filesystem::path m_CaptureDirectory{};              ///< Root folder used for dataset capture.
            bool m_CaptureEnabled = false;                           ///< Flag indicating whether capture is active.
            bool m_DirectoryPrepared = false;                        ///< Tracks whether the directory hierarchy exists.
            uint64_t m_NextSampleIndex = 0;                          ///< Counter used when naming capture files.
            std::deque<PendingSample> m_PendingSamples;              ///< Queue linking inputs to their eventual outputs.
            uint64_t m_FrameCounter = 0;                             ///< Counter used to enforce the sampling interval.
            uint32_t m_CaptureSampleInterval = 1;                    ///< Interval that determines how often frames are captured.
            std::deque<CaptureJob> m_WriteQueue;                     ///< Pending jobs awaiting background persistence.
            std::mutex m_QueueMutex;                                 ///< Protects the write queue from concurrent access.
            std::condition_variable m_QueueCondition;                ///< Signals the worker thread when new jobs arrive.
            std::thread m_WorkerThread;                              ///< Background worker that performs disk writes.
            bool m_StopWorker = false;                               ///< Signals the worker thread to finish after draining jobs.
            bool m_WorkerRunning = false;                            ///< Tracks whether the worker thread is active.

            std::filesystem::path BuildInputPath(uint64_t index) const;
            std::filesystem::path BuildOutputPath(uint64_t index) const;
            std::filesystem::path BuildMetadataPath(uint64_t index) const;
            void EnsureDirectoryReady();
            std::string BuildShapeString(std::span<const int64_t> shape) const;
            bool WriteNpyFile(const std::filesystem::path& path, std::span<const float> data, std::span<const int64_t> shape) const;
            void WriteMetadataFile(const std::filesystem::path& path, VkExtent2D extent, uint32_t channelCount, std::span<const int64_t> tensorShape,
                std::string_view colorOrder, bool normalised) const;
            void StartWorker();
            void StopWorker();
            void EnqueueJob(CaptureJob job);
            void ProcessJob(CaptureJob& job);
            bool ShouldCaptureThisFrame();
        };
    }
}