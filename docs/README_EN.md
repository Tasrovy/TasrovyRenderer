# TasrovyRenderer

TasrovyRenderer is a C++20 Vulkan renderer built around an API-facing render layer, an RHI abstraction, asynchronous asset loading, PBR materials, skyboxes, precomputed IBL, and an ImGui scene inspector.

The root README is maintained in Chinese. This file provides the English version.

## Features

- C++20 renderer with a Vulkan backend
- RHI abstraction for buffers, images, passes, pipelines, descriptors, and command lists
- Scene-driven renderer that converts render-layer `Scene` and `Pipeline` data into RHI resources
- PBR forward shader with material textures
- Skybox rendering with runtime environment selection in ImGui
- Precomputed IBL textures per skybox:
  - irradiance cubemap
  - prefiltered specular cubemap
  - BRDF LUT
- Async model/image asset loading through the filesystem module using Taskflow
- ImGui scene inspector for cameras, lights, objects, materials, and skybox selection
- SPIR-V shader compilation through DXC

## Repository Layout

```text
src/base/        Math types and helpers
src/filesystem/  Asset loading, image/model data, Taskflow AssetManager
src/render/      API-agnostic scene, materials, meshes, pipelines, passes
src/RHI/         Render hardware interface and SceneRenderer
src/RHI/Vulkan/  Vulkan backend resources and renderer
src/ui/          ImGui overlay
src/window/      Windowing
src/log/         Logging
src/core/        Application entry point
res/             Models, textures, skyboxes, shaders, SPIR-V outputs
```

## Requirements

- Windows
- CMake 3.20+
- Vulkan SDK with DXC
- vcpkg
- A C++20 toolchain matching the project build setup

Dependencies are declared in `vcpkg.json`, including Vulkan-related libraries, Assimp, stb, spdlog, spirv-cross, and Taskflow.

## Build

Set the `VCPKG_ROOT` environment variable to your local vcpkg installation directory, then configure:

```powershell
cmake -B cmake-build-debug -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

Build:

```powershell
cmake --build cmake-build-debug --config Debug
```

Run:

```powershell
.\cmake-build-debug\src\core\TasrovyCore.exe
```

`<vcpkg-root>` is the local vcpkg installation directory.

## Shaders

Compile the main shaders:

```powershell
cd res
cmd /c compile.bat
```

Compile the IBL compute shaders:

```powershell
cd res\IBLComputeShader
cmd /c compilecomputes.bat
```

## Rendering Flow

The application builds a render-layer `Scene` and `PBRPipeline`, then submits them to `Tasrovy::RHI::SceneRenderer`.

`SceneRenderer` owns a render-thread scene snapshot. It parses the scene and pipeline into RHI resources, creates pass attachments, uploads meshes, binds material textures, updates per-frame uniform buffers, and records pass commands through `CommandList`.

Current pass setup includes shadow, g-buffer, skybox, forward, transparent, and post-processing oriented stages. The visible path currently focuses on skybox plus forward object rendering.

## Asset Loading

Large CPU-side assets such as models and images are loaded by `src/filesystem`.

`Tasrovy::FS::AssetManager` uses Taskflow workers to load assets asynchronously and exposes completion events. Render and core systems hold references to loaded asset data instead of doing blocking model/image parsing in the main render path.

## Skybox And IBL

Skybox candidates are discovered from `res` directories that contain:

```text
right.png
left.png
top.png
bottom.png
front.png
back.png
```

For each discovered skybox, the renderer creates a cubemap and precomputes the IBL resources. The ImGui scene inspector exposes an `Environment` combo box for realtime switching. Switching reuses cached cubemaps and IBL textures instead of recomputing them.

## Notes

- `SceneRenderer` keeps retired GPU resources alive across scene refreshes to avoid destroying buffers while submitted command buffers still reference them.
- RHI headers stay API-agnostic; Vulkan-specific data lives in the Vulkan backend.
- Generated local UI state such as `imgui.ini` should not be treated as source.
