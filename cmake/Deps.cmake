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
  # Default vendored layout that mirrors the official Windows zip structure.
  "${ORT_ROOT}/lib"
  # Optional override if developers unpack the SDK elsewhere while keeping the
  # same folder naming.
  "$ENV{ORT_ROOT}/lib"
)
set(ORT_DLL_CANDIDATES
  # Default vendored layout that mirrors the official Windows zip structure.
  "${ORT_ROOT}/lib"
  # Optional override if developers unpack the SDK elsewhere while keeping the
  # same folder naming.
  "$ENV{ORT_ROOT}/lib"
)

find_file(ORT_LIB NAMES onnxruntime.lib PATHS ${ORT_LIB_CANDIDATES})
find_file(ORT_DLL NAMES onnxruntime.dll PATHS ${ORT_DLL_CANDIDATES})

# Detect Git LFS pointer files early so developers get actionable guidance
# instead of confusing CMake errors about missing libraries.
function(trident_assert_not_lfs_pointer a_Path a_Label)
  # A valid Windows import library or DLL will be much larger than a pointer.
  file(SIZE "${a_Path}" l_PossiblePointerSize)
  if(l_PossiblePointerSize LESS 1024)
    file(READ "${a_Path}" l_PossiblePointerHeader LIMIT 256)
    if(l_PossiblePointerHeader MATCHES "version https://git-lfs.github.com/spec/v1")
      message(FATAL_ERROR "${a_Label} looks like a Git LFS placeholder. Please run 'git lfs pull' to fetch the real binaries.")
    endif()
  endif()
endfunction()

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

trident_assert_not_lfs_pointer("${ORT_LIB}" "ONNX Runtime import library")
trident_assert_not_lfs_pointer("${ORT_DLL}" "ONNX Runtime DLL")

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