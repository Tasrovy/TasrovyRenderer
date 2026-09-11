# 111.rdc 风格化 PBR/NPR 管线移植

## 原则

- `StylizedPBRPipeline` 直接继承 `PipelineBase`，不修改现有
  `DeferredPipeline`。
- 初始版本复制当前 Deferred RenderGraph 作为兼容基线，保证任何时刻
  都存在完整输出。
- 捕获中的阶段按资源依赖逐个替换；一个阶段只有在 Shader、输入、输出
  和下游消费者均验证后才从兼容实现切换。
- Scene、Vegetation、Character、Special 通过 `MaterialTechnique` 在同一
  Mesh Pass 内选择不同 Shader Variant。

## 当前进度

| 捕获阶段 | 移植节点 | 状态 | 当前输出 |
|---|---|---|---|
| Colour Pass #1 / EID 251 | `Stylized.Atmosphere.TransmittanceLUT` | 已实现，待画面对照 | `Stylized.TransmittanceLUT` 256×64 R11G11B10F |
| Colour Pass #2 / EID 260 | `Stylized.Atmosphere.MultiScatteringLUT` | 已实现，待画面对照 | `Stylized.MultiScatteringLUT` 32×32 R11G11B10F |
| Colour Pass #3 / EID 269 | `Stylized.Atmosphere.SkyViewLUT` | 已实现，待画面对照 | `Stylized.SkyViewLUT` 128×128 R11G11B10F |
| Shadow Pass #1–#3 | 场景、角色、CSM 阴影 | 兼容 Deferred | 现有阴影资源 |
| Depth/GBuffer Pass #7–#12 | 深度与五附件 GBuffer | 兼容 Deferred | 现有 GBuffer |
| Screen-space Pass #13–#19 | AO、SSR、阴影遮罩 | 兼容 Deferred | 现有效果资源 |
| Lighting Pass #20–#25 | PBR/NPR 分类光照 | Technique 框架完成，Shader 待迁移 | 现有 Lighting/Transparent |
| Temporal/Post Pass #26–#28 | TAA、描边、Bloom 合成 | 兼容 Deferred | `PostProcessedHDRColor` RGBA16F |
| Colour Pass #29 / EID 5616 | HDR → LogC → 展平 3D LUT → 显示输出 | 已实现，待画面对照 | `ColorGradingLUT` → Swapchain |

## 下一阶段

在 RenderDoc Debug Output 中逐张对照三张大气 LUT；确认坐标方向、动态范围
和太阳角度后，把 `Stylized.SkyViewLUT` 接入独立天空合成节点，再开始替换阴影
与五附件 GBuffer 主干。

最终显示阶段使用 `res/Textures/ColorGrading/LUT.dds`。资源尺寸为 1024×32，
格式为 `R16G16B16A16_FLOAT`，直接上传 DDS 的 half-float 数据，避免 8-bit PNG
带来的量化损失。该纹理表示横向排列的 32 个 32×32 蓝色切片。Shader 从纹理
尺寸推导 LUT 阶数，先在
每个切片内对 RG 双线性采样，再在相邻 B 切片之间插值。输入保持 scene-linear
HDR，曝光后按 EID 5616 使用 ARRI LogC EI800 公式编码，再查询 LUT。LUT 返回
linear-display RGB，由 sRGB 交换链执行最终传递函数编码，避免 Shader 重复 Gamma。
LUT 作为持久导入资源上传一次；Debug Output 自动旁路整个显示变换。

最终显示节点现按 Pass 29 的顺序组织为：可选径向色散、十字邻域 CAS 风格锐化、
线性 HDR Bloom 合成、暗角、曝光与独立 LUT EV 补偿、LogC、展平 3D LUT，以及
sRGB 域抖动。当前引擎的 Bloom 资源本身是线性 HDR，因此不复制捕获中针对私有
Bloom 编码的分段解码曲线。交换链为 sRGB；Shader 将加入抖动后的 sRGB 值反解回
线性后输出，使附件编码后得到与 UNORM 目标上显式编码等价的结果。
