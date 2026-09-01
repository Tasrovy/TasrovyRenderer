cmake_minimum_required(VERSION 3.20)
project(TasrovyRHI)

add_library(TasrovyRHI STATIC
        RHIConfig.h
        RHIBackendAccess.h
        RHITypes.h
        ResourceBackend.h
        DeviceBackend.h
        CommandListBackend.h
        Buffer.cpp
        Buffer.h
        CommandList.cpp
        CommandList.h
        CompiledRenderPipeline.cpp
        CompiledRenderPipeline.h
        Descriptor.cpp
        Descriptor.h
        Device.cpp
        Device.h
        FrameExecutor.cpp
        FrameScheduler.cpp
        FrameScheduler.h
        FrameExecutionTypes.h
        FrameExecutor.h
        FrameExecutorBackend.h
        FrameSchedulerBackend.h
        Image.cpp
        Image.h
        Pass.cpp
        Pass.h
        Pipeline.cpp
        Pipeline.h
        ResourceTracker.h
        RenderFramePlan.cpp
        RenderFramePlan.h
        RenderOverlay.h
)

target_include_directories(TasrovyRHI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(TasrovyRHI PUBLIC
            TasrovyRender
            TasrovyBase
)

if(TASROVY_RHI_BACKEND STREQUAL "Vulkan")
    add_subdirectory(Vulkan)
endif()
