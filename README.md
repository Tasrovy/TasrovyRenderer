# TasrovyRenderer

[English](docs/README_EN.md)

TasrovyRenderer 是一个使用 C++20 和 Vulkan 编写的学习型实时渲染器。项目包含 API 无关的渲染层与 RHI、Vulkan Dynamic Rendering、延迟渲染、PBR/IBL、阴影、屏幕空间效果、时间抗锯齿、场景序列化以及 ImGui 调试工具。

当前默认运行延迟渲染管线，前向渲染管线仍作为备用路径保留。

## 演示模型与署名

项目当前使用的角色模型来源如下：

- 【模型所属】永雏塔菲
- 【建模】Francesca

模型及相关美术资源的权利归原作者和所属方所有。本仓库中的相关资源仅用于渲染技术学习与效果演示；复制、分发或用于其他用途时，请遵守原模型及资源的授权要求。

当前模型已经整理为三个使用不同 UV 和材质的网格：

- `Body`：身体与服装，使用 `cloth.png`
- `Hair`：头发，使用 `hair.png`
- `Face`：面部，使用 `face.png`

当前版本未保留表情网格、骨骼、蒙皮、形态键和动画数据。OBJ 按面角展开顶点，不执行跨 UV 接缝的顶点去重，以保留 UV 与材质边界。

## 默认场景

程序启动时优先加载：

```text
res/Scenes/CornellTaffy.scene.json
```

如果场景文件不存在或无法读取，程序会创建并保存默认 Cornell Box 场景。场景包含：

- 地面、天花板、后墙、红色左墙和绿色右墙
- 顶部自发光面板
- Taffy 模型及 Body/Hair/Face 三套材质
- 一个主面光源
- 一个辅助点光源
- 一个辅助平行光
- 主相机与可选天空盒/IBL

为了测试运动向量与 TAA，当前 Taffy 会在渲染线程中以每秒 45 度绕 Y 轴旋转。

## 延迟渲染流程

默认管线按照以下顺序执行：

```text
Shadow
  -> GBuffer
  -> HBAO (1/2 resolution, optional)
  -> Hi-Z Half/Quarter/Eighth/Sixteenth (SSR only)
  -> Deferred Lighting
  -> Skybox
  -> Transparent
  -> BloomLowRes (1/4 resolution, optional)
  -> PostProcessing
  -> Swapchain
```

SSR 关闭时不会执行 Hi-Z Pass，HBAO 和 Bloom 关闭时也会跳过对应 Pass。

### GBuffer

GBuffer 保持五个颜色附件：

| 资源 | 格式 | 内容 |
| --- | --- | --- |
| `GBufferAlbedo` | RGBA16F | RGB 基础色，A 为 Motion Vector X |
| `GBufferNormal` | RGBA16F | RGB 编码法线，A 为 Motion Vector Y |
| `GBufferMaterial` | RGBA8 | Metallic、Roughness、AO、Shading Model |
| `GBufferWorldPos` | RGBA16F | 世界坐标与 Rim Power |
| `GBufferEffects` | RGBA16F | Rim Color 与 Rim Strength |
| `SceneDepth` | D32F | 场景深度 |

运动向量由当前帧与上一帧的 Model、View 和带 jitter 的 Projection 计算，支持摄像机运动和独立物体运动，不需要增加第六个 MRT。

### Lighting

Deferred Lighting 支持：

- Metallic/Roughness PBR
- 平行光、点光源与面光源
- Irradiance Map、Prefiltered Specular Map 与 BRDF LUT
- 半分辨率 HBAO
- 可选 SSDO
- Rim Lighting
- 2048×2048 阴影图
- Adaptive PCSS

Adaptive PCSS 首先判断阴影覆盖是否位于边缘。纯亮区和纯阴影区直接返回；只有边缘区域才进行 4–8 个 blocker 样本和 6–12 个过滤样本，避免固定执行 16+16 次采样。

### 后处理

最终后处理包括：

- TAA
- 分层深度 SSR
- 四分之一分辨率 Bloom
- Exposure/Tone Mapping
- 基于法线变化的描边

TAA 使用 GBuffer 运动向量、上一帧颜色和深度、Halton jitter、深度拒绝、邻域裁剪与运动自适应历史权重。

SSR、Bloom 和 Outline 使用 8 个预编译 Shader permutation。关闭功能时会选择不包含对应代码的 SPIR-V，而不是只依赖运行时分支。

## GPU 性能分析

渲染器为每个 frame-in-flight 创建独立的 Vulkan timestamp QueryPool，并记录每个实际执行 Pass 的 GPU 起止时间。

ImGui 的 `Resource Monitor` 可以查看：

- 每个 Pass 的 GPU 时间（毫秒）
- 已测量 Pass 的 GPU 总时间
- 进程 Private Bytes、Working Set 与历史趋势
- 托管 Vulkan 资源数量和估算容量
- 延迟删除队列大小
- 持续内存增长检测

GPU Pass 时间不包含 CPU 工作、ImGui 绘制和 Present。

## 线程与资源生命周期

- 主线程负责窗口事件
- Render Thread 负责场景更新、描述符更新和渲染命令生成
- Vulkan Queue 的主机访问通过互斥同步
- 多帧并行使用独立 command buffer、fence、descriptor 和渲染目标
- 资源销毁使用 fence 与延迟删除，不在正常帧循环调用 `vkDeviceWaitIdle`
- 窗口大小变化时实时重建交换链、RenderTexture、Pass 和相关 Pipeline

## 色彩与矩阵约定

- Base Color 与 Emissive 纹理按 sRGB 导入并在采样时解码
- GBuffer、Lighting、IBL 和后处理计算在线性空间进行
- 最终显示阶段进行色调映射与显示编码
- 矩阵在上传前转置，以匹配 HLSL/Vulkan 的当前布局约定
- Object 可独立控制 Projection Y 翻转，并同步调整动态 Front Face

## ImGui 调试功能

- 延迟/前向管线切换
- RenderTexture 与 GBuffer 查看
- 每个 Object 的 Transform 和 PBR 参数
- 材质 UV 模式、Scale、Offset 与 Repeat 调试
- Object Projection Y 翻转
- HBAO、SSDO、SSR、Adaptive PCSS 开关与参数
- TAA、Bloom、Outline 和曝光控制
- 场景灯光编辑
- GPU Pass Timing 与资源/内存监控

## 项目结构

```text
TasrovyRenderer/
├─ src/
│  ├─ base/             数学类型与基础工具
│  ├─ render/           Scene、Object、Material、Mesh 与 Pipeline 抽象
│  ├─ RHI/              API 无关的渲染硬件接口
│  │  └─ Vulkan/        Vulkan 后端
│  ├─ filesystem/       模型、图片和动画资源加载
│  ├─ ui/               ImGui 界面
│  ├─ window/           窗口与输入
│  ├─ logger/           日志系统
│  └─ core/             程序入口、默认场景与场景序列化
├─ res/
│  ├─ Models/Taffy/     Taffy OBJ 与 MTL
│  ├─ Textures/Taffy/   Taffy 基础色及默认辅助纹理
│  ├─ Scenes/           序列化场景
│  ├─ Skyboxes/         天空盒资源
│  ├─ Shaders/
│  │  ├─ Source/        HLSL/GLSL 源文件
│  │  ├─ Bin/           SPIR-V 二进制
│  │  └─ IBL/           IBL Shader
│  └─ Scripts/          Shader 编译脚本
└─ docs/                补充文档
```

## 依赖

- Windows
- CMake 3.15 或更高版本
- 支持 C++20 的编译器
- Vulkan SDK
- DirectX Shader Compiler（DXC）
- vcpkg

第三方依赖由根目录的 `vcpkg.json` 管理，主要包括 Vulkan、GLFW、GLM、Assimp、ImGui、Taskflow、spdlog、stb 和 SPIRV-Cross。

## 构建

将 `VCPKG_ROOT` 环境变量设置为本机的 vcpkg 安装目录，然后执行：

```powershell
cmake -B cmake-build-debug -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build cmake-build-debug --config Debug
```

运行 Debug 版本：

```powershell
.\cmake-build-debug\src\core\TasrovyCore.exe
```

构建 Release 版本：

```powershell
cmake --build cmake-build-debug --config Release
```

## 编译着色器

普通渲染 Shader 与 IBL Shader 分别通过以下脚本编译：

```powershell
.\res\Scripts\compile.bat
.\res\Scripts\compile_ibl.bat
```

脚本使用 DXC 将 HLSL 编译为 Vulkan SPIR-V，并写入对应的 `res/Shaders/Bin` 或 IBL Bin 目录。普通编译脚本同时生成 SSR/Bloom/Outline 的 8 个后处理 permutation。

## 模型资源位置

```text
res/Models/Taffy/Taffy.obj
res/Models/Taffy/Taffy.mtl
res/Textures/Taffy/cloth.png
res/Textures/Taffy/hair.png
res/Textures/Taffy/face.png
```
