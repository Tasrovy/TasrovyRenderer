"C:\Libraries\Vulkan\Bin\dxc.exe" ^
  -T vs_6_0 ^
  -E VSMain ^
  -spirv ^
  -fspv-target-env=vulkan1.2 ^
  -Fo vert.spv ^
  testShader.hlsl

"C:\Libraries\Vulkan\Bin\dxc.exe" ^
  -T ps_6_0 ^
  -E PSMain ^
  -spirv ^
  -fspv-target-env=vulkan1.2 ^
  -fvk-use-dx-layout ^
  -Fo frag.spv ^
  testShader.hlsl

  "C:\Libraries\Vulkan\Bin\dxc.exe" ^
  -T vs_6_0 ^
  -E VSMain ^
  -spirv ^
  -fspv-target-env=vulkan1.2 ^
  -fvk-use-dx-layout ^
  -Fo skyvert.spv ^
  sky.hlsl

  "C:\Libraries\Vulkan\Bin\dxc.exe" ^
  -T ps_6_0 ^
  -E PSMain ^
  -spirv ^
  -fspv-target-env=vulkan1.2 ^
  -fvk-use-dx-layout ^
  -Fo skyfrag.spv ^
  sky.hlsl

  "C:\Libraries\Vulkan\Bin\dxc.exe" ^
  -T cs_6_0 ^
  -E CSMain ^
  -spirv ^
  -fspv-target-env=vulkan1.2 ^
  -fvk-use-dx-layout ^
  -Fo compute.spv ^
  compute.hlsl

