# Tooling.cmake
# This module centralizes compiler warnings, sanitizer support, and output directories
# so that every target can share consistent project-wide tooling defaults.

function(enable_project_warnings target)
  # Apply compiler warnings according to the active toolchain.
  if (MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
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

# Standardize binary and library output directories across the entire project.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)