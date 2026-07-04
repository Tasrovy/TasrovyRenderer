# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Generate Visual Studio 2026 solution (Windows only)
generate_vs.bat

# Manual build from command line
cd build_vs
cmake --build . --config Release
cmake --build . --config Debug

# Clean rebuild
rd /s /q build_vs
```

The project uses **vcpkg** for dependency management (`C:\Libraries\vcpkg`). Dependencies are defined in `vcpkg.json` at the project root.

## Project Architecture

**TasrovyRenderer** is a C++20 Vulkan renderer using dynamic rendering (`VK_KHR_dynamic_rendering`) with MSAA support, PBR shading, IBL environment maps, skyboxes, and an ImGui debug overlay.

### Module Layout

- **`main.cpp`** — Application entry point. Contains the main loop: poll events → ImGui new frame → update UBOs → write descriptors → begin render pass → bind pipelines (main model + skybox) → draw ImGui → end render pass → present. All scene state (transforms, lighting, material) lives here as local variables.

- **`src/render/`** — Static library `TasrovyRender`. Vulkan resource wrappers and rendering utilities.
  - `VulkanContext` — Instance, physical device, logical device, surface, debug messenger, MSAA selection
  - `VulkanQueue` — Queue family handle wrapper (Graphics/Present/Compute/Transfer)
  - `VulkanSwapChain` — Swapchain with MSAA color + depth attachments, recreation on resize
  - `VulkanBuffer` — Typed buffer with persistent mapping (`setData`)
  - `VulkanImage` — 2D textures (with mipmaps), cubemaps, attachments, cube maps. Static factory pattern.
  - `VulkanPipeline` + `PipelineBuilder` — Builder pattern for graphics/compute pipelines
  - `VulkanDescriptorSetLayout` + `VulkanDescriptorPool` — Builder pattern for descriptor layout/pool
  - `DescriptorWriter` — Fluent API: `writeBuffer(n, info).writeImage(n, info).update()`
  - `ImmediateSubmitter` — One-shot blocking GPU command submission (resource init, layout transitions)
  - `Renderer` — Frame sync primitives (semaphores, fences), command pool/buffer management
  - `Model` — Mesh data (`Vertex` with position/normal/tangent/bitangent/texCoord), `SkyboxVertex`
  - `IBLMap` — IBLProcessor: equirect-to-cube, irradiance convolution, prefiltered environment map, BRDF LUT
  - `Dependencies.h` — Aggregated include header; defines `UniformBufferObject` (HLSL-compatible transposed layout)

- **`src/filesystem/`** — Static library `TasrovyFileSystem`. Asset loading and data model (namespace `Tasrovy`).
  - `FileSystem` — Asset cache with `weak_ptr` tracking: holds weak references, callers hold `shared_ptr`. Model/Image cache auto-frees when all references drop.
  - `Model` — Extended vertex format (position, normal, tangent, vertex color, 4 UV sets) with submesh and bone data. Static generators: `GenCube()`, `GenSphere()`.
  - `Image` — stb_image-based loading from file or memory, move-only RAII semantics.
  - `Anim` — Per-bone keyframe animation data (position/rotation/scale channels).
  - `AssetLoader` — Assimp-based `LoadModel` (recursive mesh processing, tangent/normal generation, bone extraction), `LoadImage`, `LoadAnim`.

- **`src/logger/`** — Static library `TasrovyLogger`. Centralized logging wrapping spdlog.
  - `Logger` — Singleton wrapping spdlog with colored console output. Format: `[HH:MM:SS.fff] [LEVEL] message`.
  - Initialize via `Tasrovy::Logger::Init()` at app startup, then use `LOG_INFO(...)`, `LOG_ERROR(...)`, etc. (fmt-style formatting).
  - All modules link `TasrovyLogger`; all raw `std::cout`/`std::cerr` replaced with `LOG_*` macros.

### Key Conventions

- All Vulkan wrappers delete copy constructors (RAII, move-only semantics)
- Matrices are **transposed** before uploading to match HLSL column-major layout
- IBL system stores per-skybox irradiance + prefiltered maps in `std::unordered_map<std::string, IBTextures>`, with a shared BRDF LUT
- Skybox rendering disables depth write with `VK_COMPARE_OP_LESS_OR_EQUAL`
- Naming: render module uses `.h`/`.cpp`, filesystem module uses `.hpp`/`.cpp`
- Shaders are compiled externally to SPIR-V (loaded from `res/*.spv`)
