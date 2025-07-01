cmake_minimum_required(VERSION 3.20)
project(Trident LANGUAGES CXX)

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

add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
set_target_properties(Vulkan::Vulkan PROPERTIES
  IMPORTED_LOCATION "${VULKAN_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${VULKAN_INCLUDE_DIR}"
)

set(GLFW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw")
if(WIN32)
  add_library(glfw3 STATIC IMPORTED GLOBAL)
  set_target_properties(glfw3 PROPERTIES
    IMPORTED_LOCATION "${GLFW_ROOT}/lib-vc2022/glfw3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${GLFW_ROOT}/include"
  )
else()
  add_subdirectory(${GLFW_ROOT} glfw_build)
  add_library(glfw3 ALIAS glfw)
endif()

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

add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/tinygltf>
)

set(IMGUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui")
file(GLOB IMGUI_SOURCES
  ${IMGUI_ROOT}/*.cpp
  ${IMGUI_ROOT}/backends/imgui_impl_glfw.cpp
  ${IMGUI_ROOT}/backends/imgui_impl_vulkan.cpp
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
  $<BUILD_INTERFACE:${IMGUI_ROOT}>
  $<BUILD_INTERFACE:${IMGUI_ROOT}/backends>
)
target_link_libraries(imgui PUBLIC glfw3 Vulkan::Vulkan)

file(GLOB_RECURSE TRIDENT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

add_library(${PROJECT_NAME} STATIC ${TRIDENT_SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  Vulkan::Vulkan
  glfw3
  glm
  spdlog
  imgui
  stb
  tinygltf
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
message(STATUS "GLFW Root: ${GLFW_ROOT}")

set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime")

if(EXISTS "${ONNXRUNTIME_ROOT}/include" AND EXISTS "${ONNXRUNTIME_ROOT}/lib")
    message(STATUS "Using ONNX Runtime from submodule: ${ONNXRUNTIME_ROOT}")
else()
    include(FetchContent)
    FetchContent_Declare(
        onnxruntime_prebuilt
        URL https://github.com/microsoft/onnxruntime/releases/download/v1.17.0/onnxruntime-linux-x64-1.17.0.tgz
    )
    FetchContent_MakeAvailable(onnxruntime_prebuilt)

    file(GLOB ONNX_DIR LIST_DIRECTORIES true "${onnxruntime_prebuilt_SOURCE_DIR}/*")
    list(FILTER ONNX_DIR INCLUDE REGEX "onnxruntime")
    list(GET ONNX_DIR 0 ONNXRUNTIME_ROOT)
endif()