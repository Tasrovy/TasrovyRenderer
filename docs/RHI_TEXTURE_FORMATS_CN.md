# RHI 纹理格式

纹理格式沿 `PipelineTextureFormat -> FrameTextureFormat ->
RenderTextureFormat -> RHI::Format -> VkFormat` 传递。Vulkan 后端在创建资源前检查
物理设备是否支持请求的采样、颜色附件、深度附件或 Storage Image 用途，避免到
Pipeline 创建或录制阶段才出现难以定位的错误。

## 当前格式

| 类型 | 格式 |
|---|---|
| 归一化颜色 | `R8Unorm`、`RG8Unorm`、`RGBA8Unorm`、`RGBA8Srgb` |
| 无符号整数 | `R8Uint`、`R16Uint`、`R32Uint`、`RG16Uint` |
| 浮点 | `R16Float`、`RG16Float`、`RGBA16Float`、`R32Float`、`RG32Float`、`RGBA32Float` |
| HDR/打包 | `R11G11B10Float`、`RGB10A2Unorm` |
| 深度/模板 | `Depth16Unorm`、`Depth24UnormStencil8`、`Depth32Float`、`Depth32FloatStencil8` |

交换链仍通过 `Swapchain` 逻辑格式声明，运行时解析为实际的 BGRA/RGBA
Presentation Format。

## 使用约束

- 整数颜色附件使用整数 Clear；归一化与浮点附件使用浮点 Clear。
- Depth/Stencil 格式只能作为深度附件或阴影资源，不能声明为 Storage Image。
- `R11G11B10Float` 适合无 Alpha 的 HDR 光照与大气 LUT；它占 4 bytes/texel，
  相比 `RGBA16Float` 的 8 bytes/texel 可减少一半带宽与显存。
- 上传数据必须与声明格式的 texel 布局一致；当前上传路径不进行 CPU 格式转换。
- BC/ASTC 压缩格式以及通用 Texture2DArray/Texture3D 资源描述尚未纳入本轮。
