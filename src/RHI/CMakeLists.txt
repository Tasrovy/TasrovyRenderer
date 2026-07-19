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
        Pipeline.cpp
        Pipeline.h
        Pass.cpp
        Pass.h
        Buffer.cpp
        Buffer.h
        Image.cpp
        Image.h
        Descriptor.cpp
        Descriptor.h
        SceneGeometry.cpp
        SceneGeometry.h
        SceneRenderer.cpp
        SceneRenderer.h
        ResourceMonitor.cpp
        ResourceMonitor.h
        ResourceTracker.h
)

target_include_directories(TasrovyRHI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(TasrovyRHI PUBLIC
            TasrovyRHI_Vulkan
            TasrovyUI
            spirv-cross-cpp
            spirv-cross-glsl
            spirv-cross-core
            TasrovyLog
)

if (WIN32)
    target_link_libraries(TasrovyRHI PRIVATE psapi)
endif()
