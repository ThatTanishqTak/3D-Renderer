#pragma once

#include "AI/OnnxRuntimeContext.h"

#include <filesystem>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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
             * @brief Push the latest frame into the model and persist its outputs.
             *
             * @param frameData Input tensor representing the rendered frame.
             * @return true when inference completed successfully.
             */
            bool ProcessFrame(std::span<const float> frameData);

            /**
             * @brief Expose the cached output buffer so the renderer can feed later passes.
             */
            const std::vector<float>& GetLastOutput() const { return m_LastOutputTensor; }

            /**
             * @brief Provide callers with the expected input shape.
             */
            std::span<const int64_t> GetPrimaryInputShape() const;

        private:
            struct TensorBinding
            {
                std::string m_Name{};                ///< Graph binding name used during inference runs.
                std::vector<int64_t> m_Shape{};      ///< Cached tensor shape describing the expected dimensions.
                size_t m_ElementCount = 0;           ///< Flattened element count derived from the shape for validation.
            };

            bool CacheModelBindings(const Ort::Session& session);
            void ResetState();

            std::string m_ModelKey{};                                   ///< Identifier supplied to OnnxRuntimeContext.
            AI::OnnxRuntimeContext* m_RuntimeContext = nullptr;          ///< Pointer to the shared runtime controller.
            bool m_IsInitialised = false;                                ///< Guard flag checked before scheduling inference.
            std::vector<TensorBinding> m_InputBindings;                  ///< Cached description of model inputs.
            std::vector<TensorBinding> m_OutputBindings;                 ///< Cached description of model outputs.
            std::vector<float> m_LastOutputTensor;                       ///< Flattened buffer storing the latest AI result.
        };
    }
}