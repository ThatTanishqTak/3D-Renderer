#pragma once

#include "Core/Utilities.h"

#include <onnxruntime_cxx_api.h>
#include <onnxruntime_c_api.h>

#include <memory>
#include <string>
#include <vector>

namespace Trident
{
    namespace AI
    {
        class ONNXRuntime
        {
        public:
            ONNXRuntime();
            ~ONNXRuntime();

            // Toggle CUDA provider usage, keeping the GPU path as the primary accelerator when enabled.
            void EnableCUDA(bool enableCUDA);

            // Temporary compatibility shim so existing DirectML toggles route to the CUDA switch until callers migrate.
            void EnableDirectML(bool enableDirectML);

            // Toggle the CPU execution provider to act as a deterministic fallback when accelerators are unavailable.
            void EnableCPUFallback(bool enableCPUFallback);

            bool LoadModel(const std::string& modelPath);
            std::vector<float> Run(const std::vector<float>& input, const std::vector<int64_t>& shape);

            // Preallocate a float tensor buffer using the engine allocation tracker; ownership stays with the caller.
            std::unique_ptr<float, void(*)(void*)> AllocateTensorBuffer(size_t elementCount) const;

            // Helper that binds a caller-owned buffer to an Ort tensor, ensuring tensor lifetime stays within the calling thread.
            Ort::Value CreateTensorFromBuffer(float* buffer, size_t elementCount, const std::vector<int64_t>& shape);

            // Cached input names mirror session metadata so callers avoid repeated allocator work on hot paths.
            const std::vector<std::string>& GetInputNames() const noexcept;

            // Cached output names mirror session metadata so callers avoid repeated allocator work on hot paths.
            const std::vector<std::string>& GetOutputNames() const noexcept;

        private:
            // Configure execution providers in a VS2022 friendly manner while keeping thread-affinity to the loader thread.
            void ConfigureExecutionProviders();

            // Cache metadata such as IO names and shapes to reduce per-inference allocations and validation steps.
            void CacheIOBindingMetadata();

            // Utility to multiply tensor dimensions with overflow checks to validate buffer sizing.
            size_t CalculateElementCount(const std::vector<int64_t>& shape) const;

            // Determine whether the provided tensor dimensions contain dynamic placeholders requiring runtime inspection.
            bool IsShapeDynamic(const std::vector<int64_t>& shape) const;

            bool m_EnableCUDA; // Flag instructing the loader to append the CUDA execution provider first.
            bool m_EnableCPUFallback; // Flag indicating whether the CPU provider must be appended for deterministic fallback.
            bool m_IsCUDAActive; // Tracks whether CUDA activation succeeded so telemetry can flag failures.
            bool m_IsCPUActive; // Tracks CPU provider activation to ensure at least one provider remains available.

            Ort::Env m_Env;
            Ort::Session m_Session{ nullptr };
            Ort::SessionOptions m_SessionOptions;
            Ort::MemoryInfo m_CpuMemoryInfo; // Shared CPU memory descriptor for all tensors created by this runtime.

            std::vector<std::string> m_InputNames; // Cached input tensor names retrieved during model load to avoid repeated queries.
            std::vector<std::vector<int64_t>> m_InputShapes; // Cached input shapes to validate runtime tensors and detect dynamic dims.
            std::vector<bool> m_InputShapeIsDynamic; // Flags describing which input shapes contain dynamic placeholders.
            std::vector<std::string> m_OutputNames; // Cached output tensor names retrieved during model load to avoid repeated queries.
            std::vector<std::vector<int64_t>> m_OutputShapes; // Cached output shapes to size preallocated buffers safely.
            std::vector<size_t> m_OutputElementCounts; // Element counts for static outputs so buffers can be reused without recomputation.
            std::vector<bool> m_OutputShapeIsDynamic; // Flags describing which outputs require runtime shape inspection.
        };
    }
}