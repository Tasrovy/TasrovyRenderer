# 基于 Vulkan 的现代实时渲染引擎

## 项目概述

本项目是一个使用 C++20、Vulkan 和 HLSL 从零开发的现代实时渲染引擎。

项目最初以 PBR 与 IBL 渲染为核心，随后逐步扩展为包含延迟渲染、RenderGraph、RHI、阴影、时域抗锯齿、屏幕空间反射和多级后处理的完整渲染框架。

当前项目不再只是对 Vulkan 对象的简单封装，而是建立了清晰的分层架构：

```text
Scene / Pipeline
        │
        ▼
     Render
        │
        ▼
 RenderGraph
        │
        ▼
   FramePacket
        │
        ▼
RHI Execution Plan
        │
        ▼
  RHI Backend
        │
        ▼
       GPU
```

其中，Render 系统负责描述场景、材质、渲染管线和资源依赖；RendererRuntime 负责组织场景代理与一帧的渲染过程；RHI 负责解析帧数据、管理 GPU 资源；当前选择的后端负责将抽象命令翻译为实际图形 API 调用。

这种设计使上层渲染算法不直接依赖 `VkImage`、`VkBuffer`、`VkPipeline` 等 Vulkan 类型，为后续增加新渲染算法或扩展其他图形 API 后端提供了基础。

## 核心技术与实现亮点

### 1. 分层渲染架构

#### Render 系统

Render 系统是 API 无关的渲染描述层，负责：

- Scene、Camera、Light、Object 和 Transform 等场景数据；
- Mesh、Material、Texture 和 Shader 等逻辑资源；
- Pipeline、PipelinePass 和渲染状态描述；
- Deferred、PBR、阴影和后处理 Pass 的组织；
- RenderGraph 的构建与校验；
- 将场景和管线编译为不可变的 `FramePacket`。

Render 层不创建 Vulkan Buffer、Image 或 Pipeline，也不执行底层图形命令。

#### RendererRuntime 系统

参考 Unreal Engine 的场景渲染架构，对原有 `SceneRenderer` 进行拆分，将不同职责划分到独立模块：

- `SceneRenderer`：渲染流程入口与高层协调；
- `RenderScene`：维护渲染线程使用的场景状态；
- `SceneUpdateCoordinator`：集中消费 Snapshot，并比较 Structural、Transform、Material、Lighting、Pipeline 五类 Generation；结构或管线变化触发重建，变换、材质参数和光照变化增量合并到渲染线程 Scene；
- `PrimitiveSceneProxy`：保存场景对象的渲染代理数据；
- `FrameOrchestrator`：组织 FramePacket、执行计划和帧流程；
- `RenderFrameSubmission`：作为 Render Thread 到 RHI Thread 的完整按值提交边界；
- `RendererDebugUI`：独立承载运行时设置、场景检查与调试输出；
- `RenderThread`：管理独立渲染线程；
- `RHIThread`：独占 CommandBuffer 录制、帧 Buffer 上传与 Queue Submit；
- `RendererRHIContext`：集中管理 Device、CommandList 和 FrameExecutor；
- `ResourceMonitor`：统计和显示 GPU 资源状态。

场景对象可以通过 Proxy 增量同步到 RenderScene，避免每帧完整重建渲染场景。

当前 CPU 采用 Main/Game、Render、RHI 三线程职责拆分。Main Thread 处理 GLFW、ImGui
和场景写入；Render Thread 在帧边界消费 UI 命令与 Scene Snapshot，维护私有 RenderScene，
并生成不可变的 `RenderFrameSubmission`；RHI Thread 按序消费有界队列，在对应 Frame Slot
可复用后完成 Buffer 上传、CommandList 录制、Queue Submit 与 Present。

队列容量与 `maxFramesInFlight` 对齐，使 Render N+1 可以与 RHI N 重叠，同时阻止 CPU
无限领先 GPU。提交对象按值保存 FramePacket、Execution Plan、Bindings 和上传字节，资源通过
`shared_ptr` 保活，不携带可变 Scene 引用。RenderGraph、Swapchain 和场景 GPU 资源重建统一
进入 RHI 队列，并在结构性边界排空已有提交。

UI 与场景分别录制到两个 CommandBuffer，再按 `[Scene, UI]` 顺序放入同一次 Graphics
Queue Submit。Main Thread 产生不可变 ImGui DrawData token；调试设置通过合并式命令邮箱送往
Render Thread，运行状态通过双缓冲只读 Snapshot 返回 Main Thread，因此 UI 不直接访问渲染线程
状态，也不在场景录制期间持有 UI 互斥锁。

关闭流程会先停止新帧生产，取消尚未 Acquire 的排队任务，终止已录制但尚未 Submit 的帧，
再排空已经提交的 GPU 工作。帧 Fence 使用有限超时并记录帧槽与阶段；所有工作线程停止后，
后端执行一次设备级空闲等待，再销毁 NGX、交换链、Semaphore 和其他 Vulkan 对象。
详细状态与所有权规则见 [多线程帧流水线与安全退出](RUNTIME_THREADING_AND_SHUTDOWN_CN.md)。

#### RHI 与图形 API 后端

RHI 提供与图形 API 无关的资源和命令接口，包括：

- Device；
- Buffer；
- Image；
- Pipeline；
- Descriptor；
- CommandList；
- Pass；
- FrameScheduler；
- FrameExecutor。

当前 Vulkan 后端负责将这些抽象类型翻译为：

- `VkBuffer`；
- `VkImage`；
- `VkPipeline`；
- `VkDescriptorSet`；
- `VkCommandBuffer`；
- Vulkan Pipeline Stage、Access Mask 与 Image Layout。

上层 Renderer 不直接使用 Vulkan 的同步标志和资源类型。

公共 RHI 使用自有 Format、Shader Stage、Buffer Usage、光栅化状态和 Image Layout；FrameScheduler 与 FrameExecutor 分别通过后端接口执行帧调度和计划翻译。Shader 资产保存 HLSL Source、Entry Point 与 Permutation Key，具体后端解析 SPIR-V 或未来的 DXIL。详细边界见 [跨 API RHI 后端架构](RHI_BACKEND_ARCHITECTURE_CN.md)。

### 2. RenderGraph 与帧数据编译

#### RenderGraph

根据各个 PipelinePass 声明的资源读写关系，自动构建渲染依赖图。

RenderGraph 负责：

- 分析 Pass 的资源读取和写入；
- 建立生产者与消费者之间的依赖；
- 检测 Read After Write；
- 检测 Write After Read；
- 检测 Write After Write；
- 计算资源第一次和最后一次使用位置；
- 标记外部资源和跨帧历史资源；
- 检查未声明资源与错误依赖；
- 输出 RenderGraph 诊断信息。

#### FramePacket

`FrameCompiler` 将当前场景和 Pipeline 编译成一份与 Vulkan 无关的 `FramePacket`。

FramePacket 包含：

- 当前帧编号；
- 相机矩阵与抖动信息；
- 当前帧和上一帧模型矩阵；
- Mesh 与 Material 资源引用；
- DrawPacket；
- Pass 状态；
- Shader 描述；
- 纹理声明；
- Pass 资源读写关系；
- 跨帧历史资源引用；
- RenderGraph 诊断信息。

FramePacket 是 Render 与 RHI 之间的主要数据边界。

### 3. GPU 资源生命周期与同步

#### 资源生命周期分析

RHI 根据 FramePacket 编译 `RHI Execution Plan`，计算每个资源的：

- 第一次使用 Pass；
- 最后一次使用 Pass；
- 是否属于外部资源；
- 是否需要跨帧保存；
- 是否作为 Storage 资源使用；
- 是否可以与其他瞬态资源共享分配槽。

#### 瞬态资源复用

对于生命周期不重叠且格式、尺寸和用途兼容的纹理，RHI 会将其分配到相同的临时资源槽中。

该机制能够减少多 Pass 渲染过程中重复创建的中间纹理数量，为后续实现更完整的显存别名和瞬态资源池提供基础。

执行计划同时根据资源用途选择物理驻留方式：普通跨 Pass 资源默认共享；需要跨帧历史或并发写入的
资源按 Frame Slot 保存；交换链等资源由外部持有；生命周期不重叠且描述兼容的瞬态资源允许复用
分配槽。可选效果关闭时不创建其专属历史资源，例如关闭描边时不分配 `OutlineHistory`。

#### 自动资源屏障

RHI 根据资源前后状态生成资源转换计划，Vulkan Executor 将抽象状态翻译为：

- Pipeline Stage；
- Access Mask；
- Image Layout；
- Buffer Memory Barrier；
- Image Memory Barrier。

Renderer 只描述资源需要进入的状态，不再直接传递 `VK_PIPELINE_STAGE_*` 或 `VK_ACCESS_*` 等 Vulkan 常量。

### 4. 现代化 Vulkan 渲染框架

#### RAII 资源管理

对 Vulkan 核心资源进行 C++ 封装，使用 RAII 自动管理生命周期，包括：

- Vulkan Instance；
- Physical Device 与 Logical Device；
- Swapchain；
- Buffer；
- Image 与 Image View；
- Pipeline；
- Descriptor Pool 与 Descriptor Set；
- Command Pool 与 Command Buffer；
- Fence、Semaphore 和 Query Pool。

所有 Vulkan 包装对象均禁止复制，通过明确的所有权关系减少资源泄漏和重复释放问题。

#### Dynamic Rendering

使用 Vulkan Dynamic Rendering 组织图形 Pass，不依赖传统的 `VkRenderPass` 和 `VkFramebuffer` 对象。

不同 Pass 可以根据颜色附件、深度附件和分辨率动态构建渲染目标，便于组织 GBuffer、阴影和后处理流程。

#### 多帧并行

通过 Frames In Flight 机制维护每帧独立的：

- Command Buffer；
- Fence；
- Semaphore；
- Uniform Buffer；
- Descriptor；
- Timestamp Query；
- 历史纹理。

使用 Fence 和 Semaphore 协调 CPU、GPU 与 Swapchain，避免覆盖仍在使用的帧资源。
Render Thread 使用缓存的显示尺寸与预测帧槽准备下一帧，避免在 RHI Thread 修改
FrameScheduler 时并发读取其可变状态；每帧 Buffer 上传仍延迟到对应 Fence 完成之后。

### 5. 延迟渲染与 PBR

#### GBuffer

使用 Deferred Rendering 将几何阶段和光照阶段分离。

GBuffer 保存：

- 世界空间位置或深度；
- 法线；
- Albedo；
- Metallic；
- Roughness；
- Ambient Occlusion；
- Emissive；
- Motion Vector；
- 其他材质或调试信息。

光照阶段通过读取 GBuffer 统一计算场景中的直接光照和环境光照。

#### Cook-Torrance BRDF

在 HLSL 中实现基于 Cook-Torrance 微表面模型的 PBR 光照，包括：

- GGX 法线分布函数；
- Smith 几何遮蔽函数；
- Schlick Fresnel；
- Metallic/Roughness 材质工作流；
- 能量守恒的漫反射与镜面反射组合。

#### 法线贴图

支持切线空间法线贴图工作流：

- Mesh 顶点包含 Normal 与 Tangent；
- Shader 中构建 TBN 矩阵；
- 将法线贴图转换到世界空间；
- 参与 PBR 光照、阴影和屏幕空间效果计算。

#### Compute 光源剔除

光照前先按屏幕 Tile 与深度分片执行 Compute Pass。Shader 根据 Tile 视锥、深度范围和光源
包围体相交关系，为最多 256 个动态光源生成位掩码；Lighting Pass 只遍历当前区域命中的 bit，
避免每个像素扫描完整光源数组。光源数据保存在 Scene Light SSBO，剔除结果通过自动生成的
Compute → Fragment Barrier 交给后续光照阶段。

### 6. 基于图像的光照

#### 天空盒系统

保留 Cubemap 天空盒与环境贴图实现。当前默认运行路径关闭天空盒发现、几何创建和 Cubemap
驻留，只创建固定描述符布局需要的中性 IBL fallback，避免未使用环境资源长期占用显存。

#### IBL 预计算

通过 Compute Shader 对 HDR 环境贴图进行预处理，生成 PBR 所需的环境光照资源：

- Irradiance Map：用于环境漫反射；
- Prefiltered Environment Map：根据不同 Roughness 生成多级镜面反射；
- BRDF Integration LUT：预计算 BRDF 镜面反射积分结果。

运行时将直接光照与 IBL 环境光照组合，实现材质对环境颜色和反射信息的响应。

IBL 提供的是环境光照近似，不将其描述为完整的动态全局光照系统。

### 7. 可切换阴影系统

引擎将阴影方案作为 RendererSettings 的一部分，使不同阴影技术可以按需切换。

#### Shadow Map

实现基础方向光 Shadow Map，用于简单场景或调试阴影流程。

#### Cascaded Shadow Maps

实现级联阴影贴图，将相机视锥划分为多个距离区间：

- 近处级联使用更高的有效阴影分辨率；
- 远处级联覆盖更大的场景范围；
- 根据像素深度选择对应 Cascade；
- 支持级联范围和阴影参数调节。

#### Virtual Shadow Map

实现基础的 Virtual Shadow Map 方案，包括：

- 虚拟阴影页描述；
- 物理阴影 Atlas；
- Page 到 Atlas 区域的映射；
- 页面对应的 Viewport 与 Scissor；
- 独立的 Virtual Shadow Atlas 资源。

当前 VSM 属于基础实现，后续仍可继续扩展页面请求、缺页反馈、缓存淘汰和按需更新机制。

### 8. Hi-Z 与屏幕空间反射

#### Hi-Z 深度层级

根据场景深度逐级生成 Hi-Z Mipmap：

- 每一级保存对应区域的深度范围；
- 为屏幕空间射线追踪提供快速深度查询；
- 支持逐 Mip 调试预览；
- 可作为后续遮挡剔除和屏幕空间 GI 的基础数据。

#### SSR

使用 Hi-Z 进行屏幕空间射线步进，实现 Screen Space Reflection：

- 根据表面法线和观察方向生成反射射线；
- 在屏幕空间中查询深度交点；
- 通过 Hi-Z 加速射线追踪；
- 根据命中置信度混合反射结果。

SSR 只使用当前屏幕内可见信息，因此不能替代完整的全局光照系统。

### 9. 时域抗锯齿与运动向量

#### Camera Jitter

使用低差异采样序列对投影矩阵进行亚像素抖动，使连续多帧覆盖不同的像素采样位置。

抖动作用于相机投影矩阵，因此影响所有由该相机生成的几何结果，而不是只偏移某一张 GBuffer。

#### Motion Vector

GBuffer 阶段根据当前帧和上一帧的变换矩阵计算运动向量，用于：

- 历史颜色重投影；
- TAA；
- TAAU；
- Motion Blur；
- 描边历史降噪。

#### TAA 与 TAAU

实现基于历史重投影的时域抗锯齿流程：

- 根据运动向量查找上一帧像素；
- 检查历史采样有效性；
- 对历史颜色进行邻域约束；
- 混合当前帧与历史帧结果；
- 根据深度和运动信息降低拖影；
- 支持内部渲染分辨率到显示分辨率的时域放大。

当前历史验证对亚像素边缘采用前景/背景双向判断；重投影深度和法线从邻域候选中选择更匹配的
样本，历史颜色使用 `clip_aabb` 沿邻域中心方向裁剪。TAAU 在显示分辨率重建当前帧样本后再融合
历史，以降低细小几何在抖动采样下交替接受和拒绝造成的闪烁。

### 10. 后处理流水线

#### 多级 Bloom

Bloom 使用多级降采样和升采样，而不是单次大范围模糊：

1. 提取高亮区域；
2. 逐级降低分辨率；
3. 在不同尺度上进行滤波；
4. 从低分辨率逐级升采样；
5. 合并不同范围的光晕；
6. 与原始场景颜色合成。

这种方式能够同时生成小范围高亮扩散和大范围柔和光晕，并减少直接模糊光源轮廓产生的锯齿。

#### 描边与时域降噪

实现基于深度和法线差异的屏幕空间描边，并支持：

- 仅输出黑色描边；
- 描边强度和阈值调节；
- 使用上一帧描边结果进行历史重投影；
- 可选的描边时域降噪；
- 降低细线闪烁和明显锯齿。

#### 其他后处理

后处理管线还包括：

- Tone Mapping；
- Motion Blur；
- Depth of Field；
- Debug Output；
- TAA/TAAU；
- SSR 合成；
- 3D LUT 校色；
- 最终颜色输出。

#### DLSS Neural Rendering（可选）

管线可在最终阶段接入同分辨率 DLSS-NR 外部 Feature，输入线性颜色、深度和运动向量，并在
Feature 不可用或执行失败时回退到原生后处理结果。NGX 运行时由用户在本地提供，不随仓库分发。
同分辨率 RenderGraph 重建保留 Feature；只有分辨率变化、功能关闭或引擎退出时释放，强度、风格
和局部色调等参数在 Evaluate 阶段更新，不触发重新创建。

### 11. GPU 驱动渲染实验代码（当前停用）

项目保留了 GPU Driven GBuffer 的数据结构与 Shader 实验代码：

- CPU 准备 Draw 数据；
- Compute Shader 处理绘制命令；
- GPU 写入 Indirect Buffer；
- 在 Compute 与 Draw Indirect 阶段之间插入资源屏障；
- 使用 Indirect Draw 提交几何绘制。

该路径尚未纳入当前 FramePacket 与 RHI Execution Plan，运行时固定使用 CPU
生成 Draw Packet 的路径，界面不再提供 GPU Driven 切换项。保留的实验代码用于后续
实现 GPU Frustum Culling、Hi-Z Occlusion Culling 和完整的间接绘制 Pass。

### 12. 资产与材质系统

#### 资产转换层

通过独立的 Assets 模块连接 FileSystem 与 Render：

```text
FileSystem Model/Image
          │
          ▼
       Assets
          │
          ▼
Render Mesh/Texture Description
          │
          ▼
     RHI GPU Resource
```

FileSystem 负责文件读取和解码，Assets 负责格式转换，Render 只保存逻辑资源描述，RHI 负责 GPU 上传。

模型转换为 Render Mesh 时，Assets 使用 `meshoptimizer` 按材质子网格自动生成传统
LOD 链。各级 LOD 共用顶点数据，并连续存放在同一个索引缓冲中；FrameCompiler 根据
主相机下的屏幕覆盖率选择 LOD，只改变 Draw Packet 的索引范围。所有几何 Pass 复用同一
选择结果，以避免 GBuffer 与阴影之间使用不同层级。

场景由 `res` 目录中的 JSON 描述驱动加载，运行时枚举可用场景并进行选择。模型、材质、Transform、
光源和管线配置由场景文件建立，不要求在 `main()` 中硬编码演示对象。

#### 材质描述

材质系统支持：

- Float、Vector 和 Texture 参数；
- PBR 材质属性；
- 纹理颜色空间声明；
- 默认纹理与缺失纹理处理；
- Alpha Cutoff；
- 阴影投射开关；
- 材质表面类型；
- 外部材质描述文件加载。

`MaterialTechnique` 描述材质参与的渲染阶段及其 Shader Variant；FrameCompiler 根据场景、植被、
角色或特殊表面类型，为同一 Pass 中的不同 Draw 选择对应 Variant。Variant 由 HLSL Source、Entry
Point 和 Permutation Key 标识，上层不保存 SPIR-V 路径或 Vulkan Pipeline 句柄。

Shader 使用 HLSL 编写，并通过 DXC 编译为 SPIR-V。

### 13. 调试与开发工具

#### ImGui 调试界面

集成 Dear ImGui 构建运行时渲染调试面板，可调整：

- 相机参数；
- 模型 Transform；
- 光源颜色、方向和强度；
- 阴影模式与阴影参数；
- TAA/TAAU；
- Bloom；
- SSR；
- 描边；
- Motion Blur；
- DOF；
- 内部渲染分辨率；
- Debug Output；
- 天空盒与环境贴图。

#### Debug Output 纹理语义

为不同纹理提供专用预览语义，避免直接显示原始数据造成全黑、全红或全黄的问题。

支持预览：

- Albedo；
- Normal；
- Metallic/Roughness；
- Scene Depth；
- Motion Vector；
- Hi-Z 各级 Mip；
- Shadow Map；
- Virtual Shadow Atlas；
- SSR；
- 描边结果；
- 历史纹理；
- Bloom 中间结果。

系统会根据纹理语义进行范围映射、通道选择和可视化转换。

#### GPU 资源与性能监控

提供运行时资源监控和 GPU Timestamp：

- 跟踪 Buffer 与 Image；
- 显示资源尺寸和用途；
- 记录资源创建、销毁、驻留类型、分配槽和估算显存；
- 统计进程内存、已跟踪 GPU 内存及增长趋势；
- 记录不同 Pass 的 GPU 时间；
- 显示当前 CPU Draw Packet 路径的 Pass 性能；
- 辅助分析资源生命周期和 Pass 性能。

#### 图形调试流程

开发过程中使用：

- Vulkan Validation Layers；
- RenderDoc；
- ImGui Debug Output；
- GPU Timestamp；
- 资源状态诊断；
- RenderGraph Diagnostics。

这些工具用于定位：

- Descriptor 绑定错误；
- Image Layout 不匹配；
- Pipeline Barrier 缺失；
- 深度与运动向量显示异常；
- TAA 历史重投影闪烁；
- 阴影资源状态错误；
- 后处理锯齿和模糊问题。

## 总结

本项目已经从最初的 Vulkan PBR Demo，发展为包含完整分层架构和现代实时渲染功能的渲染引擎。

项目重点不仅是实现单独的图形效果，还包括：

- API 无关的 Render 描述层；
- RendererRuntime 场景渲染系统；
- RHI 与 Vulkan 后端分离；
- RenderGraph 资源依赖分析；
- FramePacket 帧数据边界；
- GPU 资源生命周期管理；
- Shared、Frame-buffered、External 与 Aliased 驻留策略；
- 自动资源屏障；
- Deferred PBR 与 IBL；
- SM、CSM 和基础 Virtual Shadow Map；
- Hi-Z 与 SSR；
- TAA/TAAU；
- 多级 Bloom 与时域后处理；
- Main、Render、RHI 三线程有界帧流水线；
- 独立 UI CommandBuffer、UI 命令邮箱与双缓冲调试快照；
- 可诊断、可取消的 Vulkan 安全退出流程；
- GPU Driven 数据结构与 Shader 实验代码（当前停用）；
- 可视化调试和资源监控。

通过该项目，我系统实践了从渲染算法、Shader 编写、GPU 资源管理到渲染架构设计和图形调试的完整开发流程，并构建了一个能够持续扩展新渲染技术的实时渲染基础框架。
