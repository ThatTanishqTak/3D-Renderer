cmake_minimum_required(VERSION 3.20)
project(Trident-Forge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS $ENV{VULKAN_SDK}/Bin
)
if(NOT GLSLANG_VALIDATOR)
  message(FATAL_ERROR "glslangValidator not found! Please install Vulkan SDK or add it to PATH.")
endif()

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders)
set(SHADER_BIN_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Assets/Shaders)
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

file(GLOB_RECURSE SHADER_SRC_FILES
     RELATIVE ${SHADER_SRC_DIR}
     "${SHADER_SRC_DIR}/*.vert"
     "${SHADER_SRC_DIR}/*.frag")

set(SPIRV_OUTPUTS)
foreach(SHADER_FILE IN LISTS SHADER_SRC_FILES)
  set(SRC "${SHADER_SRC_DIR}/${SHADER_FILE}")
  set(SPV "${SHADER_BIN_DIR}/${SHADER_FILE}.spv")

  add_custom_command(
      OUTPUT ${SPV}
      COMMAND ${GLSLANG_VALIDATOR} -V ${SRC} -o ${SPV}
      DEPENDS ${SRC}
      COMMENT "Compiling GLSL ==> SPIR-V: ${SHADER_FILE}"
      VERBATIM
  )
  
  list(APPEND SPIRV_OUTPUTS ${SPV})
endforeach()

add_custom_target(Shaders
  DEPENDS ${SPIRV_OUTPUTS}
  COMMENT "Building all shaders..."
)

file(GLOB_RECURSE FORGE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)
add_executable(${PROJECT_NAME} ${FORGE_SOURCES})

target_link_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>
    $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>
)

add_dependencies(${PROJECT_NAME} Shaders)

target_include_directories(${PROJECT_NAME}
    PRIVATE ${CMAKE_SOURCE_DIR}/Trident/src
)
target_link_libraries(${PROJECT_NAME}
    PRIVATE Trident
)