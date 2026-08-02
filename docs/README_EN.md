# TasrovyRenderer

[中文](../README.md)

TasrovyRenderer is a real-time renderer built with C++20, HLSL, and Vulkan. The project now focuses on a data-driven rendering compiler and execution path rather than only collecting rendering effects: the Render layer declares scenes, resources, and passes; RenderGraph analyzes their dependencies; FramePacket captures an API-independent frame description; RHI derives resource and synchronization plans; and the Vulkan Executor creates resources, pipelines, descriptors, and GPU commands.

The deferred pipeline is the default. A smaller forward PBR pipeline remains available as a fallback path.

> The project is under active development. The current Virtual Shadow Map is a simplified implementation that validates virtual-to-physical atlas mapping; it is not Unreal Engine's complete VSM system. IBL precomputation is disabled by default, while its legacy Vulkan implementation is retained.

## Architecture

```text
Scene / Object / Camera / Light
              |
              v
Pipeline + dynamic PipelinePass declarations
              |
              v
RenderGraph validation + dependency analysis + topological scheduling
              |
              v
FrameCompiler -> FramePacket
              |
              v
RHIFrameCompiler -> RenderFrameExecutionPlan
              |
              v
FrameExecutor -> VulkanFrameExecutor
              |
              v
Vulkan resources / descriptors / barriers / command buffers
```

Module responsibilities:

- **Render**: API-independent scenes, meshes, materials, pipelines, passes, RenderGraph, and FramePacket.
- **RendererRuntime**: composes RenderScene, the render thread, ViewState, frame compilation, runtime parameters, and scene GPU resources.
- **RHI**: translates FramePacket into pipeline, descriptor, resource lifetime, and barrier execution plans.
- **Vulkan Backend**: resolves the execution plan into Vulkan objects and recorded GPU commands.
- **Assets / Filesystem**: loads models, textures, animations, and serialized scene assets.
- **UI**: provides ImGui rendering controls, debug outputs, and resource monitoring.

`Render` does not depend on RHI or Vulkan. RHI consumes Render output through the FramePacket data contract, while RendererRuntime connects both sides.

## RenderGraph and Dynamic Pipelines

`Pipeline::GenPass()` dynamically declares the resources and passes required by the current configuration. Feature selection happens before RenderGraph generation, so disabled shadow, SSR, Bloom, or temporal passes never enter the graph or RHI execution plan.

`addPass()` declares a pass; it does not define execution order. RenderGraph derives a stable topological order from:

- resource reads and writes with RAW / WAR / WAW hazards;
- explicit pass dependencies;
- selected producers for multi-writer resources;
- current-frame and previous-frame resource references.

Compilation rejects cycles, undeclared resources, ambiguous producers, descriptor binding collisions, and illegal same-pass shader feedback. Invalid graphs never proceed to GPU resource creation or execution.

The complete translation path is:

```text
RenderGraph
  -> FramePacket
  -> RHI Execution Plan
  -> Vulkan Executor
```

FramePacket contains ordered passes, texture and buffer descriptions, vertex layouts, descriptor layouts, pass parameters, pipeline permutations, Draw / Dispatch / Copy commands, and imported resource handles. After FramePacket generation, Renderer no longer reads the original `PipelinePass` objects.

## Extensibility Contracts

### Parameter Providers

A pass selects parameter generation through a string provider ID instead of a fixed enum:

```cpp
pass->setParameterProvider("my_pipeline.parameters");
```

RendererRuntime can register a custom provider:

```cpp
FrameRuntimeParameterCompiler::registerProvider(
    "my_pipeline.parameters",
    [](FrameParameterProviderContext& context) {
        context.uniform.postEffectParams = /* ... */;
    });
```

Providers can access the camera, ViewState, RendererSettings, and frame resolution, or replace the entire parameter byte payload. Missing providers are rejected explicitly.

### Imported Resource Handles

Resources originating outside the graph use generic string handles:

```cpp
pass->addImportedTexture({
    3,
    "my_pipeline.history",
    PipelineShaderStageFragment
});
```

`FrameBindingResolver` resolves handles into RHI resources at runtime. FrameCompiler and Vulkan Executor do not require fixed enum branches for every skybox, history texture, or generated resource.

### Pipeline Configuration Versions

RendererSettings are translated into a generic `PipelineConfiguration`. Each pipeline interprets the keys it supports and increments its configuration version when its structure changes. SceneRenderer observes the version and regenerates passes, RenderGraph, FramePacket, and the RHI plan without `dynamic_cast<DeferredPipeline>`.

## Rendering Features

### Deferred Rendering and PBR

- GBuffer-based deferred rendering
- Metallic / Roughness workflow
- Cook-Torrance BRDF
- directional, point, and spot light data
- base color, normal, metallic-roughness-AO, emissive, and stylized material parameters
- transparent forward composition

Primary GBuffer resources:

| Resource | Format | Purpose |
| --- | --- | --- |
| `GBufferAlbedo` | RGBA16F | Base color |
| `GBufferNormal` | RGBA16F | World-space normal |
| `GBufferMaterial` | RGBA8 | Metallic, Roughness, AO, Shading Model |
| `GBufferWorldPos` | RGBA16F | World position and extended parameters |
| `GBufferEffects` | RGBA16F | Rim and stylized parameters |
| `GBufferVelocity` | RG16F | Dedicated motion vectors |
| `SceneDepth` | D32F | Scene depth |

### Shadows

- single Shadow Map
- four-cascade Cascaded Shadow Maps
- simplified Virtual Shadow Map with fixed-resident virtual pages mapped into a physical depth atlas
- PCF / adaptive PCSS
- cascade splitting, stabilized projection, and boundary blending

The selected shadow technique is part of Pipeline configuration, so only the required passes enter RenderGraph.

### Screen-Space and Temporal Effects

- HBAO
- hierarchical Hi-Z
- SSR
- TAA and TAAU
- Motion Blur
- Depth of Field
- multi-level Bloom downsample/upsample pyramid
- outlines with optional temporal outline denoising
- exposure and tone mapping

TAA/TAAU use a dedicated `RG16F GBufferVelocity`, previous View/Projection/Model transforms, Halton jitter, history reprojection, depth rejection, and neighborhood clipping. Camera discontinuities invalidate temporal history as camera cuts.

## Current IBL Status

IBL precomputation is disabled by default:

- skyboxes still load, render, and switch normally;
- `Device::createIBLMaps()` is not called;
- `IBLProcessor` and its Irradiance, Prefiltered Specular, and BRDF LUT compute shaders do not execute;
- environment-lighting contribution remains disabled;
- safe placeholder textures keep descriptor bindings valid.

The legacy IBLProcessor, compute shaders, and Device entry point remain in the repository. The intended future design is for Device to generate derived environment maps automatically on demand instead of requiring SceneRenderer or SceneGPUResources to trigger generation manually.

## Scene, Threads, and Resource Lifetime

- `SceneRenderer` is the public rendering facade.
- `SceneRendererRuntime` is the composition root.
- `RenderScene` exposes thread-safe Scene/Pipeline snapshots.
- `PrimitiveSceneProxy` supports incremental primitive insertion, updates, and removal.
- the render thread coordinates frames and submits RHI work; the main thread owns window events.
- frames in flight use independent command buffers, fences, descriptors, and history resources.
- GPU resources use RAII, resource scopes, fences, and deferred deletion.
- swapchain and display-relative resources rebuild after window resizing.
- RHI owns transient texture lifetimes, allocation slots, and automatic barrier plans.

## Debugging

The ImGui tools provide:

- pipeline, shadow, and post-processing controls;
- camera, light, object, and material editing;
- GBuffer, depth, velocity, Hi-Z, shadow, and outline previews;
- debug texture semantic conversion for non-color textures;
- GPU pass timestamps;
- Vulkan resource count, estimated GPU memory, and deferred-deletion monitoring;
- skybox selection.

Development uses Vulkan Validation Layers and RenderDoc to inspect resources, synchronization, layouts, descriptors, and shaders.

## Default Scene and Asset Attribution

The application first attempts to load:

```text
res/Scenes/CornellTaffy.scene.json
```

The demonstration character belongs to 永雏塔菲, with modeling credited to Francesca. The related model and art assets remain the property of their original authors and owners. They are included only for rendering study and demonstration; reuse or redistribution must follow the original asset licenses.

## Repository Layout

```text
TasrovyRenderer/
|- src/
|  |- base/             Math types and foundational utilities
|  |- render/           API-independent scene, pipeline, RenderGraph, FramePacket
|  |- renderer/         Render thread, frame orchestration, proxies, parameters
|  |- RHI/              RHI resources, execution plans, generic executor
|  |  `- Vulkan/        Vulkan backend and Vulkan executor
|  |- assets/           File assets to Render-resource conversion
|  |- filesystem/       Model, image, and animation loading
|  |- ui/               ImGui
|  |- window/           Window and input
|  |- log/              Logging
|  `- core/             Application entry and scene serialization
|- res/
|  |- Materials/
|  |- Models/
|  |- Scenes/
|  |- Shaders/
|  |- Skyboxes/
|  `- Textures/
`- docs/
```

## Requirements

- Windows
- CMake 3.20+
- a C++20-capable MinGW or compatible toolchain
- Vulkan SDK
- DirectX Shader Compiler (DXC, used through the Vulkan SDK)
- vcpkg

Dependencies are managed by `vcpkg.json` and include Volk, GLFW, GLM, Assimp, Taskflow, spdlog, and nlohmann-json.

## Build

Set `VCPKG_ROOT` to your local vcpkg installation directory, then run:

```powershell
cmake -B cmake-build-debug `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build cmake-build-debug --config Debug
```

Run:

```powershell
.\cmake-build-debug\src\core\TasrovyCore.exe
```

## Shader Compilation

Main rendering shaders:

```powershell
.\res\Scripts\compile.bat
```

Retained IBL compute shaders:

```powershell
.\res\Scripts\compile_ibl.bat
```

IBL is dormant by default, so the current runtime path does not require regenerating IBL precomputation output.
