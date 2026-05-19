@echo off
SETLOCAL

:: 1. 设置 vcpkg 路径 (请确保这个路径是正确的)
SET VCPKG_ROOT=C:\Libraries\vcpkg
SET TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

:: 2. 设置生成目录
SET BUILD_DIR=build_vs

:: 3. 如果已经存在 build 目录，先清理它（可选，为了防止缓存污染）
if exist %BUILD_DIR% (
    echo Cleaning old build directory...
    rd /s /q %BUILD_DIR%
)

:: 4. 创建新的 build 目录
mkdir %BUILD_DIR%
cd %BUILD_DIR%

:: 5. 调用 CMake 生成 Visual Studio 2022 解决方案
:: -G 指定生成器
:: -A 指定架构 (x64)
:: -DCMAKE_TOOLCHAIN_FILE 关联 vcpkg
echo Generating Visual Studio solution...
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%"

:: 6. 完成
echo.
echo ========================================================
echo Generation complete! 
echo Solution file is in: %BUILD_DIR%\TasrovyRenderer.sln
echo ========================================================
pause