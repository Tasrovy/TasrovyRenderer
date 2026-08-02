add_library(TasrovyRendererRuntime STATIC
        FrameParameterBuilder.cpp
        FrameParameterBuilder.h
        FrameExecutionScheduler.cpp
        FrameExecutionScheduler.h
        FrameBindingResolver.cpp
        FrameBindingResolver.h
        FrameRuntimeParameterCompiler.cpp
        FrameRuntimeParameterCompiler.h
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
