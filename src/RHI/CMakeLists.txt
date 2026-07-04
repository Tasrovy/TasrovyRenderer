cmake_minimum_required(VERSION 3.20)
project(TasrovyRHI)

add_subdirectory(Vulkan)

find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)
find_package(spirv_cross_cpp CONFIG REQUIRED)

add_library(TasrovyRHI STATIC
        RHIConfig.h
        ReflectionData.h
        SpirvReflection.cpp
        SpirvReflection.h
        ReflectionBridge.cpp
        ReflectionBridge.h
        CommandList.cpp
        CommandList.h
        Device.cpp
        Device.h
)

target_include_directories(TasrovyRHI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(TasrovyRHI PUBLIC
            TasrovyRHI_Vulkan
            spirv-cross-cpp
            spirv-cross-glsl
            spirv-cross-core
            TasrovyLog
)
