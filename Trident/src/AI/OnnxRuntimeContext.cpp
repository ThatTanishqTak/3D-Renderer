#include "AI/OnnxRuntimeContext.h"

#include <algorithm>
#include <functional>
#include <numeric>
#include <stdexcept>

namespace Trident
{
    namespace AI
    {
        OnnxRuntimeContext& OnnxRuntimeContext::Get()
        {
            static OnnxRuntimeContext s_Instance;
            return s_Instance;
        }

        OnnxRuntimeContext::OnnxRuntimeContext()
            : m_Environment{ s_DefaultLogLevel, "TridentOnnx" }
            , m_DefaultSessionOptions{}
        {
            // Configure a predictable baseline that we can tweak via ConfigureThreading.
            m_DefaultSessionOptions.SetInterOpNumThreads(s_DefaultInterOpThreads);
            m_DefaultSessionOptions.SetIntraOpNumThreads(s_DefaultIntraOpThreads);
            m_DefaultSessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        }

        void OnnxRuntimeContext::ConfigureThreading(uint32_t interOpThreads, uint32_t intraOpThreads)
        {
            // The ONNX runtime expects non-zero values; guard against invalid input while leaving
            // room for future heuristics such as querying hardware concurrency.
            const uint32_t l_InterOp = std::max<uint32_t>(1, interOpThreads);
            const uint32_t l_IntraOp = std::max<uint32_t>(1, intraOpThreads);

            m_DefaultSessionOptions.SetInterOpNumThreads(static_cast<int>(l_InterOp));
            m_DefaultSessionOptions.SetIntraOpNumThreads(static_cast<int>(l_IntraOp));
        }

        const Ort::Session& OnnxRuntimeContext::LoadModel(std::string_view modelName, const std::filesystem::path& modelPath)
        {
            const std::string l_Key{ modelName };

            {
                std::scoped_lock l_Lock{ m_SessionMutex };
                const auto l_Existing = m_Sessions.find(l_Key);
                if (l_Existing != m_Sessions.end())
                {
                    return *l_Existing->second;
                }
            }

            const std::filesystem::path l_SanitizedPath = SanitizeModelPath(modelPath);

#ifdef _WIN32
            const std::wstring l_ModelPath = l_SanitizedPath.wstring();
            auto a_NewSession = std::make_shared<Ort::Session>(m_Environment, l_ModelPath.c_str(), m_DefaultSessionOptions);
#else
            const std::string l_ModelPath = l_SanitizedPath.string();
            auto a_NewSession = std::make_shared<Ort::Session>(m_Environment, l_ModelPath.c_str(), m_DefaultSessionOptions);
#endif

            std::scoped_lock l_Lock{ m_SessionMutex };
            const auto [l_Iterator, l_Inserted] = m_Sessions.emplace(l_Key, std::move(a_NewSession));
            if (!l_Inserted)
            {
                return *l_Iterator->second;
            }

            return *l_Iterator->second;
        }

        void OnnxRuntimeContext::UnloadModel(std::string_view modelName)
        {
            const std::string l_Key{ modelName };
            std::scoped_lock l_Lock{ m_SessionMutex };
            m_Sessions.erase(l_Key);
        }

        std::vector<Ort::Value> OnnxRuntimeContext::Run(std::string_view modelName,
            std::span<const char* const> inputNames,
            std::span<const Ort::Value> inputs,
            std::span<const char* const> outputNames)
        {
            if (inputNames.size() != inputs.size())
            {
                throw std::invalid_argument("Input name and tensor counts must match");
            }

            const std::string l_Key{ modelName };
            const std::shared_ptr<Ort::Session> l_Session = [&]()
                {
                    std::scoped_lock l_Lock{ m_SessionMutex };
                    const auto l_It = m_Sessions.find(l_Key);
                    if (l_It == m_Sessions.end())
                    {
                        throw std::runtime_error("Requested model has not been loaded");
                    }

                    return l_It->second;
                }();

            // Copy output names into an owning container since the runtime expects mutable char* arrays.
            std::vector<const char*> l_OutputNames{ outputNames.begin(), outputNames.end() };

            auto a_OutputTensors = l_Session->Run(Ort::RunOptions{ nullptr },
                inputNames.data(), const_cast<Ort::Value*>(inputs.data()), static_cast<size_t>(inputs.size()),
                l_OutputNames.data(), l_OutputNames.size());

            return a_OutputTensors;
        }

        Ort::Value OnnxRuntimeContext::CreateTensorFloat(std::span<const float> values, std::span<const int64_t> shape) const
        {
            // Copy the incoming data into a runtime-managed buffer. Later we can optimise this by
            // allowing callers to supply their own allocator or use OrtValue::CreateTensor with
            // custom release callbacks.
            const size_t l_ElementCount = std::accumulate(shape.begin(), shape.end(), size_t{ 1 }, std::multiplies<size_t>{});
            if (l_ElementCount != values.size())
            {
                throw std::invalid_argument("Tensor shape does not match value count");
            }

            Ort::AllocatorWithDefaultOptions l_Allocator;
            auto a_Tensor = Ort::Value::CreateTensor<float>(l_Allocator, shape.data(), shape.size());
            float* l_Data = a_Tensor.GetTensorMutableData<float>();
            std::copy(values.begin(), values.end(), l_Data);
            return a_Tensor;
        }

        const Ort::Env& OnnxRuntimeContext::GetEnvironment() const
        {
            return m_Environment;
        }

        std::filesystem::path OnnxRuntimeContext::SanitizeModelPath(const std::filesystem::path& modelPath) const
        {
            if (!std::filesystem::exists(modelPath))
            {
                throw std::runtime_error("Model file does not exist: " + modelPath.string());
            }

            return modelPath;
        }
    }
}