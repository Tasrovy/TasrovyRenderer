# TasrovyRenderer

A C++20 Vulkan renderer.

## Build

```bash
# 1. Clone vcpkg
git clone https://github.com/microsoft/vcpkg.git C:\Libraries\vcpkg
C:\Libraries\vcpkg\bootstrap-vcpkg.bat

# 2. Configure (auto-installs dependencies via vcpkg manifest)
cmake -B cmake-build-debug -DCMAKE_TOOLCHAIN_FILE=C:/Libraries/vcpkg/scripts/buildsystems/vcpkg.cmake

# 3. Build
cmake --build cmake-build-debug --config Debug
```

## Run

```bash
cmake-build-debug\src\core\TasrovyCore.exe
```
