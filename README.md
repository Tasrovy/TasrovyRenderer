# TasrovyRenderer

A C++20 Vulkan renderer featuring PBR shading, IBL environment mapping, skyboxes, and an ImGui debug overlay.

## Getting Started

### Prerequisites

- Vulkan SDK
- CMake 3.20+
- vcpkg

### Building

```bash
vcpkg install
cmake -B build -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### Running

```bash
build\src\core\TasrovyCore.exe
```
