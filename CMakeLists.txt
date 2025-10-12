cmake_minimum_required(VERSION 3.20)
project(3D-Renderer LANGUAGES CXX)

# Enable discovery of helper modules (e.g., dependency declarations).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Configure third-party dependencies before fetching them.
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)

# Centralize dependency fetching logic for third-party libraries.
include(Deps)

# Share project-wide tooling helpers such as warnings and sanitizers.
include(Tooling)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/MP)
  add_compile_options(/utf-8)
endif()

add_subdirectory(Trident)
add_subdirectory(Trident-Forge)

# Apply consistent warnings and sanitizer settings to the primary targets.
enable_project_warnings(Trident)
enable_project_warnings(Trident-Forge)
enable_sanitizers(Trident)
enable_sanitizers(Trident-Forge)