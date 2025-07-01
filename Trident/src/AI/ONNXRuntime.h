#pragma once

#include "Core/Utilities.h"

#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>

namespace Trident
{
    namespace AI
    {
        class ONNXRuntime
        {
        public:
            ONNXRuntime();
            ~ONNXRuntime();

            bool LoadModel(const std::string& modelPath);
            std::vector<float> Run(const std::vector<float>& input, const std::vector<int64_t>& shape);

        private:
            Ort::Env m_Env;
            Ort::Session m_Session{ nullptr };
            Ort::SessionOptions m_SessionOptions;
        };
    }
}