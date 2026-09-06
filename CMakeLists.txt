# PB FrameFlux - LGPL-2.1
# Root CMake Configuration

cmake_minimum_required(VERSION 3.22)
project(PB_FrameFlux LANGUAGES CXX)

# Export compile_commands.json for VS Code IntelliSense / Clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Compiler optimization and warning flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -O3)
endif()

# -----------------------------------------------------------------------------
# Find Dependencies
# -----------------------------------------------------------------------------

# Find Vulkan Headers & Loader
find_package(Vulkan REQUIRED)

# Find Slang Compiler (slangc)
find_program(SLANGC_EXECUTABLE slangc REQUIRED)
message(STATUS "Found Slang compiler: ${SLANGC_EXECUTABLE}")

# -----------------------------------------------------------------------------
# Slang to SPIR-V Compilation & Header Embedding Pipeline
# -----------------------------------------------------------------------------

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(GENERATED_SHADER_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_shaders")
file(MAKE_DIRECTORY ${GENERATED_SHADER_HEADER_DIR})

set(SLANG_SHADERS
    "${SHADER_DIR}/warp_interpolate.slang"
    "${SHADER_DIR}/flow_search_dp4a.slang"
    "${SHADER_DIR}/luma_pack.slang"
)

set(GENERATED_SPV_HEADERS "")

foreach(SHADER_PATH ${SLANG_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_PATH} NAME_WE)
    set(SPV_FILE "${GENERATED_SHADER_HEADER_DIR}/${SHADER_NAME}.spv")
    set(HEADER_FILE "${GENERATED_SHADER_HEADER_DIR}/${SHADER_NAME}_spv.hpp")

# Compile Slang -> SPIR-V -> Generate C++ Header via Python
    add_custom_command(
        OUTPUT ${HEADER_FILE}
        COMMAND ${SLANGC_EXECUTABLE} ${SHADER_PATH}
                -target spirv
                -profile spirv_1_5
                -entry computeMain
                -stage compute
                -o ${SPV_FILE}
        COMMAND python3 -c "b = open('${SPV_FILE}', 'rb').read(); open('${HEADER_FILE}', 'w').write('#pragma once\\n#include <cstdint>\\n#include <cstddef>\\n\\ninline const uint8_t ${SHADER_NAME}_spv[] = {\\n' + ', '.join(map(str, b)) + '\\n};\\n\\ninline const size_t ${SHADER_NAME}_spv_size = sizeof(${SHADER_NAME}_spv);\\n')"
        DEPENDS ${SHADER_PATH}
        COMMENT "Compiling Slang shader ${SHADER_NAME}.slang to embedded header"
        VERBATIM
    )

    list(APPEND GENERATED_SPV_HEADERS ${HEADER_FILE})
endforeach()
# Target for shaders
add_custom_target(FrameFluxShaders DEPENDS ${GENERATED_SPV_HEADERS})

# -----------------------------------------------------------------------------
# Build Vulkan Layer Library
# -----------------------------------------------------------------------------

set(LAYER_SOURCES
    layer/layer_entrypoints.cpp
    layer/swapchain_interceptor.cpp
    layer/compute_engine.cpp
)

set(LAYER_HEADERS
    layer/swapchain_interceptor.hpp
    layer/compute_engine.hpp
)

# Output target name: libVkLayer_projectbig_frameflux.so
add_library(VkLayer_projectbig_frameflux SHARED
    ${LAYER_SOURCES}
    ${LAYER_HEADERS}
    ${GENERATED_SPV_HEADERS}
)

add_dependencies(VkLayer_projectbig_frameflux FrameFluxShaders)

target_include_directories(VkLayer_projectbig_frameflux PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/layer
    ${GENERATED_SHADER_HEADER_DIR}
)

target_link_libraries(VkLayer_projectbig_frameflux PRIVATE
    Vulkan::Vulkan
)

# Hide non-exported symbols to strictly follow Khronos Layer conventions
set_target_properties(VkLayer_projectbig_frameflux PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    OUTPUT_NAME "VkLayer_projectbig_frameflux"
)

# -----------------------------------------------------------------------------
# Generate JSON Manifest
# -----------------------------------------------------------------------------

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/layer/VkLayer_projectbig_frameflux.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/VkLayer_projectbig_frameflux.json"
    @ONLY
)

message(STATUS "Configured VK_LAYER_PROJECTBIG_FrameFlux")