# 跨 API RHI 后端架构

## 目标

RHI 公共层只描述 GPU 工作，不保存 Vulkan 枚举或 SPIR-V 资产路径。当前只实现 Vulkan 后端，但接口允许后续增加 D3D12 后端而不修改 RenderGraph、FramePacket 与 RendererRuntime。

```text
RenderGraph
    ↓
FramePacket（HLSL Source / Entry / Permutation）
    ↓
RHI Execution Plan（Format / State / Pipeline Description）
    ↓
IDeviceBackend + ICommandListBackend
    ↓
IFrameExecutorBackend + IFrameSchedulerBackend
    ↓
Vulkan Backend
```

## 公共层边界

`RHITypes.h` 定义 API 无关的格式、Shader Stage、Buffer Usage、拓扑、光栅化状态和 Image Layout。公共执行计划只使用这些类型，具体后端负责翻译。

`Device` 只负责公共资源作用域与对象包装，实际的设备、队列、交换链、上传器、IBL 处理器以及资源创建由 `IDeviceBackend` 持有。后端工厂由所选后端的 CMake 源文件提供，公共实现不再包含 Vulkan 条件分支。

`Buffer`、`Image`、`Pipeline`、`DescriptorSetLayout` 与 `DescriptorPool` 是轻量公共包装；其生命周期和操作分别委托给 `IBufferBackend`、`IImageBackend`、`IPipelineBackend` 及描述符后端。Vulkan RAII 对象只存在于 `src/RHI/Vulkan`。

`CommandList` 对 Renderer 暴露资源对象和抽象命令，全部录制操作转发给 `ICommandListBackend`，不再公开 Pipeline、Pipeline Layout 或 Command Buffer 原生句柄。少量必须访问原生对象的后端代码通过受限的 `BackendAccess` 访问，Render 与 Renderer 模块无法调用该通道。

`FrameScheduler` 负责帧获取、Fence、提交、Present 与交换链重建，其实现委托给 `IFrameSchedulerBackend`。`FrameExecutor` 负责资源解析、Pipeline 编译、Barrier 与 Pass 执行，其实现委托给 `IFrameExecutorBackend`。

场景与 UI 使用独立 CommandList/CommandBuffer。FrameExecutor 只生成场景工作；UI 后端在场景结果上
以 `loadOp = LOAD` 追加 ImGui，两个 CommandBuffer 由 FrameScheduler 放入同一次提交。这样 UI
录制与场景 Pass 保持模块边界，同时由同一个 Render-Finished Semaphore 和帧 Fence 覆盖。

## 帧同步与退出边界

帧 Fence 只证明引用该 Fence 的 Queue Submit 已完成，不能单独证明 Present Engine 或未绑定该
Fence 的外部工作已经停止。因此 RHI 区分两级同步：

- 帧级同步：复用 Frame Slot 前等待 Fence；只有确定即将 Queue Submit 时才 Reset Fence；
- 设备级同步：所有生产者与 RHI Worker 停止后调用 `Device::waitIdleForShutdown()`，再析构后端对象。

关闭请求到达后，不再接受新的帧生产。尚未 Acquire 的排队提交可以直接取消；已 Acquire 或已录制
但尚未 Submit 的帧通过 `abortFrame()` 回收本帧状态；已经 Submit 的帧必须等待完成。Fence 等待采用
有限超时，并附带 Frame Slot、提交序号和处理阶段日志，避免退出时出现无诊断的永久阻塞。

`waitIdleForShutdown()` 属于 `IDeviceBackend`，公共层不依赖 `vkDeviceWaitIdle`。Vulkan 实现负责将
设备丢失转换为诊断信息，并保证 NGX 等外部 GPU Feature 在设备和其资源销毁前完成释放。

完整的线程所有权和退出状态见
[多线程帧流水线与安全退出](RUNTIME_THREADING_AND_SHUTDOWN_CN.md)。

## Shader 资产

Render 层的 Shader 描述由以下信息组成：

- HLSL 源文件；
- Shader Stage；
- Entry Point；
- 可选 Permutation Key。

Render 层不再引用 `.spv`。Vulkan 后端通过 `VulkanShaderBinary` 将描述解析到构建阶段生成的 SPIR-V 文件。未来 D3D12 后端可以使用相同描述解析 DXIL，而无需修改 Pipeline 定义。

## UI 后端

`UIOverlay` 负责 ImGui 上下文和界面生命周期，具体图形 API 初始化与 DrawData 录制由 `IUIBackend` 完成。Vulkan 实现在 `src/ui/Vulkan`，并通过 `VulkanRenderOverlayBackend` 与 Vulkan CommandList 对接。

## 构建选择

后端由 CMake Cache 变量选择：

```powershell
cmake -B cmake-build-debug `
  -DTASROVY_RHI_BACKEND=Vulkan `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

当前允许值为 `Vulkan` 与预留的 `D3D12`。在 D3D12 后端实现完成前，选择 `D3D12` 会在配置阶段给出明确错误。

## 新后端接入点

新增后端需要实现以下边界：

1. `IDeviceBackend` 以及 Buffer、Image、Pipeline 和 Descriptor 的资源后端；
2. `ICommandListBackend` 命令翻译；
3. `IFrameSchedulerBackend`；
4. `IFrameExecutorBackend`；
5. Shader 二进制解析或编译；
6. UI Backend；
7. 对应的 CMake 条件源文件与依赖。

RenderGraph 排序、Hazard 分析、FramePacket、RHI Execution Plan、GPUScene 和三线程调度不属于后端接入范围。
