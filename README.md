# TasrovyRenderer

[English](docs/README_EN.md)

TasrovyRenderer 是一个使用 C++20、HLSL 和 Vulkan 构建的实时渲染器。项目当前重点不只是实现渲染效果，而是建立一条数据驱动、可扩展的渲染编译与执行链：Render 层声明场景、资源和 Pass，RenderGraph 分析依赖，FramePacket 固化一帧的 API 无关描述，RHI 生成资源与同步计划，最终由 Vulkan Executor 创建资源、Pipeline、Descriptor 并录制命令。

默认使用延迟渲染管线；前向 PBR 管线作为较轻量的备用路径保留。

> 项目仍在持续开发中。当前 Virtual Shadow Map 是用于验证虚拟页到物理 Atlas 映射链路的简化实现，并非 Unreal Engine 完整 VSM。IBL 预计算当前默认停用，旧实现保留在 Vulkan Backend 中。

## 核心架构

```text
Scene / Object / Camera / Light
              |
              v
Pipeline + dynamic PipelinePass declarations
              |
              v
RenderGraph validation + dependency analysis + topological scheduling
              |
              v
FrameCompiler -> FramePacket
              |
              v
RHIFrameCompiler -> RenderFrameExecutionPlan
              |
              v
FrameExecutor -> VulkanFrameExecutor
              |
              v
Vulkan resources / descriptors / barriers / command buffers
```

模块职责：

- **Render**：API 无关的 Scene、Mesh、Material、Pipeline、PipelinePass、RenderGraph 和 FramePacket。
- **RendererRuntime**：组合 RenderScene、渲染线程、ViewState、帧编译、运行时参数和场景 GPU 资源。
- **RHI**：将 FramePacket 翻译为 Pipeline、Descriptor、资源生命周期和 Barrier 执行计划。
- **Vulkan Backend**：解析执行计划，创建 Vulkan 对象并录制、提交 GPU 命令。
- **Assets / Filesystem**：模型、纹理、动画和场景资源加载。
- **UI**：ImGui 渲染设置、调试输出和资源监控。

`Render` 不依赖 RHI 或 Vulkan。RHI 通过 `FramePacket` 数据协议消费 Render 的输出，RendererRuntime 负责连接两侧。

## RenderGraph 与动态管线

`Pipeline::GenPass()` 动态声明本帧可能使用的资源和 Pass。功能开关在 RenderGraph 生成前应用，因此关闭的阴影、SSR、Bloom、TAA 等 Pass 不会进入图或 RHI 执行计划。

`addPass()` 只表示声明，不表示执行顺序。RenderGraph 根据以下信息生成稳定的拓扑顺序：

- 资源读写关系与 RAW / WAR / WAW Hazard
- 显式 Pass 依赖
- 多写入者资源指定的生产者版本
- 当前帧和上一帧资源引用

编译阶段会拒绝循环依赖、未声明资源、歧义生产者、Descriptor Binding 冲突，以及同一 Pass 内非法采样并修改同一资源等错误。非法图不会继续创建或执行 GPU 工作。

完整翻译链如下：

```text
RenderGraph
  -> FramePacket
  -> RHI Execution Plan
  -> Vulkan Executor
```

`FramePacket` 包含排序后的 Pass、纹理和 Buffer 描述、Vertex Layout、Descriptor Layout、Pass Parameters、Pipeline Permutation、Draw / Dispatch / Copy 命令及导入资源句柄。生成 FramePacket 后，Renderer 不再读取原始 `PipelinePass`。

## 可扩展协议

### Parameter Provider

Pass 使用字符串 Provider ID 声明参数来源，而不是固定枚举：

```cpp
pass->setParameterProvider("my_pipeline.parameters");
```

RendererRuntime 可以注册自定义参数生成器：

```cpp
FrameRuntimeParameterCompiler::registerProvider(
    "my_pipeline.parameters",
    [](FrameParameterProviderContext& context) {
        context.uniform.postEffectParams = /* ... */;
    });
```

Provider 可访问相机、ViewState、RendererSettings 和分辨率，也可以完全覆盖 Pass 参数字节。未注册的 Provider 会被明确拒绝。

### Imported Resource Handle

场景外部纹理使用通用字符串句柄声明：

```cpp
pass->addImportedTexture({
    3,
    "my_pipeline.history",
    PipelineShaderStageFragment
});
```

`FrameBindingResolver` 在运行时把句柄解析为 RHI 资源；FrameCompiler 和 Vulkan Executor 不需要为每一种天空盒、历史纹理或程序纹理增加固定枚举分支。

### Pipeline 配置版本

RendererSettings 会转换成通用 `PipelineConfiguration`。Pipeline 解释自己关心的配置键，并在结构发生变化时递增配置版本。SceneRenderer 检测版本变化后重新生成 Pass、RenderGraph、FramePacket 和 RHI Plan，不再通过 `dynamic_cast<DeferredPipeline>` 操作特定管线。

## 实时渲染功能

### 延迟渲染与 PBR

- GBuffer 延迟渲染
- Metallic / Roughness 工作流
- Cook-Torrance BRDF
- 平行光、点光源和聚光灯数据
- 法线、基础颜色、金属粗糙度 AO、自发光和风格化材质参数
- 透明物体前向合成

主要 GBuffer 资源包括：

| 资源 | 格式 | 用途 |
| --- | --- | --- |
| `GBufferAlbedo` | RGBA16F | 基础颜色 |
| `GBufferNormal` | RGBA16F | 世界空间法线 |
| `GBufferMaterial` | RGBA8 | Metallic、Roughness、AO、Shading Model |
| `GBufferWorldPos` | RGBA16F | 世界坐标和扩展参数 |
| `GBufferEffects` | RGBA16F | Rim 等风格化参数 |
| `GBufferVelocity` | RG16F | 独立运动向量 |
| `SceneDepth` | D32F | 场景深度 |

### 阴影

- 单张 Shadow Map
- 四级 Cascaded Shadow Map
- 简化 Virtual Shadow Map：固定驻留虚拟页映射到物理深度 Atlas
- PCF / Adaptive PCSS
- 级联切分、稳定投影和边界混合

阴影技术通过 Pipeline 配置选择，只有对应 Pass 会进入 RenderGraph。

### 屏幕空间与时域效果

- HBAO
- 分层 Hi-Z
- SSR
- TAA 与 TAAU
- Motion Blur
- Depth of Field
- 多级 Bloom 下采样/上采样金字塔
- 描边与可选时域描边降噪
- Exposure 与 Tone Mapping

TAA/TAAU 使用独立 `RG16F GBufferVelocity`、前一帧 View/Projection/Model、Halton Jitter、历史重投影、深度拒绝和邻域裁剪。相机突变会被识别为 Camera Cut 并使历史失效。

## IBL 当前状态

IBL 预计算当前默认停用：

- 天空盒仍会加载、显示和切换。
- 不会调用 `Device::createIBLMaps()`。
- 不会创建 `IBLProcessor` 或执行 Irradiance、Prefiltered Specular、BRDF LUT Compute Shader。
- 环境光贡献默认关闭。
- Descriptor 使用安全占位纹理，避免未绑定资源。

旧 `IBLProcessor`、Compute Shader 和 Device 入口仍保留。未来计划由 Device 根据资源需求自动生成派生环境贴图，而不是由 SceneRenderer 或 SceneGPUResources 手动触发。

## 场景、线程与资源生命周期

- `SceneRenderer` 是公共门面。
- `SceneRendererRuntime` 是组件组合根。
- `RenderScene` 提供线程安全的 Scene/Pipeline 快照。
- `PrimitiveSceneProxy` 支持渲染对象的增量添加、更新和移除。
- Render Thread 负责帧协调和 RHI 提交，主线程负责窗口事件。
- 多帧并行使用独立的 Command Buffer、Fence、Descriptor 和历史资源。
- GPU 资源通过 RAII、Resource Scope、Fence 和延迟删除管理。
- 窗口尺寸变化会重建交换链及显示尺寸相关资源。
- RHI 负责瞬态纹理生命周期、复用槽和自动 Barrier 计划。

## 调试工具

ImGui 面板当前提供：

- 管线、阴影和后处理配置
- Camera、Light、Object 和 Material 参数编辑
- GBuffer、深度、运动向量、Hi-Z、阴影和描边结果预览
- Debug Texture Semantic 转换，便于观察非颜色纹理
- GPU Pass Timestamp
- Vulkan 资源数量、估算显存和延迟删除队列监控
- 天空盒选择

项目开发过程中使用 Vulkan Validation Layers 和 RenderDoc 进行资源、同步、布局、Descriptor 和 Shader 调试。

## 默认场景与资源声明

程序优先加载：

```text
res/Scenes/CornellTaffy.scene.json
```

演示角色模型归属永雏塔菲，建模作者为 Francesca。相关模型和美术资源的权利归原作者及所属方所有，本仓库中的资源仅用于渲染技术学习和效果展示。复制、分发或用于其他用途时，请遵守原资源的授权要求。

## 项目结构

```text
TasrovyRenderer/
|- src/
|  |- base/             数学类型与基础工具
|  |- render/           API 无关场景、管线、RenderGraph 与 FramePacket
|  |- renderer/         渲染线程、帧协调、场景代理与运行时参数
|  |- RHI/              RHI 资源、执行计划与统一执行入口
|  |  `- Vulkan/        Vulkan Backend 与 Vulkan Executor
|  |- assets/           文件资源到 Render 资源的转换
|  |- filesystem/       模型、图像和动画加载
|  |- ui/               ImGui
|  |- window/           窗口与输入
|  |- log/              日志系统
|  `- core/             程序入口与场景序列化
|- res/
|  |- Materials/
|  |- Models/
|  |- Scenes/
|  |- Shaders/
|  |- Skyboxes/
|  `- Textures/
`- docs/
```

## 环境要求

- Windows
- CMake 3.20+
- 支持 C++20 的 MinGW 或其他兼容工具链
- Vulkan SDK
- DirectX Shader Compiler（DXC，随 Vulkan SDK 使用）
- vcpkg

依赖由 `vcpkg.json` 管理，主要包括 Volk、GLFW、GLM、Assimp、Taskflow、spdlog 和 nlohmann-json。

## 构建

先将 `VCPKG_ROOT` 设置为本机的 vcpkg 安装目录，然后执行：

```powershell
cmake -B cmake-build-debug `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build cmake-build-debug --config Debug
```

运行：

```powershell
.\cmake-build-debug\src\core\TasrovyCore.exe
```

## 编译 Shader

主渲染 Shader：

```powershell
.\res\Scripts\compile.bat
```

保留的 IBL Compute Shader：

```powershell
.\res\Scripts\compile_ibl.bat
```

IBL 默认停用，因此正常运行当前管线不需要重新生成 IBL 预计算结果。
