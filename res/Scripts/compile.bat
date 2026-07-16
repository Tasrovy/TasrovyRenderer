@echo off
setlocal

if not defined VULKAN_SDK (
    echo VULKAN_SDK is not set. Install the Vulkan SDK or define VULKAN_SDK before running this script.
    exit /b 1
)
set "DXC=%VULKAN_SDK%\Bin\dxc.exe"
set "SOURCE=%~dp0..\Shaders\Source"
set "OUTPUT=%~dp0..\Shaders\Bin"

if not exist "%DXC%" (
    echo DXC was not found at "%DXC%".
    exit /b 1
)

if not exist "%OUTPUT%" mkdir "%OUTPUT%"

call :compile vs_6_0 VSMain testShader.hlsl vert.spv || exit /b 1
call :compile ps_6_0 PSMain testShader.hlsl frag.spv || exit /b 1
call :compile vs_6_0 VSMain sky.hlsl skyvert.spv || exit /b 1
call :compile ps_6_0 PSMain sky.hlsl skyfrag.spv || exit /b 1
call :compile cs_6_0 CSMain compute.hlsl compute.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_shadow.hlsl deferred_shadow_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_shadow.hlsl deferred_shadow_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_gbuffer.hlsl deferred_gbuffer_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_gbuffer.hlsl deferred_gbuffer_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_lighting.hlsl deferred_lighting_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_lighting.hlsl deferred_lighting_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_transparent.hlsl deferred_transparent_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_transparent.hlsl deferred_transparent_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_postprocess.hlsl deferred_postprocess_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_postprocess.hlsl deferred_postprocess_frag.spv || exit /b 1

echo Shader compilation completed.
exit /b 0

:compile
"%DXC%" -T %1 -E %2 -spirv -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -Fo "%OUTPUT%\%4" "%SOURCE%\%3"
exit /b %errorlevel%
