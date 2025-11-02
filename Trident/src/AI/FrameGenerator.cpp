#include "AI/FrameGenerator.h"

#include "Core/Utilities.h"

#include <algorithm>
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

            std::vector<const char*> l_InputNames;
            l_InputNames.reserve(m_InputBindings.size());
            for (const TensorBinding& it_Binding : m_InputBindings)
            {
                l_InputNames.push_back(it_Binding.m_Name.c_str());
            }

            std::vector<Ort::Value> l_InputTensors;
            l_InputTensors.reserve(m_InputBindings.size());

            try
            {
                // TODO: Extend this to support multi-input models when the engine begins to leverage them.
                Ort::Value a_FrameTensor = m_RuntimeContext->CreateTensorFloat(frameData, l_PrimaryInput.m_Shape);
                l_InputTensors.emplace_back(std::move(a_FrameTensor));

                std::vector<const char*> l_OutputNames;
                l_OutputNames.reserve(m_OutputBindings.size());
                for (const TensorBinding& it_Binding : m_OutputBindings)
                {
                    l_OutputNames.push_back(it_Binding.m_Name.c_str());
                }

                auto a_OutputTensors = m_RuntimeContext->Run(m_ModelKey, l_InputNames, l_InputTensors, l_OutputNames);

                m_LastOutputTensor.clear();
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

                    const size_t l_WriteOffset = m_LastOutputTensor.size();
                    m_LastOutputTensor.resize(l_WriteOffset + l_ElementCount);
                    std::copy(l_Data, l_Data + l_ElementCount, m_LastOutputTensor.begin() + static_cast<std::ptrdiff_t>(l_WriteOffset));
                }
            }
            catch (const Ort::Exception& l_Exception)
            {
                TR_CORE_ERROR("ONNX runtime rejected a frame submission: {}", l_Exception.what());
                return false;
            }
            catch (const std::exception& l_Exception)
            {
                TR_CORE_ERROR("Unexpected failure during AI frame processing: {}", l_Exception.what());
                return false;
            }

            return !m_LastOutputTensor.empty();
        }

        std::span<const int64_t> FrameGenerator::GetPrimaryInputShape() const
        {
            if (m_InputBindings.empty())
            {
                return {};
            }

            return m_InputBindings.front().m_Shape;
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
            m_IsInitialised = false;
            m_InputBindings.clear();
            m_OutputBindings.clear();
            m_LastOutputTensor.clear();
        }
    }
}