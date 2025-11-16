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
# ONNX Runtime (pre-built CPU, x64 Windows)
# -------------------------------------------------
# We rely on the locally committed pre-built SDK that ships ONNX IR 12+
# support and is compatible with Visual Studio 2022 / MSVC in C++20 mode.
set(ORT_VERSION "1.23.2" CACHE STRING "ONNX Runtime version")
set(ORT_ROOT    "${CMAKE_SOURCE_DIR}/Trident/vendor/onnxruntime")
set(ORT_INCLUDE "${ORT_ROOT}/include")

# The vendored package is laid out like the official Windows zip: headers
# under include/ and binaries under lib/. Probe the common locations so we
# surface clear errors if the committed SDK is incomplete.
set(ORT_LIB_CANDIDATES
  "${ORT_ROOT}/lib/onnxruntime.lib"
)
set(ORT_DLL_CANDIDATES
  "${ORT_ROOT}/lib/onnxruntime.dll"
)

find_file(ORT_LIB NAMES onnxruntime.lib PATHS ${ORT_LIB_CANDIDATES})
find_file(ORT_DLL NAMES onnxruntime.dll PATHS ${ORT_DLL_CANDIDATES})

# Verify files
if(NOT EXISTS "${ORT_INCLUDE}/onnxruntime_cxx_api.h")
  message(FATAL_ERROR "ONNX Runtime headers missing! Expected: ${ORT_INCLUDE}/onnxruntime_cxx_api.h")
endif()
if(NOT EXISTS "${ORT_ROOT}/lib/onnxruntime.lib")
  message(FATAL_ERROR "ONNX Runtime import library missing! Checked: ${ORT_LIB_CANDIDATES}")
endif()
if(NOT EXISTS "${ORT_ROOT}/lib/onnxruntime.dll")
  message(FATAL_ERROR "ONNX Runtime DLL missing! Checked: ${ORT_DLL_CANDIDATES}")
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
message(STATUS "ONNX Runtime v${ORT_VERSION} (CPU) ready from vendored SDK!")