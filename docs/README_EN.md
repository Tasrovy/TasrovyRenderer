# TasrovyRenderer

[中文](../README.md)

TasrovyRenderer is an experimental real-time renderer built with C++20, HLSL, and Vulkan. It focuses on modern rendering pipelines, graphics API abstraction, and data-driven frame execution.

The deferred renderer is the default pipeline, with a smaller forward PBR path retained as an alternative.

## Features

- RenderGraph dependency analysis and automatic synchronization
- API-independent FramePacket and RHI execution plans
- Vulkan dynamic rendering and automatic pipeline barriers
- Deferred PBR, IBL, and transparent rendering
- Shadow maps, CSM, PCF / PCSS, and an experimental Virtual Shadow Map
- HBAO, Hi-Z, SSR, TAA / TAAU, Bloom, Motion Blur, and DOF
- GPUScene-based scene data organization
- Independent UV sampling transforms per texture
- Separate main, render, and RHI thread responsibilities
- ImGui debugging, resource monitoring, and GPU timestamps

## Architecture

```text
Scene / Pipeline
       |
       v
RenderGraph -> FramePacket -> RHI Execution Plan -> Vulkan Executor
```

Main modules:

- `src/render`: scenes, materials, pipelines, RenderGraph, and frame descriptions
- `src/renderer`: scene snapshots, frame orchestration, GPUScene, and threading
- `src/RHI`: resource, command, and execution-plan abstractions
- `src/RHI/Vulkan`: Vulkan backend
- `src/assets`, `src/filesystem`: model and texture loading
- `src/ui`: runtime debugging interface

See the [architecture overview](PROJECT_OVERVIEW_CN.md) for implementation details.

## Requirements

- Windows
- CMake 3.20+
- A C++20-compatible toolchain
- Vulkan SDK
- DirectX Shader Compiler (DXC)
- vcpkg

Dependencies are declared in `vcpkg.json`.

## Building

Set `VCPKG_ROOT`, then run:

```powershell
cmake -B cmake-build-debug `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build cmake-build-debug --config Debug
```

Compile shaders:

```powershell
.\res\Scripts\compile.bat
```

Run:

```powershell
.\cmake-build-debug\src\core\TasrovyCore.exe
```

## Project Status

The project is under active development:

- Virtual Shadow Maps currently use a simplified page-mapping implementation
- IBL precomputation is disabled by default, while its backend remains available
- Experimental GPU-driven GBuffer code is not connected to the active runtime path
- Thread scheduling currently favors ownership and synchronization correctness

## Documentation

- [Architecture and rendering pipeline](PROJECT_OVERVIEW_CN.md)
- [GPUScene and uniform architecture](GPU_SCENE_UNIFORM_ARCHITECTURE_CN.md)

## Asset Notice

The demonstration character belongs to 永雏塔菲, with modeling credited to Francesca. Model and art assets are included only for rendering study and demonstration. Their rights remain with the original authors and owners, and reuse or redistribution must follow the original licenses.
