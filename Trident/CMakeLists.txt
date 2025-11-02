cmake_minimum_required(VERSION 3.20)
project(Trident LANGUAGES C CXX)

add_definitions(-DGLFW_INCLUDE_VULKAN)

if(NOT DEFINED ENV{VULKAN_SDK})
  message(FATAL_ERROR "Please set the VULKAN_SDK environment variable to your Vulkan SDK path")
endif()
set(VULKAN_SDK_PATH $ENV{VULKAN_SDK})

find_path(VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h HINTS "${VULKAN_SDK_PATH}/Include")
find_library(VULKAN_LIBRARY NAMES vulkan-1 HINTS "${VULKAN_SDK_PATH}/Lib")

if(NOT VULKAN_INCLUDE_DIR OR NOT VULKAN_LIBRARY)
  message(FATAL_ERROR "Unable to find Vulkan headers or lib in ${VULKAN_SDK_PATH}")
endif()

add_definitions(-DGLFW_INCLUDE_VULKAN)
find_package(Vulkan REQUIRED)

# FetchContent is used for third-party dependencies such as ONNX Runtime and Assimp.
include(FetchContent)

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/glm>
)

add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/spdlog/include>
)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb>
)

add_library(tinyexr INTERFACE)
target_include_directories(tinyexr INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/tinyexr>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/tinyexr/deps/miniz>
)
target_sources(tinyexr INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tinyexr/deps/miniz/miniz.c
)
# Provide KTX texture loading headers and import library to the build.
add_library(libktx INTERFACE IMPORTED)
set_target_properties(libktx PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libktx/include"
)
if(WIN32)
  # Visual Studio/MSVC consumes the static import library.
  set_target_properties(libktx PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libktx/lib/ktx.lib"
  )
else()
  # Non-Windows toolchains expect the shared object name.
  set_target_properties(libktx PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libktx/lib/libktx.so"
  )
endif()

# Provide a portable imported target for ONNX Runtime so CMake can link against it.
if(NOT TARGET onnxruntime::onnxruntime)
  # Allow developers to override the default SDK location when they have a custom install.
  set(TRIDENT_ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime" CACHE PATH
    "Root directory containing the ONNX Runtime SDK")

  # Discover the public headers that ship with the runtime distribution.
  find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h onnxruntime_c_api.h
    HINTS
      "${TRIDENT_ONNXRUNTIME_ROOT}/include"
      "${TRIDENT_ONNXRUNTIME_ROOT}"
  )

  # Locate the linker import library or shared object depending on the platform.
  find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    HINTS
      "${TRIDENT_ONNXRUNTIME_ROOT}/lib"
      "${TRIDENT_ONNXRUNTIME_ROOT}/lib64"
      "${TRIDENT_ONNXRUNTIME_ROOT}"
  )

    # When the SDK is missing on Windows, pull in a known-good binary distribution automatically.
  if((NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY) AND WIN32)
    set(TRIDENT_DEFAULT_ONNXRUNTIME_VERSION "1.16.3")
    set(TRIDENT_DEFAULT_ONNXRUNTIME_PACKAGE "onnxruntime-win-x64-${TRIDENT_DEFAULT_ONNXRUNTIME_VERSION}")
    set(TRIDENT_DEFAULT_ONNXRUNTIME_URL
      "https://github.com/microsoft/onnxruntime/releases/download/v${TRIDENT_DEFAULT_ONNXRUNTIME_VERSION}/${TRIDENT_DEFAULT_ONNXRUNTIME_PACKAGE}.zip")

    # Store the downloaded SDK inside the build tree so multiple configurations can reuse it.
    set(TRIDENT_ONNXRUNTIME_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_deps/onnxruntime")

    FetchContent_Declare(trident_onnxruntime_sdk
      URL "${TRIDENT_DEFAULT_ONNXRUNTIME_URL}"
      SOURCE_DIR "${TRIDENT_ONNXRUNTIME_DOWNLOAD_DIR}/${TRIDENT_DEFAULT_ONNXRUNTIME_PACKAGE}"
    )

    FetchContent_GetProperties(trident_onnxruntime_sdk)
    if(NOT trident_onnxruntime_sdk_POPULATED)
      message(STATUS "Fetching ONNX Runtime ${TRIDENT_DEFAULT_ONNXRUNTIME_VERSION} because no local SDK was found.")
      FetchContent_Populate(trident_onnxruntime_sdk)
    endif()

    set(TRIDENT_ONNXRUNTIME_ROOT "${trident_onnxruntime_sdk_SOURCE_DIR}" CACHE PATH
      "Root directory containing the ONNX Runtime SDK" FORCE)

    # Retry discovery with the freshly downloaded SDK.
    find_path(ONNXRUNTIME_INCLUDE_DIR
      NAMES onnxruntime_cxx_api.h onnxruntime_c_api.h
      HINTS
        "${TRIDENT_ONNXRUNTIME_ROOT}/include"
        "${TRIDENT_ONNXRUNTIME_ROOT}"
      NO_DEFAULT_PATH
    )

    find_library(ONNXRUNTIME_LIBRARY
      NAMES onnxruntime
      HINTS
        "${TRIDENT_ONNXRUNTIME_ROOT}/lib"
        "${TRIDENT_ONNXRUNTIME_ROOT}/lib64"
        "${TRIDENT_ONNXRUNTIME_ROOT}"
      NO_DEFAULT_PATH
    )
  endif()

  if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR
      "Unable to locate ONNX Runtime. Update TRIDENT_ONNXRUNTIME_ROOT to a valid SDK directory.")
  endif()

  add_library(onnxruntime::onnxruntime UNKNOWN IMPORTED)
  set_target_properties(onnxruntime::onnxruntime PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
  )

  if(WIN32)
    # Windows packages ship a DLL alongside an import library. Prefer both when available.
    find_file(ONNXRUNTIME_DLL
      NAMES onnxruntime.dll
      HINTS
        "${TRIDENT_ONNXRUNTIME_ROOT}/bin"
        "${TRIDENT_ONNXRUNTIME_ROOT}"
    )

    if(ONNXRUNTIME_DLL)
      set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_IMPLIB "${ONNXRUNTIME_LIBRARY}"
        IMPORTED_LOCATION "${ONNXRUNTIME_DLL}"
      )
    else()
      # Fallback for static or unusual packaging layouts.
      set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
      )
    endif()
  else()
    # Non-Windows platforms link directly against the shared object or static library.
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
      IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
    )
  endif()
endif()

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
# Keep exporters disabled because the renderer only consumes assets, however
# enable every importer to maximise format coverage. If build times become
# prohibitive we can revisit this and disable unneeded importers explicitly.
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT ON CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(assimp
  URL https://github.com/assimp/assimp/archive/refs/tags/v5.3.1.zip
)
FetchContent_MakeAvailable(assimp)

set(IMGUIZMO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imguizmo")
add_library(imguizmo STATIC ${IMGUIZMO_ROOT}/ImGuizmo.cpp)

target_include_directories(imguizmo PUBLIC
  $<BUILD_INTERFACE:${IMGUIZMO_ROOT}>
)

# Pulls in ImGui’s public include dirs automatically
target_link_libraries(imguizmo PUBLIC imgui)

file(GLOB_RECURSE TRIDENT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

add_library(${PROJECT_NAME} STATIC ${TRIDENT_SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/nanosvg/src>
)

# Link against windowing and UI middleware fetched via FetchContent.
target_link_libraries(${PROJECT_NAME} PRIVATE
  glfw
  imgui
)

# Keep existing public dependencies available to dependents.
target_link_libraries(${PROJECT_NAME} PUBLIC
  Vulkan::Vulkan
  glm
  spdlog
  imguizmo
  stb
  tinyexr
  libktx
  assimp::assimp
  onnxruntime::onnxruntime
)

option(TRIDENT_ENABLE_VALIDATION "Enable Vulkan Validation Layers" ON)
if(TRIDENT_ENABLE_VALIDATION)
  target_compile_definitions(${PROJECT_NAME} PUBLIC TRIDENT_VALIDATION_LAYERS=1)
endif()

option(TRIDENT_ENABLE_VIEWPORTS "Enable ImGui Viewports" ON)
if(TRIDENT_ENABLE_VIEWPORTS)
  target_compile_definitions(${PROJECT_NAME} PUBLIC TRIDENT_IMGUI_VIEWPORTS=1)
endif()

option(TRIDENT_ENABLE_LOADER_REGRESSION_CHECKS "Run loader regression checks during startup" ON)
if(TRIDENT_ENABLE_LOADER_REGRESSION_CHECKS)
  target_compile_definitions(${PROJECT_NAME} PUBLIC TRIDENT_ENABLE_LOADER_REGRESSION_CHECKS=1)
endif()

message(STATUS "Vulkan SDK Path: ${VULKAN_SDK_PATH}")