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
# ONNX Runtime v1.23.2 – Pre-built CPU (x64 Windows)
# -------------------------------------------------
set(ORT_VERSION "1.23.2")
set(ORT_PACKAGE "onnxruntime-win-x64-${ORT_VERSION}")

FetchContent_Declare(
  ort_sdk
  URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_PACKAGE}.zip"
  SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/ort-sdk"
)

FetchContent_GetProperties(ort_sdk)
if(NOT ort_sdk_POPULATED)
  message(STATUS "Downloading ONNX Runtime v${ORT_VERSION} (CPU)...")
  FetchContent_Populate(ort_sdk)
endif()

set(ORT_ROOT    "${ort_sdk_SOURCE_DIR}")
set(ORT_INCLUDE "${ORT_ROOT}/include")
set(ORT_LIB     "${ORT_ROOT}/lib/onnxruntime.lib")
set(ORT_DLL     "${ORT_ROOT}/lib/onnxruntime.dll")

# Verify files
if(NOT EXISTS "${ORT_INCLUDE}/onnxruntime_cxx_api.h")
  message(FATAL_ERROR "ONNX Runtime headers missing! Expected: ${ORT_INCLUDE}/onnxruntime_cxx_api.h")
endif()
if(NOT EXISTS "${ORT_LIB}")
  message(FATAL_ERROR "ONNX Runtime import library missing! Expected: ${ORT_LIB}")
endif()
if(NOT EXISTS "${ORT_DLL}")
  message(FATAL_ERROR "ONNX Runtime DLL missing! Expected: ${ORT_DLL}")
endif()

# -------------------------------------------------
# Imported targets (modern CMake)
# -------------------------------------------------
add_library(onnxruntime_headers INTERFACE)
target_include_directories(onnxruntime_headers INTERFACE "${ORT_INCLUDE}")

add_library(onnxruntime_lib STATIC IMPORTED)
set_target_properties(onnxruntime_lib PROPERTIES
  IMPORTED_LOCATION "${ORT_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${ORT_INCLUDE}"
)

# Public aliases – these are the names you link against
add_library(onnxruntime::onnxruntime ALIAS onnxruntime_lib)
add_library(onnxruntime::headers      ALIAS onnxruntime_headers)

# Export the DLL path for the copy command later
set(ONNXRUNTIME_DLL_PATH "${ORT_DLL}" CACHE INTERNAL "Path to onnxruntime.dll")
message(STATUS "ONNX Runtime v${ORT_VERSION} (CPU) ready!")