cmake_minimum_required(VERSION 3.20)
project(TasrovyRHI)

add_subdirectory(Vulkan)

add_library(TasrovyRHI STATIC
        RHIConfig.h
        CommandList.h
        CompiledRenderPipeline.h
        FrameScheduler.h
        FrameExecutionTypes.h
        FrameExecutor.h
        Device.h
        Pipeline.h
        Pass.h
        Buffer.h
        Image.h
        Descriptor.h
        ResourceTracker.h
        RenderFramePlan.cpp
        RenderFramePlan.h
        RenderOverlay.h
)

target_include_directories(TasrovyRHI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(TasrovyRHI PUBLIC
            TasrovyRHI_Vulkan
            TasrovyRender
            TasrovyBase
)
