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
                m_Session = Ort::Session(m_Env, modelPath.c_str(), m_SessionOptions);
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

            const char* inputName = m_Session.GetInputName(0, allocator);
            const char* outputName = m_Session.GetOutputName(0, allocator);

            Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(memInfo, const_cast<float*>(input.data()), input.size(), shape.data(), shape.size());

            auto outputTensor = m_Session.Run(Ort::RunOptions{ nullptr }, &inputName, &inputTensor, 1, &outputName, 1);

            float* outputData = outputTensor.front().GetTensorMutableData<float>();
            size_t outputSize = outputTensor.front().GetTensorTypeAndShapeInfo().GetElementCount();
            return std::vector<float>(outputData, outputData + outputSize);
        }
    }
}