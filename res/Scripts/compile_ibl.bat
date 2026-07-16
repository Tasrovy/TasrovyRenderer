@echo off
setlocal

if not defined VULKAN_SDK (
    echo VULKAN_SDK is not set. Install the Vulkan SDK or define VULKAN_SDK before running this script.
    exit /b 1
)
set "DXC=%VULKAN_SDK%\Bin\dxc.exe"
set "SOURCE=%~dp0..\Shaders\IBL\Source"
set "OUTPUT=%~dp0..\Shaders\IBL\Bin"

if not exist "%DXC%" (
    echo DXC was not found at "%DXC%".
    exit /b 1
)

if not exist "%OUTPUT%" mkdir "%OUTPUT%"
call :compile irradiance.hlsl irradiance.spv || exit /b 1
call :compile brdf.hlsl brdf.spv || exit /b 1
call :compile prefilter_specular.hlsl prefilter_specular.spv || exit /b 1
echo IBL shader compilation completed.
exit /b 0

:compile
"%DXC%" -T cs_6_0 -E CSMain -spirv -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -Fo "%OUTPUT%\%2" "%SOURCE%\%1"
exit /b %errorlevel%
