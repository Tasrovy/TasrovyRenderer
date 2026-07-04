  "C:\Libraries\Vulkan\Bin\dxc.exe" ^
  -T cs_6_0 ^
  -E CSMain ^
  -spirv ^
  -fspv-target-env=vulkan1.2 ^
  -fvk-use-dx-layout ^
  -Fo irradiance.spv ^
  irradiance.hlsl

    "C:\Libraries\Vulkan\Bin\dxc.exe" ^
  -T cs_6_0 ^
  -E CSMain ^
  -spirv ^
  -fspv-target-env=vulkan1.2 ^
  -fvk-use-dx-layout ^
  -Fo brdf.spv ^
  brdf.hlsl

  "C:\Libraries\Vulkan\Bin\dxc.exe" ^
  -T cs_6_0 ^
  -E CSMain ^
  -spirv ^
  -fspv-target-env=vulkan1.2 ^
  -fvk-use-dx-layout ^
  -Fo prefilter_specular.spv ^
  prefilter_specular.hlsl