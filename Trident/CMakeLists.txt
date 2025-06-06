cmake_minimum_required(VERSION 3.20)
project(Trident LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
  IMPORTED_LOCATION               "${VULKAN_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES   "${VULKAN_INCLUDE_DIR}"
)

set(GLFW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw")
add_library(glfw3 STATIC IMPORTED GLOBAL)
set_target_properties(glfw3 PROPERTIES
  IMPORTED_LOCATION               "${GLFW_ROOT}/lib-vc2022/glfw3.lib"
  INTERFACE_INCLUDE_DIRECTORIES   "${GLFW_ROOT}/include"
)

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/glm>
)

add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/spdlog/include>
)

file(GLOB_RECURSE TRIDENT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

add_library(Trident STATIC ${TRIDENT_SOURCES})

target_include_directories(Trident PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(Trident PUBLIC
  Vulkan::Vulkan
  glfw3
  glm
  spdlog
)

option(TRIDENT_ENABLE_VALIDATION "Enable Vulkan Validation Layers" ON)
if(TRIDENT_ENABLE_VALIDATION)
  target_compile_definitions(Trident PUBLIC TRIDENT_VALIDATION_LAYERS=1)
endif()

message(STATUS "Vulkan SDK Path: ${VULKAN_SDK_PATH}")
message(STATUS "GLFW Root: ${GLFW_ROOT}")