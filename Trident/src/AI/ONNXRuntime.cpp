#include "AI/ONNXRuntime.h"

namespace Trident
{
    namespace AI
    {
        ONNXRuntime::ONNXRuntime() : m_Env(ORT_LOGGING_LEVEL_WARNING, "Trident")
        {
            m_SessionOptions.SetIntraOpNumThreads(1);
        }

        ONNXRuntime::~ONNXRuntime() = default;

        bool ONNXRuntime::LoadModel(const std::string& modelPath)
        {
            try
            {
#if defined(_WIN32)
                std::wstring wideModelPath(modelPath.begin(), modelPath.end());
                m_Session = Ort::Session(m_Env, wideModelPath.c_str(), m_SessionOptions);
#else
                m_Session = Ort::Session(m_Env, modelPath.c_str(), m_SessionOptions);
#endif
             
                return true;
            }
            catch (const Ort::Exception& e)
            {
                TR_CORE_ERROR("ONNX Runtime error: {}", e.what());
                
                return false;
            }
        }

        std::vector<float> ONNXRuntime::Run(const std::vector<float>& input, const std::vector<int64_t>& shape)
        {
            Ort::AllocatorWithDefaultOptions allocator;

            Ort::AllocatedStringPtr inputName = m_Session.GetInputNameAllocated(0, allocator);
            Ort::AllocatedStringPtr outputName = m_Session.GetOutputNameAllocated(0, allocator);

            const char* inputNames[] = { inputName.get() };
            const char* outputNames[] = { outputName.get() };

            Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(memInfo, const_cast<float*>(input.data()), input.size(), shape.data(), shape.size());

            auto outputTensor = m_Session.Run(Ort::RunOptions{ nullptr }, inputNames, &inputTensor, 1, outputNames, 1);

            float* outputData = outputTensor.front().GetTensorMutableData<float>();
            size_t outputSize = outputTensor.front().GetTensorTypeAndShapeInfo().GetElementCount();

            return std::vector<float>(outputData, outputData + outputSize);
        }
    }
}