# 多线程帧流水线与安全退出

## 设计目标

运行时将窗口事件、帧编译和 GPU 提交拆到独立线程，并用有限的 Frames In Flight 控制延迟与
内存占用。线程之间只传递不可变提交或受控快照，不共享正在修改的 Scene、FramePacket 或
ImGui DrawData。

```text
Main Thread                 Render Thread                    RHI Thread
GLFW / ImGui / Scene  ->  Scene 同步与帧编译  ->  Acquire / Upload / Record
       |                         |                            |
       | UI 命令邮箱             | RenderFrameSubmission      | Scene + UI Submit
       v                         v                            v
双缓冲 Debug Snapshot       有界多帧队列                 Present / Frame Fence
```

## 线程职责

### Main Thread

- 处理 GLFW 事件、窗口关闭和尺寸变化；
- 执行 ImGui NewFrame，生成不可变 DrawData token；
- 修改主场景并发布 Scene Snapshot；
- 将渲染设置写入合并式 UI 命令邮箱；
- 只读取已发布的 Debug Snapshot。

### Render Thread

- 在帧边界消费最新 UI 命令；
- 比较 SceneVersions，并增量更新私有 RenderScene；
- 在结构、管线或尺寸变化时请求同步重建；
- 编译 FramePacket、RHI Execution Plan、Bindings 与 GPUScene 上传数据；
- 产生完整的 `RenderFrameSubmission` 并写入有界队列。

### RHI Thread

- 独占每帧 Buffer 更新、CommandList 录制和 Queue 操作；
- 等待目标 Frame Slot 的 Fence 后写入对应 per-frame 资源；
- 录制 Scene CommandBuffer 与 UI CommandBuffer；
- 在一次 Queue Submit 中按顺序提交两者并执行 Present；
- 发布完成状态、GPU Timestamp 和 Swapchain 重建请求。

## 提交边界与背压

`RenderFrameSubmission` 是 Render 到 RHI 的不可变边界，保存帧号、预测 Frame Slot、
FramePacket、Execution Plan、资源绑定、Buffer 上传字节和 UI frame token。资源引用使用
`shared_ptr` 保持生命周期，但提交不持有可变 Scene。

RHI 工作队列的最大未完成任务数与 Frames In Flight 对齐。Render Thread 可以在 RHI 处理 N 帧
时准备 N+1，但队列达到容量后必须等待；这既提供 CPU 并行，也防止提交、UI 快照和上传数据无限
堆积。RHI 始终按序消费，只有即将复用某个 Frame Slot 时才等待其 Fence。

## UI 数据同步

UI 有两条方向相反的数据通道：

- Main → Render：设置变更写入单槽命令邮箱；连续操作会合并，Render Thread 在下一帧边界消费；
- Render → Main：运行时状态写入双缓冲 Snapshot，Main Thread 读取已发布的只读副本。

ImGui DrawData 在 Main Thread 完成后复制为带引用计数的 frame token。RHI 录制完成或取消该帧时
释放 token，避免下一次 NewFrame 覆盖仍在使用的顶点、索引或命令数据。场景与 UI 分别使用
CommandBuffer，但在同一次 Queue Submit 中保持确定顺序。

## 资源驻留

多帧并行不意味着所有资源都复制多份。执行计划按用途选择驻留策略：

| 类型 | 用途 |
|---|---|
| Shared | 多帧只读或由同步保证安全复用的普通资源 |
| FrameBuffered | 会被并发帧写入的 Uniform、历史或中间资源 |
| External | Swapchain、外部 Feature 等由外部系统持有的资源 |
| Aliased | 生命周期不重叠且描述兼容的瞬态资源槽 |

资源只有在并发访问确实需要隔离时才按 Frame Slot 复制。可选 Pass 关闭后不声明其专属资源，从
RenderGraph 源头避免无效分配。DLSS-NR Feature 在同分辨率重建时保留，仅在分辨率变化、功能关闭
或引擎退出时释放。

## Swapchain 重建

窗口尺寸变化或 Acquire/Present 返回不可用状态时，Render Thread 先停止继续扩大投机窗口并排空
相关提交，再通过 RHI 队列执行 Swapchain 重建。最小化导致 framebuffer extent 为零时暂停生产，
直到 Main Thread 发布新的有效尺寸。显示资源与内部渲染资源分开重建，避免仅窗口变化时重建整个
场景资源集合。

## 安全退出

退出按以下顺序收敛：

1. Main Thread 发布停止生产请求；
2. Render Thread 不再产生新帧；
3. RHI 取消尚未 Acquire 的任务；
4. 已录制但尚未 Submit 的帧执行 `abortFrame()`；
5. 等待已经 Submit 的帧与所有 Frame Fence；
6. 停止并连接 RHI Worker；
7. 调用后端 `waitIdleForShutdown()` 完成设备级同步；
8. 释放 NGX Feature、ImmediateSubmitter、交换链与 Vulkan 资源；
9. 销毁 Logical Device、Instance 和窗口。

Fence 只在真正提交前 Reset。如果提交失败，调度器恢复可等待状态，避免留下永远不会被 GPU
触发的未签名 Fence。所有 Fence 等待使用有限超时，并记录帧号、Frame Slot 和当前阶段，以便区分
卡在队列、Acquire、录制、Submit、Present 还是设备丢失。

## 关键约束

- GLFW 与 ImGui NewFrame 只在 Main Thread 调用；
- Vulkan Queue 与每帧可变 GPU 资源只由 RHI Thread 操作；
- Render Thread 不读取 FrameScheduler 的并发可变状态；
- 结构性重建必须通过 RHI 队列，并与已提交工作建立明确边界；
- Frame Fence 完成不等同于整个设备空闲，最终析构前仍需要设备级同步。
