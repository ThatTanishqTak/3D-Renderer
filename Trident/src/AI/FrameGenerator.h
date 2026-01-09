#pragma once

#include "AI/OnnxRuntimeContext.h"

#include <filesystem>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Trident
{
    namespace AI
    {
        /**
         * @brief Helper responsible for dispatching rendered frames to an ONNX model.
         *
         * The renderer frequently needs to post-process completed frames using machine learning models
         * (for example to drive interpolation or denoising passes). This class centralises the wiring to
         * the ONNX runtime so the main rendering loop can stay focused on GPU work. By caching tensor
         * metadata during initialisation we avoid redundant queries every frame and keep the per-frame
         * hot path lightweight. Future improvements could extend the helper with batching or asynchronous
         * execution once the engine grows more demanding AI workloads.
         */
        class FrameGenerator
        {
        public:
            FrameGenerator();
            ~FrameGenerator();

            /**
             * @brief Load the requested model and cache its tensor metadata.
             *
             * @param modelPath Absolute or relative path to the ONNX model.
             * @return true when the model is ready to accept frame data.
             */
            bool Initialise(const std::filesystem::path& modelPath);

            /**
             * @brief Returns true once Initialise has successfully loaded the target model.
             */
            bool IsInitialised() const { return m_IsInitialised; }

            /**
             * @brief Enqueue the latest frame so the background worker can process it asynchronously.
             *
             * @param frameData Input tensor representing the rendered frame.
             * @return true when the job was accepted by the queue.
             */
            bool ProcessFrame(std::span<const float> frameData);

            /**
             * @brief Attempt to retrieve the most recent output tensor produced by the worker thread.
             *
             * The method never blocks; callers can poll it each frame and only copy data when a new
             * inference result is available.
             */
            bool TryConsumeOutput(std::vector<float>& a_OutOutput);

            /**
             * @brief Expose a copy of the cached output buffer so the renderer can feed later passes.
             */
            std::vector<float> GetLastOutput() const;

            /**
             * @brief Provide callers with the expected input shape.
             */
            std::span<const int64_t> GetPrimaryInputShape() const;

            /**
             * @brief Provide callers with the primary output shape expected from the model.
             */
            std::span<const int64_t> GetPrimaryOutputShape() const;

            /**
             * @brief Retrieve the most recent inference duration recorded by the worker thread.
             *
             * The timing value is captured immediately after the ONNX runtime completes a Run call. The getter
             * acquires the output mutex so external systems can safely sample the value while the worker thread
             * continues processing frames.
             */
            double GetLastInferenceMilliseconds() const;

            /**
             * @brief Calculate the running average inference time across all completed jobs.
             */
            double GetAverageInferenceMilliseconds() const;

            /**
             * @brief Return the total number of successful inference jobs executed since initialisation.
             */
            uint64_t GetCompletedInferenceCount() const;

            /**
             * @brief Report how many frame jobs are currently waiting in the queue.
             */
            size_t GetPendingJobCount() const;

        private:
            /**
             * @brief Lightweight job object used by the background worker when dispatching inference.
             */
            struct FrameJob
            {
                std::vector<float> m_InputTensor; // Flattened tensor data copied from the renderer readback.
            };

            struct TensorBinding
            {
                std::string m_Name{};                // Graph binding name used during inference runs.
                std::vector<int64_t> m_Shape{};      // Cached tensor shape describing the expected dimensions.
                size_t m_ElementCount = 0;           // Flattened element count derived from the shape for validation.
            };

            bool CacheModelBindings(const Ort::Session& session);
            void ResetState();
            void WorkerLoop();

        private:
            std::string m_ModelKey{};                                   // Identifier supplied to OnnxRuntimeContext.
            AI::OnnxRuntimeContext* m_RuntimeContext = nullptr;          // Pointer to the shared runtime controller.
            bool m_IsInitialised = false;                                // Guard flag checked before scheduling inference.
            std::vector<TensorBinding> m_InputBindings;                  // Cached description of model inputs.
            std::vector<TensorBinding> m_OutputBindings;                 // Cached description of model outputs.
            std::vector<float> m_LastOutputTensor;                       // Flattened buffer storing the latest AI result.
            std::vector<float> m_InputStagingBuffer;                     // Preallocated CPU buffer used to stage incoming frames.
            std::deque<std::vector<float>> m_InputBufferPool;            // Reusable buffers returned by the worker thread after inference completes.
            std::deque<std::vector<float>> m_OutputBufferPool;           // Reusable buffers for output aggregation to reduce allocations.
            std::deque<FrameJob> m_PendingJobs;                          // Queue of frames waiting to be processed asynchronously.
            std::deque<std::vector<float>> m_CompletedOutputs;           // Queue of freshly generated AI outputs awaiting consumption.
            mutable std::mutex m_QueueMutex;                             // Guards pending job access. Marked mutable so const inspectors can lock safely.
            mutable std::mutex m_OutputMutex;                            // Guards completed output queue and cached tensor.
            std::condition_variable m_QueueCondition;                    // Signals the worker thread when new jobs arrive.
            std::thread m_WorkerThread;                                  // Dedicated worker responsible for running inference.
            bool m_WorkerShouldStop = false;                             // Latch toggled during shutdown so the worker exits gracefully.
            double m_LastInferenceMilliseconds = 0.0;                    // Timing for the most recent inference run measured in milliseconds.
            double m_TotalInferenceMilliseconds = 0.0;                   // Accumulated duration of all completed inference runs.
            uint64_t m_CompletedInferenceCount = 0;                      // Number of jobs that produced an output tensor.
            size_t m_PendingJobCount = 0;                                // Cached size of the pending job queue for quick inspection.
        };
    }
}