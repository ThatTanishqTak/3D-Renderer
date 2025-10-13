# Tooling.cmake
# This module centralizes compiler warnings, sanitizer support, and output directories
# so that every target can share consistent project-wide tooling defaults.

function(enable_project_warnings target)
  # Apply compiler warnings according to the active toolchain.
  if (MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    add_compile_definitions(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(-Wno-dev)
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