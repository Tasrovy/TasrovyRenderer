# TasrovyRenderer

A C++20 Vulkan renderer.

## Build

```bash
cmake -B cmake-build-debug -DCMAKE_TOOLCHAIN_FILE=<vcpkg-path>/scripts/buildsystems/vcpkg.cmake
cmake --build cmake-build-debug --config Debug
```

## Run

```bash
cmake-build-debug\src\core\TasrovyCore.exe
```
