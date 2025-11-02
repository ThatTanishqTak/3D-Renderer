cmake_minimum_required(VERSION 3.20)
project(3D-Renderer LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)

include(Deps)          # <-- fetches ONNX Runtime, creates targets
include(Tooling)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/MP /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

add_subdirectory(Trident)
add_subdirectory(Trident-Forge)

enable_project_warnings(Trident)
enable_project_warnings(Trident-Forge)
enable_sanitizers(Trident)
enable_sanitizers(Trident-Forge)