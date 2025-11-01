include(FetchContent)

# GLFW
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

# ImGui (docking branch)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

# Wrap Dear ImGui into a proper CMake target
FetchContent_GetProperties(imgui)
if (NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()
set(IMGUI_DIR ${imgui_SOURCE_DIR})

# Vulkan backend
find_package(Vulkan REQUIRED)

add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)

# Link deps for Vulkan, not OpenGL
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

#
# ONNX Runtime
# The renderer's neural components will rely on ONNX Runtime in the future. The
# configuration below keeps the integration lightweight by limiting execution
# providers and turning off optional components that are not required for a CPU
#-centric static build.
#

# Disable optional functionality to keep the build conservative and portable.
set(onnxruntime_BUILD_SHARED_LIB OFF CACHE BOOL "Build ONNX Runtime as static libraries" FORCE)
set(onnxruntime_BUILD_UNIT_TESTS OFF CACHE BOOL "Disable ONNX Runtime unit tests" FORCE)
set(onnxruntime_ENABLE_CPU_EP ON CACHE BOOL "Enable the CPU execution provider" FORCE)
set(onnxruntime_ENABLE_CUDA OFF CACHE BOOL "Disable CUDA execution provider" FORCE)
set(onnxruntime_ENABLE_ROCM OFF CACHE BOOL "Disable ROCm execution provider" FORCE)
set(onnxruntime_ENABLE_OPENVINO OFF CACHE BOOL "Disable OpenVINO execution provider" FORCE)
set(onnxruntime_ENABLE_TENSORRT OFF CACHE BOOL "Disable TensorRT execution provider" FORCE)
set(onnxruntime_ENABLE_DNNL OFF CACHE BOOL "Disable oneDNN execution provider" FORCE)
set(onnxruntime_ENABLE_NNAPI OFF CACHE BOOL "Disable NNAPI execution provider" FORCE)
set(onnxruntime_ENABLE_COREML OFF CACHE BOOL "Disable CoreML execution provider" FORCE)
set(onnxruntime_ENABLE_TVM OFF CACHE BOOL "Disable TVM execution provider" FORCE)
set(onnxruntime_ENABLE_VITISAI OFF CACHE BOOL "Disable Vitis AI execution provider" FORCE)
set(onnxruntime_ENABLE_OPENCL OFF CACHE BOOL "Disable OpenCL execution provider" FORCE)
set(onnxruntime_ENABLE_MIGRAPHX OFF CACHE BOOL "Disable MIGraphX execution provider" FORCE)

# Fetch the ONNX Runtime dependency so that targets can link against
# onnxruntime::onnxruntime in future renderer components.
FetchContent_Declare(
  onnxruntime
  GIT_REPOSITORY https://github.com/microsoft/onnxruntime.git
  GIT_TAG v1.23.2
)
FetchContent_MakeAvailable(onnxruntime)