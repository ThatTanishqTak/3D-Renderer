include(FetchContent)

# -------------------------------------------------
# GLFW
# -------------------------------------------------
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

# -------------------------------------------------
# ImGui (docking branch)
# -------------------------------------------------
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

FetchContent_GetProperties(imgui)
if (NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()
set(IMGUI_DIR ${imgui_SOURCE_DIR})

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

target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

# -------------------------------------------------
# ONNX Runtime – static build
# -------------------------------------------------
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

FetchContent_Declare(
  onnxruntime
  GIT_REPOSITORY https://github.com/microsoft/onnxruntime.git
  GIT_TAG v1.23.2
)
FetchContent_MakeAvailable(onnxruntime)

# -------------------------------------------------
# Fix: Properly expose ONNX Runtime headers
# -------------------------------------------------
# Get the source directory
get_target_property(ONNXRUNTIME_SOURCE_DIR onnxruntime SOURCE_DIR)
if(NOT ONNXRUNTIME_SOURCE_DIR)
  message(FATAL_ERROR "ONNX Runtime source directory not found!")
endif()

# The actual include path is <source>/include
set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_SOURCE_DIR}/include")

# Create interface library to propagate includes
add_library(onnxruntime_headers INTERFACE)
target_include_directories(onnxruntime_headers INTERFACE
  "${ONNXRUNTIME_INCLUDE_DIR}"
)

# Optional: make target_link_libraries cleaner
add_library(onnxruntime::onnxruntime ALIAS onnxruntime)
add_library(onnxruntime::headers      ALIAS onnxruntime_headers)