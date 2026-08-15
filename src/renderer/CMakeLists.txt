add_library(TasrovyRendererRuntime STATIC
        FrameParameterBuilder.cpp
        FrameParameterBuilder.h
        FrameExecutionScheduler.cpp
        FrameExecutionScheduler.h
        FrameBindingResolver.cpp
        FrameBindingResolver.h
        FrameRuntimeParameterCompiler.cpp
        FrameRuntimeParameterCompiler.h
        FrameBufferUpload.h
        GPUScene.cpp
        GPUScene.h
        GpuDrivenGBufferSystem.h
        FrameOrchestrator.cpp
        FrameOrchestrator.h
        PrimitiveSceneProxy.cpp
        PrimitiveSceneProxy.h
        RendererSettings.h
        RendererFeaturePolicy.cpp
        RendererFeaturePolicy.h
        RendererRHIContext.cpp
        RendererRHIContext.h
        RenderScene.cpp
        RenderScene.h
        RenderThread.cpp
        RenderThread.h
        RHIThread.cpp
        RHIThread.h
        ResourceMonitor.cpp
        ResourceMonitor.h
        SceneGPUResources.cpp
        SceneGPUResources.h
        SceneAnimationSystem.cpp
        SceneAnimationSystem.h
        ShadowViewSystem.cpp
        ShadowViewSystem.h
        ViewState.h
        ViewSystem.cpp
        ViewSystem.h
        SceneRenderer.cpp
        SceneRenderer.h
        SceneRendererComponents.cpp
        SceneRendererComponents.h
        SceneRendererExecution.cpp
        SceneRendererExecution.h
        SceneRendererRuntime.cpp
        SceneRendererRuntime.h
        SkyboxGeometry.cpp
        SkyboxGeometry.h
)

target_include_directories(TasrovyRendererRuntime PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

# The renderer is the orchestration layer: it consumes API-independent render
# descriptions and records work through RHI abstractions. Platform API details
# remain in RHI/Vulkan.
target_link_libraries(TasrovyRendererRuntime PUBLIC
        TasrovyRender
        TasrovyAssets
        TasrovyRHI
        TasrovyUI
        TasrovyWindow
        TasrovyLog
)

if (WIN32)
    target_link_libraries(TasrovyRendererRuntime PRIVATE psapi)
endif()

if (BUILD_TESTING)
    add_executable(TasrovyRenderSceneSnapshotTest
            tests/RenderSceneSnapshotTest.cpp
    )
    target_link_libraries(TasrovyRenderSceneSnapshotTest PRIVATE
            TasrovyRendererRuntime
    )
    add_test(
            NAME TasrovyRenderSceneSnapshot
            COMMAND TasrovyRenderSceneSnapshotTest
    )
    add_executable(TasrovyRHIThreadTest
            tests/RHIThreadTest.cpp
    )
    target_link_libraries(TasrovyRHIThreadTest PRIVATE
            TasrovyRendererRuntime
    )
    add_test(
            NAME TasrovyRHIThread
            COMMAND TasrovyRHIThreadTest
    )
endif()
