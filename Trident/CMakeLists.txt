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

# Introduce a PhysX interface target so that we can centralise include paths
# and link libraries instead of scattering them through the codebase.
add_library(physx_sdk INTERFACE IMPORTED)
set_target_properties(physx_sdk PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/PhysX/include"
)
target_include_directories(physx_sdk INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/PhysX/include>
)
# Keep the directory layout in one place so the generator expressions stay readable.
set(PHYSX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/PhysX")
set(PHYSX_DEBUG_LIB_DIR "${PHYSX_ROOT}/Debug/lib")
set(PHYSX_CHECKED_LIB_DIR "${PHYSX_ROOT}/Checked/lib")
set(PHYSX_DEBUG_DLL_DIR "${PHYSX_ROOT}/Debug/dll")
set(PHYSX_CHECKED_DLL_DIR "${PHYSX_ROOT}/Checked/dll")

# Build the list of libraries we always need, switching between the Debug and Checked
# archives based on the active configuration. The Checked variants work well for
# Release/RelWithDebInfo builds because they include extra validation while still
# being optimised.
set(PHYSX_REQUIRED_LIBS
  PhysX_64.lib
  PhysXCommon_64.lib
  PhysXFoundation_64.lib
  PhysXExtensions_static_64.lib
  PhysXTask_static_64.lib
  PhysXPvdSDK_static_64.lib
  PhysXCooking_64.lib
)

set(PHYSX_LINK_LIBS "")
foreach(PHYSX_LIB IN LISTS PHYSX_REQUIRED_LIBS)
  list(APPEND PHYSX_LINK_LIBS
    "$<$<CONFIG:Debug>:${PHYSX_DEBUG_LIB_DIR}/${PHYSX_LIB}>"
    "$<$<NOT:$<CONFIG:Debug>>:${PHYSX_CHECKED_LIB_DIR}/${PHYSX_LIB}>"
  )
endforeach()

target_link_libraries(physx_sdk INTERFACE
  ${PHYSX_LINK_LIBS}
)

# TODO: Lift this PhysX wiring into a dedicated FindPhysX.cmake helper so that
# non-Windows toolchains can participate in the build without manual edits and so
# future configurations (e.g. Profile) can slot in with minimal changes.

# Provide KTX texture loading headers and import library to the build.
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

include(FetchContent)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
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
target_link_libraries(imguizmo PUBLIC imgui)

file(GLOB_RECURSE TRIDENT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)
add_library(${PROJECT_NAME} STATIC ${TRIDENT_SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

# Link against windowing and UI middleware fetched via FetchContent.
target_link_libraries(${PROJECT_NAME} PRIVATE
  glfw
  imgui
)

# Make the PhysX SDK a public dependency so downstream targets inherit headers and libraries.
target_link_libraries(${PROJECT_NAME} PUBLIC
  physx_sdk
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

message(STATUS "Vulkan SDK Path: ${VULKAN_SDK_PATH}")

set(ONNXRUNTIME_VERSION "1.17.0")
set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime")
if(EXISTS "${ONNXRUNTIME_ROOT}/include" AND EXISTS "${ONNXRUNTIME_ROOT}/lib")
    message(STATUS "Using ONNX Runtime from submodule: ${ONNXRUNTIME_ROOT}")
else()
    include(FetchContent)
    if(WIN32)
        set(ONNXRUNTIME_ARCHIVE "onnxruntime-win-x64-${ONNXRUNTIME_VERSION}.zip")
        FetchContent_Declare(
            onnxruntime_prebuilt
            URL https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/${ONNXRUNTIME_ARCHIVE}
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
    # Copy the PhysX runtime DLLs next to the built artefact, matching the libs above.
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<IF:$<CONFIG:Debug>,${PHYSX_DEBUG_DLL_DIR},${PHYSX_CHECKED_DLL_DIR}>"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
    target_link_libraries(${PROJECT_NAME} PUBLIC
        ${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib
    )
else()
    target_link_libraries(${PROJECT_NAME} PUBLIC
        ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so
    )
endif()