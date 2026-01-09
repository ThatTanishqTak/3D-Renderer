#include "AI/FrameGenerator.h"

#include "Core/Utilities.h"

#include <algorithm>
#include <chrono>
#include <numeric>

namespace Trident
{
    namespace AI
    {
        namespace
        {
            /**
             * @brief Build a key derived from the model path so the runtime context can cache sessions.
             */
            std::string BuildModelKey(const std::filesystem::path& modelPath)
            {
                const std::filesystem::path l_Filename = modelPath.filename();
                if (!l_Filename.empty())
                {
                    return l_Filename.string();
                }

                return modelPath.string();
            }

            /**
             * @brief Helper to calculate the flattened element count from a tensor shape.
             */
            size_t CalculateElementCount(std::span<const int64_t> shape)
            {
                if (shape.empty())
                {
                    return 0;
                }

                return static_cast<size_t>(std::accumulate(shape.begin(), shape.end(), int64_t{ 1 },
                    [](int64_t lhs, int64_t rhs)
                    {
                        const int64_t l_SafeLhs = std::max<int64_t>(lhs, 1);
                        const int64_t l_SafeRhs = std::max<int64_t>(rhs, 1);

                        return l_SafeLhs * l_SafeRhs;
                    }));
            }
        }

        FrameGenerator::FrameGenerator()
        {
            m_RuntimeContext = &AI::OnnxRuntimeContext::Get();
        }

        FrameGenerator::~FrameGenerator()
        {
            // Ensure the worker thread is stopped before the owning subsystem shuts down.
            ResetState();
        }

        bool FrameGenerator::Initialise(const std::filesystem::path& modelPath)
        {
            ResetState();

            if (modelPath.empty())
            {
                TR_CORE_WARN("AI frame generator initialisation skipped because the supplied model path was empty.");
                return false;
            }

            m_ModelKey = BuildModelKey(modelPath);

            try
            {
                const Ort::Session& l_Session = m_RuntimeContext->LoadModel(m_ModelKey, modelPath);
                if (!CacheModelBindings(l_Session))
                {
                    TR_CORE_ERROR("Failed to cache tensor metadata for model '{}'", modelPath.string());
                    ResetState();

                    return false;
                }
            }
            catch (const Ort::Exception& l_Exception)
            {
                TR_CORE_ERROR("ONNX runtime rejected model '{}': {}", modelPath.string(), l_Exception.what());
                ResetState();

                return false;
            }
            catch (const std::exception& l_Exception)
            {
                TR_CORE_ERROR("Unexpected error while loading model '{}': {}", modelPath.string(), l_Exception.what());
                ResetState();

                return false;
            }

            if (!m_InputBindings.empty())
            {
                m_InputStagingBuffer.resize(m_InputBindings.front().m_ElementCount);
            }


            // The model metadata is ready, so spin up the background worker that will service inference jobs.
            m_WorkerShouldStop = false;
            m_WorkerThread = std::thread(&FrameGenerator::WorkerLoop, this);

            m_IsInitialised = true;

            return true;
        }

        bool FrameGenerator::ProcessFrame(std::span<const float> frameData)
        {
            if (!m_IsInitialised)
            {
                TR_CORE_WARN("AI frame generator was asked to process a frame before the model finished initialising.");
                return false;
            }

            if (m_InputBindings.empty())
            {
                TR_CORE_WARN("The target model did not expose any input tensors. Skipping inference run for now.");
                return false;
            }

            const TensorBinding& l_PrimaryInput = m_InputBindings.front();
            if (l_PrimaryInput.m_ElementCount != frameData.size())
            {
                TR_CORE_WARN("Incoming frame tensor element count ({}) does not match the model requirement ({}).", frameData.size(), l_PrimaryInput.m_ElementCount);
                return false;
            }

            std::vector<float> l_StagingBuffer;
            {
                // The staging buffer is shared with the worker thread through the pool, so guard it with the queue mutex.
                std::scoped_lock l_Lock(m_QueueMutex);
                if (!m_InputStagingBuffer.empty())
                {
                    l_StagingBuffer = std::move(m_InputStagingBuffer);
                }
                else if (!m_InputBufferPool.empty())
                {
                    l_StagingBuffer = std::move(m_InputBufferPool.front());
                    m_InputBufferPool.pop_front();
                }
            }

            if (l_StagingBuffer.size() != l_PrimaryInput.m_ElementCount)
            {
                l_StagingBuffer.resize(l_PrimaryInput.m_ElementCount);
            }

            // Copy the frame into a reusable staging buffer so we do not allocate a new vector each frame.
            std::copy(frameData.begin(), frameData.end(), l_StagingBuffer.begin());

            FrameJob l_Job{};
            l_Job.m_InputTensor = std::move(l_StagingBuffer);

            {
                std::scoped_lock l_Lock(m_QueueMutex);
                m_PendingJobs.emplace_back(std::move(l_Job));
                m_PendingJobCount = m_PendingJobs.size();
                if (m_InputStagingBuffer.empty())
                {
                    if (!m_InputBufferPool.empty())
                    {
                        m_InputStagingBuffer = std::move(m_InputBufferPool.front());
                        m_InputBufferPool.pop_front();
                    }
                    else
                    {
                        m_InputStagingBuffer.resize(l_PrimaryInput.m_ElementCount);
                    }
                }
            }
            m_QueueCondition.notify_one();

            return true;
        }

        std::vector<float> FrameGenerator::GetLastOutput() const
        {
            std::scoped_lock l_Lock(m_OutputMutex);
            return m_LastOutputTensor;
        }

        bool FrameGenerator::TryConsumeOutput(std::vector<float>& a_OutOutput)
        {
            std::scoped_lock l_Lock(m_OutputMutex);
            if (m_CompletedOutputs.empty())
            {
                return false;
            }

            std::vector<float> l_CompletedBuffer = std::move(m_CompletedOutputs.front());
            m_CompletedOutputs.pop_front();

            // Swap buffers so the caller keeps the output while the old caller buffer returns to the pool for reuse.
            std::vector<float> l_RecycledBuffer = std::move(a_OutOutput);
            a_OutOutput = std::move(l_CompletedBuffer);
            l_RecycledBuffer.clear();
            m_OutputBufferPool.emplace_back(std::move(l_RecycledBuffer));

            return true;
        }

        double FrameGenerator::GetLastInferenceMilliseconds() const
        {
            std::scoped_lock l_Lock(m_OutputMutex);

            return m_LastInferenceMilliseconds;
        }

        double FrameGenerator::GetAverageInferenceMilliseconds() const
        {
            std::scoped_lock l_Lock(m_OutputMutex);

            if (m_CompletedInferenceCount == 0)
            {
                return 0.0;
            }

            return m_TotalInferenceMilliseconds / static_cast<double>(m_CompletedInferenceCount);
        }

        uint64_t FrameGenerator::GetCompletedInferenceCount() const
        {
            std::scoped_lock l_Lock(m_OutputMutex);

            return m_CompletedInferenceCount;
        }

        size_t FrameGenerator::GetPendingJobCount() const
        {
            std::scoped_lock l_Lock(m_QueueMutex);

            return m_PendingJobCount;
        }

        std::span<const int64_t> FrameGenerator::GetPrimaryInputShape() const
        {
            if (m_InputBindings.empty())
            {
                return {};
            }

            return m_InputBindings.front().m_Shape;
        }

        std::span<const int64_t> FrameGenerator::GetPrimaryOutputShape() const
        {
            if (m_OutputBindings.empty())
            {
                return {};
            }

            return m_OutputBindings.front().m_Shape;
        }

        bool FrameGenerator::CacheModelBindings(const Ort::Session& session)
        {
            Ort::AllocatorWithDefaultOptions l_Allocator;

            const size_t l_InputCount = session.GetInputCount();
            const size_t l_OutputCount = session.GetOutputCount();

            m_InputBindings.clear();
            m_OutputBindings.clear();
            m_InputBindings.reserve(l_InputCount);
            m_OutputBindings.reserve(l_OutputCount);

            for (size_t it_Index = 0; it_Index < l_InputCount; ++it_Index)
            {
                auto a_Name = session.GetInputNameAllocated(it_Index, l_Allocator);
                Ort::TypeInfo l_TypeInfo = session.GetInputTypeInfo(it_Index);
                const auto& a_TensorInfo = l_TypeInfo.GetTensorTypeAndShapeInfo();

                TensorBinding l_Binding{};
                l_Binding.m_Name = a_Name.get();
                l_Binding.m_Shape = a_TensorInfo.GetShape();
                l_Binding.m_ElementCount = CalculateElementCount(l_Binding.m_Shape);
                m_InputBindings.emplace_back(std::move(l_Binding));
            }

            for (size_t it_Index = 0; it_Index < l_OutputCount; ++it_Index)
            {
                auto a_Name = session.GetOutputNameAllocated(it_Index, l_Allocator);
                Ort::TypeInfo l_TypeInfo = session.GetOutputTypeInfo(it_Index);
                const auto& a_TensorInfo = l_TypeInfo.GetTensorTypeAndShapeInfo();

                TensorBinding l_Binding{};
                l_Binding.m_Name = a_Name.get();
                l_Binding.m_Shape = a_TensorInfo.GetShape();
                l_Binding.m_ElementCount = CalculateElementCount(l_Binding.m_Shape);
                m_OutputBindings.emplace_back(std::move(l_Binding));
            }

            return !m_InputBindings.empty() && !m_OutputBindings.empty();
        }

        void FrameGenerator::ResetState()
        {
            // Tear down the asynchronous pipeline so a subsequent Initialise call starts from a clean slate.
            {
                std::unique_lock<std::mutex> l_Lock(m_QueueMutex);
                m_WorkerShouldStop = true;
                m_QueueCondition.notify_all();
            }

            if (m_WorkerThread.joinable())
            {
                m_WorkerThread.join();
            }

            {
                std::scoped_lock l_Lock(m_QueueMutex);
                m_PendingJobs.clear();
                m_InputBufferPool.clear();
                m_InputStagingBuffer.clear();
                m_WorkerShouldStop = false;
                m_PendingJobCount = 0;
            }

            {
                std::scoped_lock l_Lock(m_OutputMutex);
                m_CompletedOutputs.clear();
                m_OutputBufferPool.clear();
                m_LastOutputTensor.clear();
                m_LastInferenceMilliseconds = 0.0;
                m_TotalInferenceMilliseconds = 0.0;
                m_CompletedInferenceCount = 0;
            }

            m_IsInitialised = false;
            m_InputBindings.clear();
            m_OutputBindings.clear();
        }

        void FrameGenerator::WorkerLoop()
        {
            const Ort::MemoryInfo l_CpuMemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

            // Process frames submitted by the renderer until shutdown is requested.
            while (true)
            {
                FrameJob l_Job{};
                {
                    std::unique_lock<std::mutex> l_Lock(m_QueueMutex);
                    m_QueueCondition.wait(l_Lock, [this]()
                        {
                            return m_WorkerShouldStop || !m_PendingJobs.empty();
                        });

                    if (m_WorkerShouldStop)
                    {
                        break;
                    }

                    l_Job = std::move(m_PendingJobs.front());
                    m_PendingJobs.pop_front();
                    m_PendingJobCount = m_PendingJobs.size();
                }

                if (l_Job.m_InputTensor.empty())
                {
                    continue;
                }

                std::vector<float> l_CombinedOutput;
                double l_RunMilliseconds = 0.0;
                bool l_RecordedTiming = false;

                try
                {
                    std::vector<const char*> l_InputNames;
                    l_InputNames.reserve(m_InputBindings.size());
                    for (const TensorBinding& it_Binding : m_InputBindings)
                    {
                        l_InputNames.push_back(it_Binding.m_Name.c_str());
                    }

                    std::vector<Ort::Value> l_InputTensors;
                    l_InputTensors.reserve(m_InputBindings.size());

                    // TODO: Extend this to support multi-input models when the engine begins to leverage them.
                    // The input buffer must stay alive for the duration of the Run call, so it lives in l_Job until after inference finishes.
                    Ort::Value l_FrameTensor = Ort::Value::CreateTensor<float>(l_CpuMemoryInfo, l_Job.m_InputTensor.data(), l_Job.m_InputTensor.size(),
                        m_InputBindings.front().m_Shape.data(), m_InputBindings.front().m_Shape.size());
                    l_InputTensors.emplace_back(std::move(l_FrameTensor));

                    std::vector<const char*> l_OutputNames;
                    l_OutputNames.reserve(m_OutputBindings.size());
                    for (const TensorBinding& it_Binding : m_OutputBindings)
                    {
                        l_OutputNames.push_back(it_Binding.m_Name.c_str());
                    }

                    {
                        // The output buffer pool is shared with the main thread, so always hold the output mutex.
                        std::scoped_lock l_Lock(m_OutputMutex);
                        if (!m_OutputBufferPool.empty())
                        {
                            l_CombinedOutput = std::move(m_OutputBufferPool.front());
                            m_OutputBufferPool.pop_front();
                        }
                    }

                    l_CombinedOutput.clear();
                    size_t l_ExpectedOutputElementCount = 0;
                    for (const TensorBinding& it_Binding : m_OutputBindings)
                    {
                        l_ExpectedOutputElementCount += it_Binding.m_ElementCount;
                    }

                    if (l_ExpectedOutputElementCount > 0)
                    {
                        l_CombinedOutput.reserve(l_ExpectedOutputElementCount);
                    }

                    // Capture the inference duration so the renderer can expose accurate timing data for debugging.
                    const auto l_RunStart = std::chrono::steady_clock::now();
                    auto a_OutputTensors = m_RuntimeContext->Run(m_ModelKey, l_InputNames, l_InputTensors, l_OutputNames);
                    const auto l_RunEnd = std::chrono::steady_clock::now();
                    l_RunMilliseconds = std::chrono::duration<double, std::milli>(l_RunEnd - l_RunStart).count();
                    l_RecordedTiming = true;

                    for (size_t it_Index = 0; it_Index < a_OutputTensors.size(); ++it_Index)
                    {
                        const Ort::Value& l_Output = a_OutputTensors[it_Index];
                        const Ort::TensorTypeAndShapeInfo l_Info = l_Output.GetTensorTypeAndShapeInfo();
                        const size_t l_ElementCount = static_cast<size_t>(l_Info.GetElementCount());
                        const float* l_Data = l_Output.GetTensorData<float>();
                        if (l_Data == nullptr)
                        {
                            TR_CORE_WARN("Output tensor {} did not contain any data.", it_Index);
                            continue;
                        }

                        const size_t l_WriteOffset = l_CombinedOutput.size();
                        l_CombinedOutput.resize(l_WriteOffset + l_ElementCount);
                        std::copy(l_Data, l_Data + l_ElementCount, l_CombinedOutput.begin() + static_cast<std::ptrdiff_t>(l_WriteOffset));
                    }
                }
                catch (const Ort::Exception& l_Exception)
                {
                    TR_CORE_ERROR("ONNX runtime rejected a frame submission: {}", l_Exception.what());
                    {
                        std::scoped_lock l_Lock(m_QueueMutex);
                        m_InputBufferPool.emplace_back(std::move(l_Job.m_InputTensor));
                    }
                    continue;
                }
                catch (const std::exception& l_Exception)
                {
                    TR_CORE_ERROR("Unexpected failure during AI frame processing: {}", l_Exception.what());
                    {
                        std::scoped_lock l_Lock(m_QueueMutex);
                        m_InputBufferPool.emplace_back(std::move(l_Job.m_InputTensor));
                    }
                    continue;
                }

                {
                    // Return the input buffer to the pool now that the runtime no longer reads from it.
                    std::scoped_lock l_Lock(m_QueueMutex);
                    m_InputBufferPool.emplace_back(std::move(l_Job.m_InputTensor));
                }

                if (l_CombinedOutput.empty())
                {
                    if (l_RecordedTiming)
                    {
                        std::scoped_lock l_Lock(m_OutputMutex);
                        m_LastInferenceMilliseconds = l_RunMilliseconds;
                    }

                    // Recycle the output buffer because no results were produced.
                    std::scoped_lock l_Lock(m_OutputMutex);
                    m_OutputBufferPool.emplace_back(std::move(l_CombinedOutput));
                    continue;
                }

                std::vector<float> l_FinalOutput = std::move(l_CombinedOutput);
                {
                    // Output buffers live in the completed queue until consumers pull them, so the pool only holds unused buffers.
                    std::scoped_lock l_Lock(m_OutputMutex);
                    m_LastOutputTensor = l_FinalOutput;
                    m_CompletedOutputs.emplace_back(std::move(l_FinalOutput));
                    if (l_RecordedTiming)
                    {
                        m_LastInferenceMilliseconds = l_RunMilliseconds;
                        m_TotalInferenceMilliseconds += l_RunMilliseconds;
                        ++m_CompletedInferenceCount;
                    }
                }

                // TODO: Investigate temporal accumulation, adaptive batching, and GPU buffer interop to further optimise the asynchronous path.
            }
        }
    }
}