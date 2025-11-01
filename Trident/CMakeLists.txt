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

include(GNUInstallDirs)
include(FetchContent)

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

# Bump the ONNX Runtime dependency to pull the refreshed 1.23.2 binaries.
set(ONNXRUNTIME_VERSION "1.23.2")
set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime")
set(ONNXRUNTIME_VENDOR_VERSION_FILE "${ONNXRUNTIME_ROOT}/VERSION_NUMBER")
set(ONNXRUNTIME_VENDOR_VERSION "")

if(EXISTS "${ONNXRUNTIME_ROOT}/include" AND EXISTS "${ONNXRUNTIME_ROOT}/lib")
    if(EXISTS "${ONNXRUNTIME_VENDOR_VERSION_FILE}")
        # Read the vendor version file to ensure the bundled SDK matches the
        # configured dependency. This guards against stale archives lingering
        # between upgrades and documents the expectation for future bumps.
        file(READ "${ONNXRUNTIME_VENDOR_VERSION_FILE}" ONNXRUNTIME_VENDOR_VERSION)
        string(STRIP "${ONNXRUNTIME_VENDOR_VERSION}" ONNXRUNTIME_VENDOR_VERSION)
    endif()
endif()

if(EXISTS "${ONNXRUNTIME_ROOT}/include" AND EXISTS "${ONNXRUNTIME_ROOT}/lib" AND ONNXRUNTIME_VENDOR_VERSION STREQUAL ONNXRUNTIME_VERSION)
    message(STATUS "Using ONNX Runtime from submodule: ${ONNXRUNTIME_ROOT}")
else()
    include(FetchContent)
    if(WIN32)
        # The DirectML execution provider requires the DML-enabled archive so that the
        # renderer can activate the GPU path on Windows while retaining the CPU fall-back.
        # Windows consumes the GPU-enabled prebuilt package to keep feature parity with
        # Visual Studio builds that relied on DirectML acceleration. Future updates could
        # expose a toggle that lets non-GPU workflows fetch the lighter CPU archive.
        set(ONNXRUNTIME_ARCHIVE "onnxruntime-win-x64-gpu-${ONNXRUNTIME_VERSION}.zip")
        FetchContent_Declare(
            onnxruntime_prebuilt
            # Linux falls back to the general x64 package; a future enhancement could gate
            # GPU vs. CPU archives using explicit build options once we validate driver
            # requirements across distributions.
            URL https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz
        )
    else()
        FetchContent_Declare(
            onnxruntime_prebuilt
            URL https://github.com/microsoft/onnxruntime/releases/download/v1.17.0/onnxruntime-linux-x64-1.17.0.tgz
        )
    endif()
    FetchContent_MakeAvailable(onnxruntime_prebuilt)

    file(GLOB ONNX_DIR LIST_DIRECTORIES true "${onnxruntime_prebuilt_SOURCE_DIR}/*")
    list(FILTER ONNX_DIR INCLUDE REGEX "onnxruntime")
    list(GET ONNX_DIR 0 ONNXRUNTIME_ROOT)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${ONNXRUNTIME_ROOT}/include>
)

if(WIN32)
    # Confirm the DirectML provider landed in the expected location so the runtime can
    # load the GPU execution path, and discover the import library shipped by the DML
    # archive.  We retain the CPU provider automatically bundled with ONNX Runtime.
    set(ONNXRUNTIME_DML_PROVIDER "${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll")
    if(NOT EXISTS "${ONNXRUNTIME_DML_PROVIDER}")
        message(FATAL_ERROR "onnxruntime_providers_dml.dll missing from ${ONNXRUNTIME_ROOT}/lib; ensure the DirectML package is available.")
    endif()

    find_library(ONNXRUNTIME_IMPORT_LIBRARY
        NAMES onnxruntime onnxruntime.lib
        PATHS "${ONNXRUNTIME_ROOT}/lib"
        NO_DEFAULT_PATH
    )
    if(NOT ONNXRUNTIME_IMPORT_LIBRARY)
        message(FATAL_ERROR "Failed to locate the DirectML-enabled onnxruntime import library under ${ONNXRUNTIME_ROOT}/lib")
    endif()

    target_link_libraries(${PROJECT_NAME} PUBLIC
        ${ONNXRUNTIME_IMPORT_LIBRARY}
    )

    # Surface the provider DLL so executable targets (e.g., Trident-Forge) can copy it
    # beside their runtime binaries for loader discovery.
    set(TRIDENT_ONNXRUNTIME_DML_DLL "${ONNXRUNTIME_DML_PROVIDER}" CACHE FILEPATH "Path to the DirectML execution provider runtime" FORCE)

    install(FILES "${ONNXRUNTIME_DML_PROVIDER}"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
else()
    target_link_libraries(${PROJECT_NAME} PUBLIC
        ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so
    )
endif()