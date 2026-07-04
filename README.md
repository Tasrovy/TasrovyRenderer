# TasrovyRenderer

A C++20 Vulkan renderer.

## Build

```bash
# 1. Install dependencies
vcpkg install

# 2. Configure
cmake -B cmake-build-debug -DCMAKE_TOOLCHAIN_FILE=C:/Libraries/vcpkg/scripts/buildsystems/vcpkg.cmake

# 3. Build
cmake --build cmake-build-debug --config Debug
```

## Run

```bash
cmake-build-debug\src\core\TasrovyCore.exe
```
