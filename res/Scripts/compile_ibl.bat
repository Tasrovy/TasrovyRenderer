@echo off
setlocal

set "DXC=C:\Libraries\Vulkan\Bin\dxc.exe"
set "SOURCE=%~dp0..\Shaders\IBL\Source"
set "OUTPUT=%~dp0..\Shaders\IBL\Bin"

if not exist "%OUTPUT%" mkdir "%OUTPUT%"
call :compile irradiance.hlsl irradiance.spv || exit /b 1
call :compile brdf.hlsl brdf.spv || exit /b 1
call :compile prefilter_specular.hlsl prefilter_specular.spv || exit /b 1
echo IBL shader compilation completed.
exit /b 0

:compile
"%DXC%" -T cs_6_0 -E CSMain -spirv -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -Fo "%OUTPUT%\%2" "%SOURCE%\%1"
exit /b %errorlevel%
