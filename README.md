# TasrovyRenderer

TasrovyRenderer 是一个 C++20 Vulkan 渲染器，目标是把上层渲染对象、RHI 抽象、异步资源加载、PBR 材质、天空盒、预计算 IBL 和 ImGui 调试工具串成一条可迭代的渲染主流程。

[English](docs/README_EN.md)

## 当前能力

- 基于 Vulkan 的 RHI 后端
- 面向渲染层的 `SceneRenderer`
- 场景驱动的 mesh、material、pass、pipeline 创建流程
- PBR forward shader 和基础材质贴图绑定
- 天空盒渲染和 ImGui 实时切换
- 每个天空盒预计算 IBL 贴图：
  - irradiance cubemap
  - prefiltered specular cubemap
  - BRDF LUT
- 通过 Taskflow 进行异步模型和图片资源加载
- ImGui 场景调试面板：相机、灯光、物体、材质、天空盒
- HLSL 通过 DXC 编译到 SPIR-V

## 目录结构

```text
src/base/        数学类型和基础工具
src/filesystem/  模型、图片等资产加载，以及 Taskflow AssetManager
src/render/      API 无关的场景、材质、网格、管线和 pass 描述
src/RHI/         RHI 抽象层和 SceneRenderer
src/RHI/Vulkan/  Vulkan 后端资源、管线、交换链和 IBL 计算
src/ui/          ImGui overlay
src/window/      窗口系统
src/log/         日志系统
src/core/        程序入口
res/             模型、贴图、天空盒、shader 和 SPIR-V 输出
```

## 环境要求

- Windows
- CMake 3.20+
- Vulkan SDK，并包含 DXC
- vcpkg
- 与项目配置匹配的 C++20 编译工具链

依赖由根目录的 `vcpkg.json` 声明，包括 Vulkan 相关库、Assimp、stb、spdlog、spirv-cross 和 Taskflow。

## 构建

配置：

```powershell
cmake -B cmake-build-debug -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
```

构建：

```powershell
cmake --build cmake-build-debug --config Debug
```

运行：

```powershell
.\cmake-build-debug\src\core\TasrovyCore.exe
```

其中 `<vcpkg-root>` 是你本机 vcpkg 的安装目录。

## Shader 编译

主渲染 shader：

```powershell
cd res
cmd /c compile.bat
```

IBL compute shader：

```powershell
cd res\IBLComputeShader
cmd /c compilecomputes.bat
```

## 渲染流程

应用层创建 render 模块中的 `Scene` 和 `PBRPipeline`，然后提交给 `Tasrovy::RHI::SceneRenderer`。

`SceneRenderer` 持有渲染线程自己的场景快照。它会把上层 scene 和 pipeline 解析为 RHI 资源，创建 pass attachment，上传 mesh buffer，加载材质贴图，更新每帧 uniform buffer，并通过 `CommandList` 录制 draw 命令。

当前管线包含 shadow、g-buffer、skybox、forward、transparent 和 post-processing 等阶段。现阶段主要可见路径集中在 skybox 和 forward 主物体渲染。

## 资源加载

模型、图片等较大的 CPU 侧资产由 `src/filesystem` 负责加载。

`Tasrovy::FS::AssetManager` 使用 Taskflow worker 异步加载资源，并通过完成事件通知调用侧。渲染层和核心层持有资源引用，避免在主渲染流程中同步解析模型或图片。

## 天空盒和 IBL

渲染器会在 `res` 下查找包含以下六张图的目录作为天空盒候选：

```text
right.png
left.png
top.png
bottom.png
front.png
back.png
```

每个天空盒候选都会提前创建 cubemap，并预计算对应的 IBL 资源。ImGui 的 `Skybox` 区域提供 `Environment` 下拉框，可以实时切换天空盒。切换时复用已缓存的 cubemap 和 IBL 贴图，不重新计算。

## 开发备注

- `SceneRenderer` 会保留一段旧 GPU 资源，避免场景刷新时销毁仍被已提交 command buffer 引用的 buffer。
- RHI 头文件保持 API 无关，Vulkan 细节集中在 `src/RHI/Vulkan/`。
- `imgui.ini` 属于本地 UI 状态文件，不应作为源码提交。
