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
call :compile vs_6_0 VSMain deferred_gbuffer_gpu.hlsl deferred_gbuffer_gpu_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_gbuffer_gpu.hlsl deferred_gbuffer_gpu_frag.spv || exit /b 1
call :compile cs_6_0 CSMain gbuffer_cull.hlsl gbuffer_cull_comp.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_hbao.hlsl deferred_hbao_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_hbao.hlsl deferred_hbao_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_bloom_lowres.hlsl deferred_bloom_lowres_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_bloom_lowres.hlsl deferred_bloom_lowres_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_bloom_downsample.hlsl deferred_bloom_downsample_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_bloom_downsample.hlsl deferred_bloom_downsample_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_bloom_upsample.hlsl deferred_bloom_upsample_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_bloom_upsample.hlsl deferred_bloom_upsample_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_lighting.hlsl deferred_lighting_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_lighting.hlsl deferred_lighting_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_hiz_init.hlsl deferred_hiz_init_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_hiz_init.hlsl deferred_hiz_init_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_hiz_reduce.hlsl deferred_hiz_reduce_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_hiz_reduce.hlsl deferred_hiz_reduce_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_ssr.hlsl deferred_ssr_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_ssr.hlsl deferred_ssr_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_dof.hlsl deferred_dof_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_dof.hlsl deferred_dof_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_transparent.hlsl deferred_transparent_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_transparent.hlsl deferred_transparent_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_taau.hlsl deferred_taau_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_taau.hlsl deferred_taau_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_motion_blur.hlsl deferred_motion_blur_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_motion_blur.hlsl deferred_motion_blur_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_outline_temporal.hlsl deferred_outline_temporal_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_outline_temporal.hlsl deferred_outline_temporal_frag.spv || exit /b 1
call :compile vs_6_0 VSMain deferred_postprocess.hlsl deferred_postprocess_vert.spv || exit /b 1
call :compile ps_6_0 PSMain deferred_postprocess.hlsl deferred_postprocess_frag.spv || exit /b 1
call :compile_post 0 0 0 deferred_postprocess_0_frag.spv || exit /b 1
call :compile_post 1 0 0 deferred_postprocess_1_frag.spv || exit /b 1
call :compile_post 0 1 0 deferred_postprocess_2_frag.spv || exit /b 1
call :compile_post 1 1 0 deferred_postprocess_3_frag.spv || exit /b 1
call :compile_post 0 0 1 deferred_postprocess_4_frag.spv || exit /b 1
call :compile_post 1 0 1 deferred_postprocess_5_frag.spv || exit /b 1
call :compile_post 0 1 1 deferred_postprocess_6_frag.spv || exit /b 1
call :compile_post 1 1 1 deferred_postprocess_7_frag.spv || exit /b 1

echo Shader compilation completed.
exit /b 0

:compile
"%DXC%" -T %1 -E %2 -spirv -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -Fo "%OUTPUT%\%4" "%SOURCE%\%3"
exit /b %errorlevel%

:compile_post
"%DXC%" -T ps_6_0 -E PSMain -spirv -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -DTASROVY_POST_SSR=%1 -DTASROVY_POST_BLOOM=%2 -DTASROVY_POST_OUTLINE=%3 -Fo "%OUTPUT%\%4" "%SOURCE%\deferred_postprocess.hlsl"
exit /b %errorlevel%
