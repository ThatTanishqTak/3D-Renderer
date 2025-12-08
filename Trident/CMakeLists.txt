cmake_minimum_required(VERSION 3.20)
project(Trident LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_definitions(-DGLFW_INCLUDE_VULKAN)

# Vulkan SDK sanity check
if(NOT DEFINED ENV{VULKAN_SDK})
  message(FATAL_ERROR "Please set the VULKAN_SDK environment variable to your Vulkan SDK path")
endif()
set(VULKAN_SDK_PATH $ENV{VULKAN_SDK})

find_path(VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h HINTS "${VULKAN_SDK_PATH}/Include")
find_library(VULKAN_LIBRARY NAMES vulkan-1 HINTS "${VULKAN_SDK_PATH}/Lib")
if(NOT VULKAN_INCLUDE_DIR OR NOT VULKAN_LIBRARY)
  message(FATAL_ERROR "Unable to find Vulkan headers or lib in ${VULKAN_SDK_PATH}")
endif()


# FFmpeg auto-download + configuration
set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/FFmpeg")

# URL of the FFmpeg dev build zip (headers + libs)
set(TRIDENT_FFMPEG_URL
    "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip"
    CACHE STRING "URL of FFmpeg dev build (zip with include/ and lib/)" )

# Use presence of avcodec.lib as the "installed" marker on Windows.
# On non-Windows, you can adjust this to whatever shared objects you use.
if(WIN32)
  set(TRIDENT_FFMPEG_MARKER "${FFMPEG_ROOT}/lib/avcodec.lib")
else()
  set(TRIDENT_FFMPEG_MARKER "${FFMPEG_ROOT}/lib/libavcodec.so")
endif()

if(NOT EXISTS "${TRIDENT_FFMPEG_MARKER}")
  if(NOT TRIDENT_FFMPEG_URL)
    message(FATAL_ERROR
      "FFmpeg not found in ${FFMPEG_ROOT}, and TRIDENT_FFMPEG_URL is empty.\n"
      "Set TRIDENT_FFMPEG_URL to a valid FFmpeg dev build zip.")
  endif()

  message(STATUS "FFmpeg not found in ${FFMPEG_ROOT}, downloading from: ${TRIDENT_FFMPEG_URL}")

  file(MAKE_DIRECTORY "${FFMPEG_ROOT}")
  set(TRIDENT_FFMPEG_ARCHIVE "${CMAKE_BINARY_DIR}/ffmpeg-download.zip")

  file(DOWNLOAD
    "${TRIDENT_FFMPEG_URL}"
    "${TRIDENT_FFMPEG_ARCHIVE}"
    SHOW_PROGRESS
  )

  # Extract the archive into vendor/FFmpeg.
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xvf "${TRIDENT_FFMPEG_ARCHIVE}"
    WORKING_DIRECTORY "${FFMPEG_ROOT}"
  )

  file(REMOVE "${TRIDENT_FFMPEG_ARCHIVE}")
endif()

# FFmpeg layout config
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_BIN_DIR "${FFMPEG_ROOT}/bin")

if(WIN32)
  set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")
  set(FFMPEG_AVCODEC_LIBRARY "${FFMPEG_LIB_DIR}/avcodec.lib")
  set(FFMPEG_AVFORMAT_LIBRARY "${FFMPEG_LIB_DIR}/avformat.lib")
  set(FFMPEG_AVUTIL_LIBRARY "${FFMPEG_LIB_DIR}/avutil.lib")
  set(FFMPEG_AVDEVICE_LIBRARY "${FFMPEG_LIB_DIR}/avdevice.lib")
  set(FFMPEG_SWRESAMPLE_LIBRARY "${FFMPEG_LIB_DIR}/swresample.lib")
  set(FFMPEG_SWSCALE_LIBRARY "${FFMPEG_LIB_DIR}/swscale.lib")
else()
  set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")
  set(FFMPEG_AVCODEC_LIBRARY "${FFMPEG_LIB_DIR}/libavcodec.so")
  set(FFMPEG_AVFORMAT_LIBRARY "${FFMPEG_LIB_DIR}/libavformat.so")
  set(FFMPEG_AVUTIL_LIBRARY "${FFMPEG_LIB_DIR}/libavutil.so")
  set(FFMPEG_AVDEVICE_LIBRARY "${FFMPEG_LIB_DIR}/libavdevice.so")
  set(FFMPEG_SWRESAMPLE_LIBRARY "${FFMPEG_LIB_DIR}/libswresample.so")
  set(FFMPEG_SWSCALE_LIBRARY "${FFMPEG_LIB_DIR}/libswscale.so")
endif()

# Fail early if the layout is wrong
if(NOT EXISTS "${FFMPEG_INCLUDE_DIR}/libavcodec/avcodec.h")
  message(FATAL_ERROR "FFmpeg headers not found in ${FFMPEG_INCLUDE_DIR}")
endif()

foreach(lib IN ITEMS
    FFMPEG_AVCODEC_LIBRARY
    FFMPEG_AVFORMAT_LIBRARY
    FFMPEG_AVUTIL_LIBRARY
    FFMPEG_AVDEVICE_LIBRARY
    FFMPEG_SWRESAMPLE_LIBRARY
    FFMPEG_SWSCALE_LIBRARY)
  if(NOT EXISTS "${${lib}}")
    message(FATAL_ERROR "FFmpeg library '${lib}' not found at '${${lib}}'")
  endif()
endforeach()

# Collect FFmpeg DLLs (for copying to runtime output)
if(EXISTS "${FFMPEG_BIN_DIR}")
  file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")
else()
  set(FFMPEG_DLLS "")
endif()

# Vulkan package + FetchContent
find_package(Vulkan REQUIRED)
include(FetchContent)

# Vendor libraries
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

add_library(libktx INTERFACE IMPORTED)
set_target_properties(libktx PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libktx/include"
)
if(WIN32)
  set_target_properties(libktx PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libktx/lib/ktx.lib"
  )
else()
  set_target_properties(libktx PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libktx/lib/libktx.so"
  )
endif()

# Assimp
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT ON  CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  assimp
  URL https://github.com/assimp/assimp/archive/refs/tags/v5.3.1.zip
)
FetchContent_MakeAvailable(assimp)

# ImGuizmo
set(IMGUIZMO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imguizmo")
add_library(imguizmo STATIC ${IMGUIZMO_ROOT}/ImGuizmo.cpp)
target_include_directories(imguizmo PUBLIC
  $<BUILD_INTERFACE:${IMGUIZMO_ROOT}>
)
target_link_libraries(imguizmo PUBLIC imgui)

# Trident source
file(GLOB_RECURSE TRIDENT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

add_library(${PROJECT_NAME} STATIC ${TRIDENT_SOURCES})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/nanosvg/src>
  $<BUILD_INTERFACE:${FFMPEG_INCLUDE_DIR}>
)

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
  onnxruntime::headers
  ${FFMPEG_AVFORMAT_LIBRARY}
  ${FFMPEG_AVCODEC_LIBRARY}
  ${FFMPEG_AVUTIL_LIBRARY}
  ${FFMPEG_AVDEVICE_LIBRARY}
  ${FFMPEG_SWRESAMPLE_LIBRARY}
  ${FFMPEG_SWSCALE_LIBRARY}
)

# Surface the expected ONNX Runtime version to the code so we can verify
# the loaded DLL matches the vendored SDK during startup.
target_compile_definitions(${PROJECT_NAME} PUBLIC
  TRIDENT_ORT_VERSION="${ORT_VERSION}"
)

# Options
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

# ONNX model validator tool
add_executable(trident_onnx_validator tools/ValidateOnnxAsset.cpp)
target_link_libraries(trident_onnx_validator PRIVATE ${PROJECT_NAME})
target_include_directories(trident_onnx_validator PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_definitions(trident_onnx_validator PRIVATE
  TRIDENT_DEFAULT_MODEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../Assets/AI/frame_generator.onnx"
  TRIDENT_MAX_ONNX_IR=11
)

# Copy FFmpeg DLLs next to the validator executable so it can actually run
if(FFMPEG_DLLS)
  foreach(dll IN LISTS FFMPEG_DLLS)
    add_custom_command(TARGET trident_onnx_validator POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${dll}"
        $<TARGET_FILE_DIR:trident_onnx_validator>
    )
  endforeach()
endif()

add_test(NAME TridentValidateOnnxRuntimeCompatibility
  COMMAND trident_onnx_validator
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
)