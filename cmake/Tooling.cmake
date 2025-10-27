# cmake/Tooling.cmake
# This module centralizes compiler warnings, sanitizer support, and output directories
# so that every target can share consistent project-wide tooling defaults.

function(enable_project_warnings target)
  if (MSVC)
    # Force /MT and disable iterator debugging to match PhysX libraries
    target_compile_options(${target} PRIVATE /W4 /permissive- /MT)
    # Disable _DEBUG and define NDEBUG to match PhysX release configuration
    target_compile_definitions(${target} PRIVATE _ITERATOR_DEBUG_LEVEL=0 _DEBUG=0 NDEBUG=1)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wno-unused-parameter)
  endif()
endfunction()

function(enable_sanitizers target)
  # Sanitizers are helpful during development/debug configurations on non-MSVC toolchains.
  if (CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo" AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined)
    target_link_libraries(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

if(IS_MULTI_CONFIG)
  set(OUTPUT_CONFIG "$<CONFIG>")
else()
  set(OUTPUT_CONFIG "${CMAKE_BUILD_TYPE}")
endif()

set(OS_NAME ${CMAKE_SYSTEM_NAME})
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ARCH "x64")
else()
  set(ARCH "x86")
endif()

set(OUTPUT_DIR "${OS_NAME}-${OUTPUT_CONFIG}-${ARCH}/${PROJECT_NAME}")
# Standardize binary and library output directories across the entire project.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${OUTPUT_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${OUTPUT_DIR})

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
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)