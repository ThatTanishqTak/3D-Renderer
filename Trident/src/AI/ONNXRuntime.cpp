#include "AI/ONNXRuntime.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>

#if defined(__has_include)
#if __has_include(<cuda_provider_factory.h>)
#include <cuda_provider_factory.h>
#define TRIDENT_HAS_CUDA_PROVIDER 1
#else
#define TRIDENT_HAS_CUDA_PROVIDER 0
#endif
#else
#define TRIDENT_HAS_CUDA_PROVIDER 0
#endif

namespace Trident
{
    namespace AI
    {
        ONNXRuntime::ONNXRuntime() : m_EnableCUDA(true), m_EnableCPUFallback(true), m_IsCUDAActive(false), m_IsCPUActive(false), m_ModelLoaded(false),
            m_Env(ORT_LOGGING_LEVEL_WARNING, "Trident"), m_Session(nullptr), m_SessionOptions(), m_CpuMemoryInfo(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU))
        {
            // Single threaded intra-op execution keeps behaviour predictable for the render thread owner.
            m_SessionOptions.SetIntraOpNumThreads(1);
        }

        ONNXRuntime::~ONNXRuntime() = default;

        void ONNXRuntime::EnableCUDA(bool enableCUDA)
        {
            // Allow callers to explicitly control whether CUDA acceleration should be attempted.
            m_EnableCUDA = enableCUDA;
        }

        void ONNXRuntime::EnableDirectML(bool enableDirectML)
        {
            // Maintain backwards compatibility by forwarding legacy DirectML toggles to the CUDA setting.
            TR_CORE_WARN("ONNX Runtime: EnableDirectML is deprecated; forwarding toggle to EnableCUDA.");
            EnableCUDA(enableDirectML);
        }

        void ONNXRuntime::EnableCPUFallback(bool enableCPUFallback)
        {
            m_EnableCPUFallback = enableCPUFallback;
        }

        bool ONNXRuntime::LoadModel(const std::string& modelPath)
        {
            ConfigureExecutionProviders();

            m_ModelLoaded = false;
            m_LoadedModelPath.clear();
            if (!m_IsCUDAActive && !m_IsCPUActive)
            {
                TR_CORE_ERROR("ONNX Runtime: No execution providers available after configuration.");
                return false;
            }

            try
            {
#if defined(_WIN32)
                std::wstring l_WideModelPath(modelPath.begin(), modelPath.end());
                m_Session = Ort::Session(m_Env, l_WideModelPath.c_str(), m_SessionOptions);
#else
                m_Session = Ort::Session(m_Env, modelPath.c_str(), m_SessionOptions);
#endif

                CacheIOBindingMetadata();

                m_ModelLoaded = true;
                m_LoadedModelPath = modelPath;
                TR_CORE_INFO("ONNX Runtime: Model '{}' loaded with CUDA={} CPUFallback={}", modelPath, m_IsCUDAActive, m_IsCPUActive);

                return true;
            }
            catch (const Ort::Exception& e)
            {
                TR_CORE_ERROR("ONNX Runtime error: {}", e.what());
                m_ModelLoaded = false;
                m_LoadedModelPath.clear();

                return false;
            }
        }

        std::vector<float> ONNXRuntime::Run(const std::vector<float>& input, const std::vector<int64_t>& shape)
        {
            if (!m_Session)
            {
                TR_CORE_ERROR("ONNX Runtime: Run invoked before a model was successfully loaded.");
                return {};
            }

            if (m_InputNames.empty() || m_OutputNames.empty())
            {
                TR_CORE_ERROR("ONNX Runtime: Missing cached IO metadata. Ensure LoadModel succeeded before calling Run.");
                return {};
            }

            size_t l_ShapeElementCount = CalculateElementCount(shape);
            if (l_ShapeElementCount != input.size())
            {
                TR_CORE_ERROR("ONNX Runtime: Input element count {} mismatches provided shape element count {}.", input.size(), l_ShapeElementCount);
                return {};
            }

            // Validate static input expectations for the first binding; dynamic shapes are allowed to change per dispatch.
            if (!m_InputShapeIsDynamic.empty() && !m_InputShapeIsDynamic.front())
            {
                size_t l_ExpectedCount = CalculateElementCount(m_InputShapes.front());
                if (l_ExpectedCount != input.size())
                {
                    TR_CORE_WARN("ONNX Runtime: Input size {} diverges from cached metadata {}. Proceeding due to caller override.", input.size(), l_ExpectedCount);
                }
            }

            Ort::Value l_InputTensor = CreateTensorFromBuffer(const_cast<float*>(input.data()), input.size(), shape);

            const char* l_InputNames[] = { m_InputNames.front().c_str() };
            const char* l_OutputNames[] = { m_OutputNames.front().c_str() };

            std::vector<Ort::Value> l_OutputTensors;
            l_OutputTensors.reserve(1);

            if (!m_OutputShapeIsDynamic.empty() && !m_OutputShapeIsDynamic.front())
            {
                size_t l_OutputElementCount = m_OutputElementCounts.front();
                if (l_OutputElementCount > 0)
                {
                    auto l_Buffer = AllocateTensorBuffer(l_OutputElementCount);
                    float* l_OutputBuffer = l_Buffer.get();
                    Ort::Value l_OutputTensor = CreateTensorFromBuffer(l_OutputBuffer, l_OutputElementCount, m_OutputShapes.front());

                    l_OutputTensors.emplace_back(std::move(l_OutputTensor));

                    Ort::RunOptions l_RunOptions{ nullptr };
                    m_Session.Run(l_RunOptions, l_InputNames, &l_InputTensor, 1, l_OutputNames, l_OutputTensors.data(), l_OutputTensors.size());

                    std::vector<float> l_Output(l_OutputElementCount);
                    std::memcpy(l_Output.data(), l_OutputBuffer, l_OutputElementCount * sizeof(float));

                    // Keep allocation alive until copy completes; clear tensors so Ort::Value releases before buffer free.
                    l_OutputTensors.clear();

                    return l_Output;
                }
            }

            auto l_OutputTensor = m_Session.Run(Ort::RunOptions{ nullptr }, l_InputNames, &l_InputTensor, 1, l_OutputNames, 1);

            float* l_OutputData = l_OutputTensor.front().GetTensorMutableData<float>();
            size_t l_OutputSize = l_OutputTensor.front().GetTensorTypeAndShapeInfo().GetElementCount();

            return std::vector<float>(l_OutputData, l_OutputData + l_OutputSize);
        }

        std::unique_ptr<float, void(*)(void*)> ONNXRuntime::AllocateTensorBuffer(size_t elementCount) const
        {
            size_t l_Bytes = elementCount * sizeof(float);
            void* l_Raw = TR_MALLOC(l_Bytes);

            if (l_Raw == nullptr)
            {
                TR_CORE_ERROR("ONNX Runtime: Failed to allocate {} bytes for tensor buffer.", l_Bytes);
            }

            return { static_cast<float*>(l_Raw), [](void* ptr)
            {
                std::free(ptr);
            } };
        }

        Ort::Value ONNXRuntime::CreateTensorFromBuffer(float* buffer, size_t elementCount, const std::vector<int64_t>& shape)
        {
            size_t l_Expected = CalculateElementCount(shape);
            if (buffer == nullptr)
            {
                TR_CORE_ERROR("ONNX Runtime: Null buffer passed for tensor creation.");
            }
            else if (l_Expected != elementCount)
            {
                TR_CORE_WARN("ONNX Runtime: Tensor buffer element count {} mismatches expected {}. Proceeding with provided buffer.", elementCount, l_Expected);
            }

            return Ort::Value::CreateTensor<float>(m_CpuMemoryInfo, buffer, elementCount, shape.data(), shape.size());
        }

        const std::vector<std::string>& ONNXRuntime::GetInputNames() const noexcept
        {
            return m_InputNames;
        }

        const std::vector<std::string>& ONNXRuntime::GetOutputNames() const noexcept
        {
            return m_OutputNames;
        }

        void ONNXRuntime::ConfigureExecutionProviders()
        {
            // Provider configuration must happen on the same thread that owns the Ort::SessionOptions instance.
            m_IsCUDAActive = false;
            m_IsCPUActive = false;

#if TRIDENT_HAS_CUDA_PROVIDER
            if (m_EnableCUDA)
            {
                // Device 0 is the default primary adapter when the engine requests GPU execution.
                int l_DeviceId = 0;
                OrtStatus* l_Status = OrtSessionOptionsAppendExecutionProvider_CUDA(m_SessionOptions, l_DeviceId);
                if (l_Status == nullptr)
                {
                    m_IsCUDAActive = true;
                    TR_CORE_INFO("ONNX Runtime: CUDA execution provider appended on device {}.", l_DeviceId);
                }
                else
                {
                    TR_CORE_WARN("ONNX Runtime: CUDA provider unavailable - {}", Ort::GetApi().GetErrorMessage(l_Status));
                    Ort::GetApi().ReleaseStatus(l_Status);
                }
            }
#else
            if (m_EnableCUDA)
            {
                TR_CORE_WARN("ONNX Runtime: CUDA requested but cuda_provider_factory.h is unavailable; skipping accelerator append.");
            }
#endif

            if (m_EnableCPUFallback || !m_IsCUDAActive)
            {
                OrtStatus* l_CpuStatus = OrtSessionOptionsAppendExecutionProvider_CPU(m_SessionOptions, 0);
                if (l_CpuStatus == nullptr)
                {
                    m_IsCPUActive = true;
                    TR_CORE_INFO("ONNX Runtime: CPU execution provider appended as fallback.");
                }
                else
                {
                    TR_CORE_ERROR("ONNX Runtime: Failed to append CPU provider - {}", Ort::GetApi().GetErrorMessage(l_CpuStatus));
                    Ort::GetApi().ReleaseStatus(l_CpuStatus);
                }
            }
        }

        void ONNXRuntime::CacheIOBindingMetadata()
        {
            // Metadata caching is single-threaded and performed immediately after model load.
            m_InputNames.clear();
            m_InputShapes.clear();
            m_InputShapeIsDynamic.clear();
            m_OutputNames.clear();
            m_OutputShapes.clear();
            m_OutputElementCounts.clear();
            m_OutputShapeIsDynamic.clear();

            Ort::AllocatorWithDefaultOptions l_Allocator;

            size_t l_InputCount = m_Session.GetInputCount();
            m_InputNames.reserve(l_InputCount);
            m_InputShapes.reserve(l_InputCount);
            m_InputShapeIsDynamic.reserve(l_InputCount);

            for (size_t l_Index = 0; l_Index < l_InputCount; ++l_Index)
            {
                Ort::AllocatedStringPtr l_Name = m_Session.GetInputNameAllocated(l_Index, l_Allocator);
                m_InputNames.emplace_back(l_Name.get());

                Ort::TypeInfo l_TypeInfo = m_Session.GetInputTypeInfo(l_Index);
                auto l_TensorInfo = l_TypeInfo.GetTensorTypeAndShapeInfo();
                std::vector<int64_t> l_Shape = l_TensorInfo.GetShape();

                m_InputShapeIsDynamic.emplace_back(IsShapeDynamic(l_Shape));
                m_InputShapes.emplace_back(std::move(l_Shape));
            }

            size_t l_OutputCount = m_Session.GetOutputCount();
            m_OutputNames.reserve(l_OutputCount);
            m_OutputShapes.reserve(l_OutputCount);
            m_OutputElementCounts.reserve(l_OutputCount);
            m_OutputShapeIsDynamic.reserve(l_OutputCount);

            for (size_t l_Index = 0; l_Index < l_OutputCount; ++l_Index)
            {
                Ort::AllocatedStringPtr l_Name = m_Session.GetOutputNameAllocated(l_Index, l_Allocator);
                m_OutputNames.emplace_back(l_Name.get());

                Ort::TypeInfo l_TypeInfo = m_Session.GetOutputTypeInfo(l_Index);
                auto l_TensorInfo = l_TypeInfo.GetTensorTypeAndShapeInfo();
                std::vector<int64_t> l_Shape = l_TensorInfo.GetShape();

                bool l_IsDynamic = IsShapeDynamic(l_Shape);
                m_OutputShapeIsDynamic.emplace_back(l_IsDynamic);

                size_t l_ElementCount = l_IsDynamic ? 0 : CalculateElementCount(l_Shape);
                m_OutputElementCounts.emplace_back(l_ElementCount);
                m_OutputShapes.emplace_back(std::move(l_Shape));
            }

            TR_CORE_INFO("ONNX Runtime: Cached {} input(s) and {} output(s) metadata.", m_InputNames.size(), m_OutputNames.size());
        }

        size_t ONNXRuntime::CalculateElementCount(const std::vector<int64_t>& shape) const
        {
            if (shape.empty())
            {
                return 0;
            }

            size_t l_Total = 1;
            for (int64_t l_Dimension : shape)
            {
                if (l_Dimension <= 0)
                {
                    return 0;
                }

                size_t l_Candidate = l_Total * static_cast<size_t>(l_Dimension);
                if (l_Candidate / static_cast<size_t>(l_Dimension) != l_Total)
                {
                    TR_CORE_ERROR("ONNX Runtime: Tensor element count overflow detected.");
                    return 0;
                }

                l_Total = l_Candidate;
            }

            return l_Total;
        }

        bool ONNXRuntime::IsShapeDynamic(const std::vector<int64_t>& shape) const
        {
            return std::any_of(shape.begin(), shape.end(), [](int64_t l_Dimension)
                {
                    return l_Dimension <= 0;
                });
        }
    }
}

#undef TRIDENT_HAS_CUDA_PROVIDER