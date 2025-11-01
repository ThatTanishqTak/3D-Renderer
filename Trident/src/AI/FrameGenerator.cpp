#include "AI/FrameGenerator.h"

#include "Core/Utilities.h"

#include <chrono>
#include <cstring>
#include <stop_token>

namespace Trident
{
    namespace AI
    {
        FrameGenerator::FrameGenerator() : m_Worker(&FrameGenerator::WorkerMain, this)
        {
        }

        FrameGenerator::~FrameGenerator()
        {
            if (m_Worker.joinable())
            {
                m_Worker.request_stop();
                m_WorkAvailable.notify_all();
                m_ResultAvailable.notify_all();
            }
        }

        void FrameGenerator::EnableCUDA(bool enableCUDA)
        {
            m_Runtime.EnableCUDA(enableCUDA);
        }

        void FrameGenerator::EnableCPUFallback(bool enableCPUFallback)
        {
            m_Runtime.EnableCPUFallback(enableCPUFallback);
        }

        bool FrameGenerator::LoadModel(const std::string& modelPath)
        {
            return m_Runtime.LoadModel(modelPath);
        }

        bool FrameGenerator::EnqueueFrame(const FrameDescriptors& descriptors, const FrameTimingMetadata& timing, std::span<const float> input, std::span<const int64_t> inputShape)
        {
            if (input.empty())
            {
                TR_CORE_WARN("FrameGenerator: Ignoring enqueue request because the input tensor is empty.");
                return false;
            }

            if (inputShape.empty())
            {
                TR_CORE_ERROR("FrameGenerator: Input shape is required so the worker can execute inference safely.");
                return false;
            }

            std::unique_lock<std::mutex> l_Lock(m_BufferMutex);

            FrameBuffer* l_TargetBuffer = nullptr;
            while (l_TargetBuffer == nullptr)
            {
                for (FrameBuffer& it_Buffer : m_Buffers)
                {
                    if (!it_Buffer.m_InputReady && !it_Buffer.m_InferenceRunning && !it_Buffer.m_ResultReady)
                    {
                        l_TargetBuffer = &it_Buffer;
                        break;
                    }
                }

                if (l_TargetBuffer == nullptr)
                {
                    m_ResultAvailable.wait(l_Lock);

                    std::stop_token a_StopToken = m_Worker.get_stop_token();

                    if (a_StopToken.stop_requested())
                    {
                        TR_CORE_WARN("FrameGenerator: Worker is shutting down; enqueue aborted.");
                        return false;
                    }
                }
            }

            EnsureInputCapacity(*l_TargetBuffer, input.size());
            std::memcpy(l_TargetBuffer->m_InputTensor.get(), input.data(), input.size() * sizeof(float));
            l_TargetBuffer->m_InputElementCount = input.size();
            l_TargetBuffer->m_InputShape.assign(inputShape.begin(), inputShape.end());
            l_TargetBuffer->m_Descriptors = descriptors;
            l_TargetBuffer->m_Timing = timing;
            if (timing.m_EnqueueTime.time_since_epoch().count() != 0)
            {
                l_TargetBuffer->m_EnqueueTimestamp = timing.m_EnqueueTime;
            }
            else
            {
                l_TargetBuffer->m_EnqueueTimestamp = std::chrono::steady_clock::now();
            }
            l_TargetBuffer->m_InputReady = true;
            l_TargetBuffer->m_ResultReady = false;
            l_TargetBuffer->m_InferenceRunning = false;

            m_WorkAvailable.notify_one();

            return true;
        }

        std::optional<FrameInferenceResult> FrameGenerator::DequeueFrame()
        {
            std::lock_guard<std::mutex> l_Lock(m_BufferMutex);

            for (FrameBuffer& it_Buffer : m_Buffers)
            {
                if (!it_Buffer.m_ResultReady)
                {
                    continue;
                }

                FrameInferenceResult l_Result{};
                l_Result.m_Descriptors = it_Buffer.m_Descriptors;
                l_Result.m_Timing = it_Buffer.m_Timing;
                l_Result.m_InferenceDuration = it_Buffer.m_InferenceDuration;
                l_Result.m_QueueLatency = std::chrono::duration_cast<std::chrono::nanoseconds>(it_Buffer.m_DispatchTimestamp - it_Buffer.m_EnqueueTimestamp);
                l_Result.m_OutputTensor.assign(it_Buffer.m_OutputTensor.get(), it_Buffer.m_OutputTensor.get() + it_Buffer.m_OutputElementCount);

                it_Buffer.m_InputReady = false;
                it_Buffer.m_ResultReady = false;
                it_Buffer.m_InferenceRunning = false;
                it_Buffer.m_InputElementCount = 0;
                it_Buffer.m_OutputElementCount = 0;

                m_ResultAvailable.notify_one();

                return l_Result;
            }

            return std::nullopt;
        }

        void FrameGenerator::WorkerMain(std::stop_token stopToken)
        {
            while (!stopToken.stop_requested())
            {
                FrameBuffer* l_WorkBuffer = nullptr;
                {
                    std::unique_lock<std::mutex> l_Lock(m_BufferMutex);
                    m_WorkAvailable.wait(l_Lock, [this, &stopToken]()
                        {
                            if (stopToken.stop_requested())
                            {
                                return true;
                            }

                            for (FrameBuffer& it_Buffer : m_Buffers)
                            {
                                if (it_Buffer.m_InputReady && !it_Buffer.m_InferenceRunning)
                                {
                                    return true;
                                }
                            }

                            return false;
                        });

                    if (stopToken.stop_requested())
                    {
                        break;
                    }

                    for (FrameBuffer& it_Buffer : m_Buffers)
                    {
                        if (it_Buffer.m_InputReady && !it_Buffer.m_InferenceRunning)
                        {
                            l_WorkBuffer = &it_Buffer;
                            break;
                        }
                    }

                    if (l_WorkBuffer == nullptr)
                    {
                        continue;
                    }

                    l_WorkBuffer->m_InputReady = false;
                    l_WorkBuffer->m_InferenceRunning = true;
                    l_WorkBuffer->m_DispatchTimestamp = std::chrono::steady_clock::now();
                }

                std::vector<float> a_InputTensor(l_WorkBuffer->m_InputTensor.get(), l_WorkBuffer->m_InputTensor.get() + l_WorkBuffer->m_InputElementCount);

                const auto a_InferenceStart = std::chrono::steady_clock::now();
                std::vector<float> a_OutputTensor = m_Runtime.Run(a_InputTensor, l_WorkBuffer->m_InputShape);
                const auto a_InferenceEnd = std::chrono::steady_clock::now();

                const auto a_InferenceDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(a_InferenceEnd - a_InferenceStart);

                {
                    std::lock_guard<std::mutex> l_Lock(m_BufferMutex);

                    if (!a_OutputTensor.empty())
                    {
                        EnsureOutputCapacity(*l_WorkBuffer, a_OutputTensor.size());
                        std::memcpy(l_WorkBuffer->m_OutputTensor.get(), a_OutputTensor.data(), a_OutputTensor.size() * sizeof(float));
                        l_WorkBuffer->m_OutputElementCount = a_OutputTensor.size();
                    }
                    else
                    {
                        l_WorkBuffer->m_OutputElementCount = 0;
                    }

                    l_WorkBuffer->m_InferenceDuration = a_InferenceDuration;
                    l_WorkBuffer->m_ResultReady = true;
                    l_WorkBuffer->m_InferenceRunning = false;

                    const auto a_QueueLatency = std::chrono::duration_cast<std::chrono::milliseconds>(l_WorkBuffer->m_DispatchTimestamp - l_WorkBuffer->m_EnqueueTimestamp);

                    // Smoke-test path that keeps developers informed about inference behaviour without tying into the GPU yet.
                    TR_CORE_TRACE("FrameGenerator: inference {} ms (queue {} ms, render delta {} ms)", std::chrono::duration<double, std::milli>(a_InferenceDuration).count(),
                        a_QueueLatency.count(), l_WorkBuffer->m_Timing.m_RenderDeltaMilliseconds);

                    m_ResultAvailable.notify_all();
                }
            }
        }

        void FrameGenerator::EnsureInputCapacity(FrameBuffer& buffer, size_t elementCount)
        {
            if (buffer.m_InputCapacity >= elementCount)
            {
                return;
            }

            buffer.m_InputTensor = m_Runtime.AllocateTensorBuffer(elementCount);
            if (!buffer.m_InputTensor)
            {
                TR_CORE_ERROR("FrameGenerator: Failed to allocate {} floats for the input tensor.", elementCount);
                buffer.m_InputCapacity = 0;

                return;
            }

            buffer.m_InputCapacity = elementCount;
        }

        void FrameGenerator::EnsureOutputCapacity(FrameBuffer& buffer, size_t elementCount)
        {
            if (buffer.m_OutputCapacity >= elementCount)
            {
                return;
            }

            buffer.m_OutputTensor = m_Runtime.AllocateTensorBuffer(elementCount);
            if (!buffer.m_OutputTensor)
            {
                TR_CORE_ERROR("FrameGenerator: Failed to allocate {} floats for the output tensor.", elementCount);
                buffer.m_OutputCapacity = 0;

                return;
            }

            buffer.m_OutputCapacity = elementCount;
        }
    }
}