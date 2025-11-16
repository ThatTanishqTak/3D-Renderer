#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <onnxruntime_cxx_api.h>

namespace Trident
{
    namespace AI
    {
        /**
         * Thin manager that centralises ONNX Runtime usage for the renderer.
         *
         * The goal is to hide the slightly verbose raw C API behind an easy to
         * consume interface that is safe to extend later. The wrapper
         * initialises the runtime environment once, owns per-model sessions and
         * provides helpers for typical inference flows. A light abstraction now
         * will make it painless to layer scheduling, batching or profiling on
         * top when the engine eventually grows more demanding machine learning
         * features.
         */
        class OnnxRuntimeContext final
        {
        public:
            /**
             * Access the singleton context instance.
             */
            static OnnxRuntimeContext& Get();

            OnnxRuntimeContext(const OnnxRuntimeContext&) = delete;
            OnnxRuntimeContext(OnnxRuntimeContext&&) = delete;
            OnnxRuntimeContext& operator=(const OnnxRuntimeContext&) = delete;
            OnnxRuntimeContext& operator=(OnnxRuntimeContext&&) = delete;

            /**
             * Configure the default thread pool sizes. The defaults are chosen
             * conservatively so the renderer stays responsive, but the values
             * can be tuned per project.
             */
            void ConfigureThreading(uint32_t interOpThreads, uint32_t intraOpThreads);

            /**
             * Load (or fetch from cache) the ONNX model stored at the provided
             * path. When successful the model becomes addressable via
             * modelName.
             */
            const Ort::Session& LoadModel(std::string_view modelName, const std::filesystem::path& modelPath);

            /**
             * Remove a previously cached model session. Useful for hot-reload
             * workflows in development tools.
             */
            void UnloadModel(std::string_view modelName);

            /**
             * Execute an inference run for the named model. Input and output
             * names must match the graph signature.
             */
            std::vector<Ort::Value> Run(std::string_view modelName, std::span<const char* const> inputNames, std::span<const Ort::Value> inputs,
                std::span<const char* const> outputNames);

            /**
             * Helper to allocate a CPU-backed tensor that feeds directly into a
             * model. More overloads can be introduced later as we support more
             * data types or custom memory allocators.
             */
            Ort::Value CreateTensorFloat(std::span<const float> values, std::span<const int64_t> shape) const;

            /**
             * Expose the underlying ONNX Runtime environment for advanced
             * diagnostics when needed. Normal callers should not need this but
             * it provides an escape hatch without compromising encapsulation.
             */ 
            const Ort::Env& GetEnvironment() const;

        private:
            OnnxRuntimeContext();
            ~OnnxRuntimeContext() = default;

            std::filesystem::path SanitizeModelPath(const std::filesystem::path& modelPath) const;
            bool HandleModelLoadFailure(const std::filesystem::path& modelPath, const Ort::Exception& runtimeError) const;
            std::optional<uint64_t> ReadOnnxIrVersion(const std::filesystem::path& modelPath) const;
            std::optional<uint64_t> ParseMaxSupportedIrVersion(std::string_view runtimeMessage) const;

            Ort::Env m_Environment;
            Ort::SessionOptions m_DefaultSessionOptions;
            std::unordered_map<std::string, std::shared_ptr<Ort::Session>> m_Sessions;
            std::mutex m_SessionMutex;

            static constexpr OrtLoggingLevel s_DefaultLogLevel = ORT_LOGGING_LEVEL_WARNING;
            static constexpr uint32_t s_DefaultInterOpThreads = 1;
            static constexpr uint32_t s_DefaultIntraOpThreads = 1;
        };
    }
}