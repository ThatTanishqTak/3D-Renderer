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

# Paths (official layout)
set(ORT_ROOT    "${ort_sdk_SOURCE_DIR}")
set(ORT_INCLUDE "${ORT_ROOT}/include")
set(ORT_LIB     "${ORT_ROOT}/lib/onnxruntime.lib")
set(ORT_DLL     "${ORT_ROOT}/lib/onnxruntime.dll")

# Verify files exist
if(NOT EXISTS "${ORT_INCLUDE}/onnxruntime_cxx_api.h")
  message(FATAL_ERROR "ONNX Runtime headers missing! Expected: ${ORT_INCLUDE}/onnxruntime_cxx_api.h")
endif()
if(NOT EXISTS "${ORT_LIB}")
  message(FATAL_ERROR "ONNX Runtime import library missing! Expected: ${ORT_LIB}")
endif()
if(NOT EXISTS "${ORT_DLL}")
  message(FATAL_ERROR "ONNX Runtime DLL missing! Expected: ${ORT_DLL}")
endif()

# Modern CMake imported targets
add_library(onnxruntime::headers INTERFACE)
target_include_directories(onnxruntime::headers INTERFACE "${ORT_INCLUDE}")

add_library(onnxruntime::onnxruntime STATIC IMPORTED)
set_target_properties(onnxruntime::onnxruntime PROPERTIES
  IMPORTED_LOCATION "${ORT_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${ORT_INCLUDE}"
)

# Copy DLL to output directory
add_custom_command(
  TARGET Trident-Forge POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${ORT_DLL}"
          "$<TARGET_FILE_DIR:Trident-Forge>"
  COMMENT "Copying onnxruntime.dll → output"
)

message(STATUS "ONNX Runtime v${ORT_VERSION} (CPU) ready!")