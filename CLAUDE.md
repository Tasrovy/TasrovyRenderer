# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```powershell
# CMake configure
cmake -B cmake-build-debug -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build cmake-build-debug --config Debug
cmake --build cmake-build-debug --config Release

# Clean rebuild
rd /s /q cmake-build-debug
```

The project uses **vcpkg** for dependency management. Its installation directory is referenced through `VCPKG_ROOT`, and dependencies are defined in `vcpkg.json` at the project root.

Shaders are compiled with **DXC** (DirectX Shader Compiler) to SPIR-V. See `res/compile.bat` for reference.

## Project Architecture

**TasrovyRenderer** is a C++20 Vulkan renderer using dynamic rendering (`VK_KHR_dynamic_rendering`) with MSAA, PBR, IBL, skyboxes, and ImGui overlay.

### Module Layout

- **`src/base/`** — Static library `TasrovyBase`. Math types wrapping GLM with project namespace.
  - `TSVector<L, T>` — Vector template inheriting `glm::vec`. Aliases: `TSVec2f`, `TSVec3f`, `TSVec4f`, etc. Free functions: `dot`, `cross`, `normalize`, `length`, `radians`, `pi`, etc. (pure hand-written, no GLM math calls)
  - `TSMatrix<C, R, T>` — Matrix template inheriting `glm::mat`. Aliases: `TSMat3f`, `TSMat4f`. Free functions: `transpose`, `inverse`, `translate`, `rotate`, `scale`, `perspective`, `lookAt`
  - `TSQuaternion<T>` — Quaternion template inheriting `glm::tquat`. Aliases: `TSQuatf`. Free functions: `slerp`, `quatFromEuler`, `mat4_cast`, `eulerAngles`, etc.

- **`src/render/`** — Static library `TasrovyRender`. API-agnostic render layer (namespace `Tasrovy`).
  - **Scene management:** `Scene`, `Object`, `Camera`, `Light` (DirectionalLight/PointLight/SpotLight), `Skybox`
  - **Resources:** `Mesh` (vertices + indices + vertex descriptions), `Material` (SPIR-V reflection-driven parameter maps), `Shader` (stores file paths, not compiled SPIR-V), `Texture` (wraps VulkanImage)
  - **Pipeline:** `Pipeline` (ordered passes), `PipelinePass` (shader + render state + objects)
  - **Geometry:** `Transform` (position + quaternion rotation + scale), `MeshVertex` (8 attributes: position/normal/tangent/vertexColor/uv0-3)
  - Smart pointer ownership: Scene entities use `unique_ptr`, resources use `shared_ptr`, resource references use `weak_ptr`
  - All classes use `static create()` factory methods, constructors are private/protected

- **`src/RHI/`** — Static library `TasrovyRHI`. Rendering Hardware Interface abstraction.
  - `RHIConfig.h` — Graphics API selection via `#define TASROVY_USE_VULKAN` (currently only Vulkan supported)
  - `Device` — Creates resources: buffers, images, pipelines, descriptors. Uses pimpl with `#ifdef` for backend
  - `CommandList` — Records GPU commands: bind pipeline/buffers, draw, barriers. Uses pimpl with `#ifdef`
  - `src/RHI/Vulkan/` — Vulkan backend implementations (VulkanContext, VulkanBuffer, VulkanImage, VulkanPipeline, VulkanSwapchain, Renderer, ImmediateSubmitter, DescriptorWriter, IBLMap, Model)

- **`src/filesystem/`** — Static library `TasrovyFileSystem`. Asset loading (namespace `Tasrovy`).
  - `Model` — Extended vertex format with submesh/bone data. Generators: `GenCube()`, `GenSphere()`
  - `Image` — stb_image-based loading
  - `Anim` — Per-bone keyframe animation data
  - `AssetLoader` — Assimp-based model/image/animation loading

- **`src/logger/`** — Static library `TasrovyLogger`. Centralized logging wrapping spdlog.
  - Initialize via `Tasrovy::Logger::Init()`, use `LOG_INFO(...)`, `LOG_ERROR(...)`, etc.

- **`src/core/`** — Application entry point (`main.cpp`). Links all libraries.

### Key Conventions

- All Vulkan wrappers delete copy constructors (RAII, move-only semantics)
- Matrices are **transposed** before uploading to match HLSL column-major layout
- All render-layer classes in `Tasrovy` namespace, use `enable_shared_from_this` for shared resources
- RHI layer uses `#ifdef TASROVY_API_VULKAN` for API-specific code in `.cpp` files only
- Header files are API-agnostic; types use forward declarations
- Naming: base/render use `.h`/`.cpp`, filesystem uses `.hpp`/`.cpp`
- TSVector/TSMatrix free functions are pure hand-written (no GLM math function calls)
