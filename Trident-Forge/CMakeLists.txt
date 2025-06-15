cmake_minimum_required(VERSION 3.20)
project(Trident-Forge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shader Compilation Setup
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS $ENV{VULKAN_SDK}/Bin
)
if(NOT GLSLANG_VALIDATOR)
  message(FATAL_ERROR "glslangValidator not found! Please install Vulkan SDK or add it to PATH.")
endif()

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders)
set(SHADER_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/Assets/Shaders)
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

# Fonts Copy Setup
set(FONT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Fonts)
set(FONT_BIN_DIR Assets/Fonts)
file(MAKE_DIRECTORY ${FONT_SRC_DIR})

# Source Files
file(GLOB_RECURSE FORGE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

add_executable(Trident-Forge ${FORGE_SOURCES})

# Shader dependency
add_dependencies(Trident-Forge Shaders)

# Font Copy Command (must be placed after add_executable)
add_custom_command(TARGET Trident-Forge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${FONT_SRC_DIR}"
    "$<TARGET_FILE_DIR:Trident-Forge>/${FONT_BIN_DIR}"
)

# Link Options
target_link_options(Trident-Forge PRIVATE
    $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>
    $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>
)

# Includes and Libraries
target_include_directories(Trident-Forge
    PRIVATE ${CMAKE_SOURCE_DIR}/Trident/src
)
target_link_libraries(Trident-Forge
    PRIVATE Trident
)