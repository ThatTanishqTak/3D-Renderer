cmake_minimum_required(VERSION 3.20)
project(Trident-Forge LANGUAGES C CXX)

file(GLOB_RECURSE FORGE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)
add_executable(${PROJECT_NAME} ${FORGE_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Trident/src)
target_link_libraries(${PROJECT_NAME} PRIVATE Trident)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw imgui)

target_link_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>
    $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>
)

# -----------------------------------------------------------------
# Shader compilation
# -----------------------------------------------------------------
find_program(GLSLANG_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/Bin)
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders)
set(SHADER_BIN_DIR ${SHADER_SRC_DIR})
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

if(GLSLANG_VALIDATOR)
  file(GLOB_RECURSE SHADER_SRC_FILES CONFIGURE_DEPENDS
       RELATIVE ${SHADER_SRC_DIR}
       "${SHADER_SRC_DIR}/*.vert"
       "${SHADER_SRC_DIR}/*.frag")
  set(SPIRV_OUTPUTS)
  foreach(SHADER_FILE IN LISTS SHADER_SRC_FILES)
    set(SRC "${SHADER_SRC_DIR}/${SHADER_FILE}")
    set(SPV "${SHADER_BIN_DIR}/${SHADER_FILE}.spv")
    get_filename_component(_spv_dir "${SPV}" DIRECTORY)
    add_custom_command(
        OUTPUT ${SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_spv_dir}"
        COMMAND ${GLSLANG_VALIDATOR} -V "${SRC}" -o "${SPV}"
        DEPENDS "${SRC}"
        COMMENT "Compiling GLSL -> SPIR-V: ${SHADER_FILE}"
        VERBATIM
    )
    list(APPEND SPIRV_OUTPUTS ${SPV})
  endforeach()
  add_custom_target(Shaders DEPENDS ${SPIRV_OUTPUTS})
  add_dependencies(${PROJECT_NAME} Shaders)
else()
  message(WARNING "glslangValidator not found; relying on runtime shader compilation.")
endif()

# -----------------------------------------------------------------
# Copy assets
# -----------------------------------------------------------------
set(ASSETS_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Assets)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ASSETS_SRC_DIR}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Assets"
    COMMENT "Copying assets to runtime output..."
)

# -----------------------------------------------------------------
# Copy ONNX Runtime DLL next to the executable
# -----------------------------------------------------------------
set(ONNXRUNTIME_BUNDLED_DLL "${CMAKE_SOURCE_DIR}/Trident/vendor/onnxruntime/lib/onnxruntime.dll")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_BUNDLED_DLL}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/onnxruntime.dll"
    COMMENT "Copying bundled onnxruntime.dll -> output"
)