#include "AI/OnnxRuntimeContext.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <functional>
#include <numeric>
#include <optional>
#include <sstream>
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

        OnnxRuntimeContext::OnnxRuntimeContext() : m_Environment{ s_DefaultLogLevel, "TridentOnnx" }, m_DefaultSessionOptions{}
        {
            // Configure a predictable baseline that we can tweak via ConfigureThreading.
            m_DefaultSessionOptions.SetInterOpNumThreads(s_DefaultInterOpThreads);
            m_DefaultSessionOptions.SetIntraOpNumThreads(s_DefaultIntraOpThreads);
            m_DefaultSessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        }

        void OnnxRuntimeContext::ConfigureThreading(uint32_t interOpThreads, uint32_t intraOpThreads)
        {
            // The ONNX runtime expects non-zero values; guard against invalid input while leaving room for future heuristics such as querying hardware concurrency.
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
            std::shared_ptr<Ort::Session> a_NewSession;

            try
            {
                // Attempt to create the ONNX session for the requested model. The runtime may throw if the model targets a newer IR/opset than the bundled runtime supports,
                // so we catch that case to provide a more actionable diagnostic downstream.
                a_NewSession = std::make_shared<Ort::Session>(m_Environment, l_ModelPath.c_str(), m_DefaultSessionOptions);
            }
            catch (const Ort::Exception& l_Exception)
            {
                if (!HandleModelLoadFailure(l_SanitizedPath, l_Exception))
                {
                    throw;
                }
            }
#else
            const std::string l_ModelPath = l_SanitizedPath.string();
            std::shared_ptr<Ort::Session> a_NewSession;

            try
            {
                // Attempt to create the ONNX session for the requested model. The runtime may throw if the model targets a newer IR/opset than the bundled runtime supports,
                // so we catch that case to provide a more actionable diagnostic downstream.
                a_NewSession = std::make_shared<Ort::Session>(m_Environment, l_ModelPath.c_str(), m_DefaultSessionOptions);
            }
            catch (const Ort::Exception& l_Exception)
            {
                if (!HandleModelLoadFailure(l_SanitizedPath, l_Exception))
                {
                    throw;
                }
            }
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

        std::vector<Ort::Value> OnnxRuntimeContext::Run(std::string_view modelName, std::span<const char* const> inputNames, std::span<const Ort::Value> inputs,
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
            // Copy the incoming data into a runtime-managed buffer. Later we can optimise this by allowing callers to supply their own allocator or use OrtValue::CreateTensor with
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

        bool OnnxRuntimeContext::HandleModelLoadFailure(const std::filesystem::path& modelPath, const Ort::Exception& runtimeError) const
        {
            const std::string l_RuntimeMessage{ runtimeError.what() };
            const bool l_IsIrMismatch = l_RuntimeMessage.find("Unsupported model IR version") != std::string::npos;

            if (!l_IsIrMismatch)
            {
                return false;
            }

            // Compose a friendlier diagnostic that explains why the runtime rejected the model and how the developer can resolve the mismatch.
            // how the developer can resolve the mismatch. Dynamically include the runtime version
            // and (where available) its maximum supported IR to avoid misleading users when the
            // bundled binaries lag behind the model exporter.
            const std::string l_RuntimeVersion{ Ort::GetVersionString() };
            const std::optional<uint64_t> l_ModelIr = ReadOnnxIrVersion(modelPath);
            const std::optional<uint64_t> l_MaxSupportedIr = ParseMaxSupportedIrVersion(l_RuntimeMessage);

            std::ostringstream l_Stream;
            l_Stream << "ONNX Runtime " << l_RuntimeVersion << " rejected model '"<< modelPath.string()
                << "' because it targets an ONNX IR version newer than the bundled runtime understands.";

            if (l_ModelIr)
            {
                l_Stream << " Detected model IR version " << *l_ModelIr << '.';
            }

            if (l_MaxSupportedIr)
            {
                l_Stream << " Maximum supported IR version in this build: " << *l_MaxSupportedIr << '.';
            }

            l_Stream << " Update the packaged onnxruntime binaries or re-export the model with an older opset compatible with the deployed runtime.";

            throw Ort::Exception(l_Stream.str().c_str(), runtimeError.GetOrtErrorCode());
        }

        std::optional<uint64_t> OnnxRuntimeContext::ReadOnnxIrVersion(const std::filesystem::path& modelPath)
        {
            std::ifstream l_Stream{ modelPath, std::ios::binary };
            if (!l_Stream)
            {
                return std::nullopt;
            }

            // The ONNX file format begins with field #1 (ir_version) encoded as a protobuf varint.
            // We only need the first field to inform the error message, so a tiny parser suffices.
            const int l_KeyByte = l_Stream.get();
            if (l_KeyByte == EOF)
            {
                return std::nullopt;
            }

            constexpr uint8_t s_IrVersionKey = static_cast<uint8_t>((1 << 3) | 0); // field=1, wire=varint
            if (static_cast<uint8_t>(l_KeyByte) != s_IrVersionKey)
            {
                return std::nullopt;
            }

            uint64_t l_Version = 0;
            int l_Shift = 0;

            while (l_Shift < 64)
            {
                const int l_RawByte = l_Stream.get();
                if (l_RawByte == EOF)
                {
                    return std::nullopt;
                }

                const uint8_t l_Byte = static_cast<uint8_t>(l_RawByte);
                l_Version |= static_cast<uint64_t>(l_Byte & 0x7F) << l_Shift;

                if ((l_Byte & 0x80U) == 0U)
                {
                    return l_Version;
                }

                l_Shift += 7;
            }

            return std::nullopt;
        }

        std::optional<uint64_t> OnnxRuntimeContext::ParseMaxSupportedIrVersion(std::string_view runtimeMessage) const
        {
            // The runtime emits messages such as "Unsupported model IR version: 14, max supported IR version: 12".
            // Parse the tail end to surface the runtime's capability in our higher-level exception.
            constexpr std::string_view s_Token = "max supported IR version";
            const size_t l_TokenPos = runtimeMessage.find(s_Token);
            if (l_TokenPos == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t l_NumberPos = runtimeMessage.find_first_of("0123456789", l_TokenPos + s_Token.size());
            if (l_NumberPos == std::string::npos)
            {
                return std::nullopt;
            }

            uint64_t l_MaxSupportedIr = 0;
            const char* l_Begin = runtimeMessage.data() + l_NumberPos;
            const char* l_End = runtimeMessage.data() + runtimeMessage.size();
            const std::from_chars_result l_Result = std::from_chars(l_Begin, l_End, l_MaxSupportedIr);

            if (l_Result.ec != std::errc{})
            {
                return std::nullopt;
            }

            return l_MaxSupportedIr;
        }
    }
}
