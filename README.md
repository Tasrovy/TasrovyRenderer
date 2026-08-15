# TasrovyRenderer

[English](docs/README_EN.md)

TasrovyRenderer 是一个使用 C++20、HLSL 和 Vulkan 构建的实验性实时渲染器，重点关注现代渲染管线、图形 API 抽象和数据驱动的帧执行架构。

项目默认使用延迟渲染管线，同时保留轻量级前向 PBR 路径。

## 功能

- RenderGraph 资源依赖分析与自动同步
- API 无关的 FramePacket 与 RHI 执行计划
- Vulkan Dynamic Rendering 与自动 Pipeline Barrier
- Deferred PBR、IBL 和透明物体渲染
- Shadow Map、CSM、PCF / PCSS 和实验性 Virtual Shadow Map
- HBAO、Hi-Z、SSR、TAA / TAAU、Bloom、Motion Blur 与 DOF
- GPUScene 场景数据组织
- 每纹理独立 UV 采样变换
- Main、Render、RHI 三线程职责分离
- ImGui 调试界面、资源监控和 GPU Timestamp

## 架构

```text
Scene / Pipeline
       |
       v
RenderGraph -> FramePacket -> RHI Execution Plan -> Vulkan Executor
```

主要模块：

- `src/render`：场景、材质、管线、RenderGraph 和帧描述
- `src/renderer`：场景快照、帧协调、GPUScene 和线程调度
- `src/RHI`：资源、命令和执行计划抽象
- `src/RHI/Vulkan`：Vulkan 后端
- `src/assets`、`src/filesystem`：模型与纹理加载
- `src/ui`：运行时调试界面

更详细的设计说明见 [项目文档](docs/PROJECT_OVERVIEW_CN.md)。

## 环境要求

- Windows
- CMake 3.20+
- 支持 C++20 的工具链
- Vulkan SDK
- DirectX Shader Compiler（DXC）
- vcpkg

依赖声明位于 `vcpkg.json`。

## 构建

设置 `VCPKG_ROOT` 后执行：

```powershell
cmake -B cmake-build-debug `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build cmake-build-debug --config Debug
```

编译 Shader：

```powershell
.\res\Scripts\compile.bat
```

运行：

```powershell
.\cmake-build-debug\src\core\TasrovyCore.exe
```

## 项目状态

项目仍在开发中：

- Virtual Shadow Map 为用于验证页映射流程的简化实现
- IBL 预计算默认停用，相关后端实现仍被保留
- GPU Driven GBuffer 实验代码暂未接入当前运行时执行链
- 当前线程调度优先保证资源所有权和同步正确性

## 文档

- [项目架构与渲染流程](docs/PROJECT_OVERVIEW_CN.md)
- [GPUScene 与 Uniform 架构](docs/GPU_SCENE_UNIFORM_ARCHITECTURE_CN.md)

## 资源说明

演示角色模型归属永雏塔菲，建模作者为 Francesca。模型与美术资源仅用于渲染技术学习和效果展示，其权利归原作者及所属方所有。复制、分发或用于其他用途时，请遵守原资源的授权要求。
