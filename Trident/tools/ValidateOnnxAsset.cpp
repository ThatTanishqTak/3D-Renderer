#include "AI/OnnxRuntimeContext.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

// Simple build-time validator that checks the bundled ONNX asset against the
// embedded runtime capabilities. The goal is to catch IR or opset mismatches
// during CI instead of shipping a model the runtime cannot load. There is room
// to expand this to emit richer diagnostics (such as opset import summaries)
// once additional runtime metadata is available.
int main(int argc, char** argv)
{
    const std::filesystem::path l_DefaultPath{ TRIDENT_DEFAULT_MODEL_PATH };
    const std::filesystem::path l_ModelPath = argc > 1 ? std::filesystem::path{ argv[1] } : l_DefaultPath;

    std::cout << "Validating ONNX model at '" << l_ModelPath << "' against IR cap "
        << TRIDENT_MAX_ONNX_IR << "..." << std::endl;

    if (!std::filesystem::exists(l_ModelPath))
    {
        std::cerr << "Model file is missing: " << l_ModelPath << std::endl;
        return 1;
    }

    const std::optional<uint64_t> l_ModelIr = Trident::AI::OnnxRuntimeContext::ReadOnnxIrVersion(l_ModelPath);
    if (!l_ModelIr)
    {
        std::cerr << "Unable to read IR version from: " << l_ModelPath << std::endl;
        return 2;
    }

    if (*l_ModelIr > static_cast<uint64_t>(TRIDENT_MAX_ONNX_IR))
    {
        std::cerr << "Model targets IR " << *l_ModelIr << " which exceeds the bundled runtime cap of "
            << TRIDENT_MAX_ONNX_IR << std::endl;
        return 3;
    }

    try
    {
        // Attempt a lightweight load to prove the runtime can parse the graph.
        // Future improvements could load a dedicated regression input to assert
        // tensor bindings stay compatible.
        Trident::AI::OnnxRuntimeContext::Get().LoadModel("validation_probe", l_ModelPath);
    }
    catch (const Ort::Exception& l_Exception)
    {
        std::cerr << "ONNX Runtime rejected the model: " << l_Exception.what() << std::endl;
        return 4;
    }

    std::cout << "Model IR " << *l_ModelIr << " is compatible with the bundled runtime." << std::endl;
    return 0;
}