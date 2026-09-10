# PB FrameFlux - LGPL-2.1
# Root CMake Configuration

cmake_minimum_required(VERSION 3.22)
project(PB_FrameFlux LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# -----------------------------------------------------------------------------
# Optimization & Portability Options
# -----------------------------------------------------------------------------
option(FRAMEFLUX_ENABLE_LTO "Enable Link-Time Optimization (LTO)" OFF) # OFF by default for fast dev
option(FRAMEFLUX_STATIC_STDLIB "Statically link libstdc++ and libgcc" OFF)
set(FRAMEFLUX_ARCH "native" CACHE STRING "Target CPU architecture: native, x86-64-v2, x86-64-v3, x86-64-v4")

# Basic compiler warnings
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# Architecture optimization flag
if(NOT FRAMEFLUX_ARCH STREQUAL "none")
    add_compile_options(-march=${FRAMEFLUX_ARCH})
    message(STATUS "[FrameFlux] Targeting CPU architecture: -march=${FRAMEFLUX_ARCH}")
endif()

# Release Mode Extreme Optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -fno-plt)
    add_link_options(-Wl,-O1 -Wl,--as-needed -Wl,-z,relro -Wl,-z,now)

    # Static C++ Runtime for universal glibcxx portability
    if(FRAMEFLUX_STATIC_STDLIB)
        add_link_options(-static-libgcc -static-libstdc++)
        message(STATUS "[FrameFlux] Statically linking libgcc & libstdc++")
    endif()

    # Link-Time Optimization (LTO)
    if(FRAMEFLUX_ENABLE_LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
        if(ipo_supported)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
            message(STATUS "[FrameFlux] Link-Time Optimization (LTO) ENABLED")
        else()
            message(WARNING "[FrameFlux] LTO not supported: ${ipo_error}")
        endif()
    endif()
endif()

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
find_package(Vulkan REQUIRED)
find_program(SLANGC_EXECUTABLE slangc REQUIRED)
message(STATUS "Found Slang compiler: ${SLANGC_EXECUTABLE}")

# -----------------------------------------------------------------------------
# Shader Compilation (Slang -> SPIR-V -> Embedded Header)
# -----------------------------------------------------------------------------
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(GENERATED_SHADER_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_shaders")
file(MAKE_DIRECTORY ${GENERATED_SHADER_HEADER_DIR})

set(SLANG_SHADERS
    "${SHADER_DIR}/warp_interpolate.slang"
    "${SHADER_DIR}/flow_search_dp4a.slang"
    "${SHADER_DIR}/luma_pack.slang"
    "${SHADER_DIR}/flow_downsample.slang"
    "${SHADER_DIR}/flow_refine_subgroup.slang"
    "${SHADER_DIR}/fallback_blend.slang"
    "${SHADER_DIR}/overlay_hud.slang"
)

set(GENERATED_SPV_HEADERS "")

foreach(SHADER_PATH ${SLANG_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_PATH} NAME_WE)
    set(SPV_FILE "${GENERATED_SHADER_HEADER_DIR}/${SHADER_NAME}.spv")
    set(HEADER_FILE "${GENERATED_SHADER_HEADER_DIR}/${SHADER_NAME}_spv.hpp")

    add_custom_command(
        OUTPUT ${HEADER_FILE}
        COMMAND ${SLANGC_EXECUTABLE} ${SHADER_PATH}
                -target spirv
                -profile spirv_1_5
                -entry computeMain
                -stage compute
                -o ${SPV_FILE}
        COMMAND python3 -c "b = open('${SPV_FILE}', 'rb').read(); open('${HEADER_FILE}', 'w').write('#pragma once\\n#include <cstdint>\\n#include <cstddef>\\n\\nalignas(4) inline const uint8_t ${SHADER_NAME}_spv[] = {\\n' + ', '.join(map(str, b)) + '\\n};\\n\\ninline const size_t ${SHADER_NAME}_spv_size = sizeof(${SHADER_NAME}_spv);\\n')"
        DEPENDS ${SHADER_PATH}
        COMMENT "Compiling Slang shader ${SHADER_NAME}.slang"
        VERBATIM
    )

    list(APPEND GENERATED_SPV_HEADERS ${HEADER_FILE})
endforeach()

add_custom_target(FrameFluxShaders DEPENDS ${GENERATED_SPV_HEADERS})

# -----------------------------------------------------------------------------
# Build Layer Target
# -----------------------------------------------------------------------------

set(LAYER_SOURCES
    layer/layer_entrypoints.cpp
    layer/swapchain_interceptor.cpp
    layer/compute_engine.cpp
    config/settings.cpp
    runtime/scheduler/frametime_smoother.cpp
    runtime/scheduler/display_timing.cpp
    runtime/motion/compute_optical_flow.cpp
    runtime/motion/nv_optical_flow.cpp
    runtime/motion/game_vectors.cpp
    runtime/interpolator/confidence_map.cpp
    runtime/interpolator/frame_blender.cpp
)

set(LAYER_HEADERS
    layer/swapchain_interceptor.hpp
    layer/compute_engine.hpp
)

add_library(VkLayer_projectbig_frameflux SHARED
    ${LAYER_SOURCES}
    ${LAYER_HEADERS}
    ${GENERATED_SPV_HEADERS}
)

add_dependencies(VkLayer_projectbig_frameflux FrameFluxShaders)

target_include_directories(VkLayer_projectbig_frameflux PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/layer
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/motion
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/scheduler
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/interpolator
    ${GENERATED_SHADER_HEADER_DIR}
)

target_link_libraries(VkLayer_projectbig_frameflux PRIVATE
    Vulkan::Vulkan
)

set_target_properties(VkLayer_projectbig_frameflux PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    OUTPUT_NAME "VkLayer_projectbig_frameflux"
)

# -----------------------------------------------------------------------------
# Manifest Configuration
# -----------------------------------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/layer/VkLayer_projectbig_frameflux.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/VkLayer_projectbig_frameflux.json"
    @ONLY
)