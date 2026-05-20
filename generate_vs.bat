@echo off
SETLOCAL

:: 1. 设置 vcpkg 路径
SET VCPKG_ROOT=C:\Libraries\vcpkg
SET TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

:: 2. 设置生成目录
SET BUILD_DIR=build_vs

:: 3. 清理旧的编译目录
if exist %BUILD_DIR% (
    echo Cleaning old build directory...
    rd /s /q %BUILD_DIR%
)

:: 4. 创建并进入目录
mkdir %BUILD_DIR%
cd %BUILD_DIR%

:: 5. 显式指定 Visual Studio 2026 生成器
echo Generating Visual Studio 2026 solution...
cmake .. -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%"

:: 6. 【新增】检查 CMake 执行状态 (错误处理)
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================================
    echo [ERROR] CMake generation FAILED! 
    echo Please check the error messages above.
    echo ========================================================
    echo.
    pause
    exit /b %ERRORLEVEL%
)

:: 7. 【新增】完成并自动打开项目文件
echo.
echo ========================================================
echo Generation complete! 
echo Automatically opening the project file...
echo ========================================================

:: 自动检测并打开 .slnx 或 .sln
if exist "TasrovyRenderer.slnx" (
    echo Opening TasrovyRenderer.slnx...
    start "" "TasrovyRenderer.slnx"
) else if exist "TasrovyRenderer.sln" (
    echo Opening TasrovyRenderer.sln...
    start "" "TasrovyRenderer.sln"
) else (
    echo [WARNING] Could not find any solution file (.slnx or .sln) in %BUILD_DIR%
)

pause