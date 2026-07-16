# TasrovyRenderer

[English](docs/README_EN.md)

TasrovyRenderer 是一个使用 C++20 编写的 Vulkan 学习型渲染器。项目以自研渲染层和 RHI 为基础，使用 Vulkan Dynamic Rendering，现阶段重点实现了 PBR、延迟渲染、IBL、异步资源加载以及 ImGui 场景调试工具。

## 演示模型与署名

项目当前使用的角色模型来源如下：

- 【模型所属】永雏塔菲
- 【建模】Francesca

模型及相关美术资源的权利归原作者和所属方所有。本仓库中的相关资源仅用于渲染技术学习与效果演示；复制、分发或用于其他用途时，请遵守原模型及资源的授权要求。

当前导入的演示模型已经整理为三个使用不同 UV/材质的网格：

- `Body`：身体与服装，使用 `cloth.png`
- `Hair`：头发，使用 `hair.png`
- `Face`：面部，使用 `face.png`

为了缩小调试范围，当前版本未保留表情网格、骨骼、蒙皮、形态键和动画数据。

## 当前功能

- Vulkan 1.3 与 Dynamic Rendering
- 前向 PBR 渲染路径
- 延迟渲染路径与 G-Buffer
- 天空盒与基于图像的光照（IBL）
  - Irradiance Map
  - Prefiltered Specular Map
  - BRDF LUT
- Assimp 模型导入与 stb_image 图片加载
- 基于 Taskflow 的异步资源加载
- SPIR-V 反射驱动的材质参数与描述符绑定
- ImGui 场景检查器
  - 渲染管线切换
  - Render Texture/G-Buffer 调试显示
  - PBR 参数调节
  - UV 模式、Scale、Offset 与 Repeat 调试
  - Object Transform 与投影 Y 翻转控制
- Fence 驱动的帧资源回收与延迟销毁

## 项目结构

```text
TasrovyRenderer/
├─ src/
│  ├─ base/             数学类型与基础工具
│  ├─ render/           场景、对象、材质、网格与渲染管线抽象
│  ├─ RHI/              API 无关的渲染硬件接口
│  │  └─ Vulkan/        Vulkan 后端实现
│  ├─ filesystem/       模型、图片和动画资源加载
│  ├─ ui/               ImGui 调试界面
│  ├─ window/           窗口与输入
│  ├─ log/              日志系统
│  └─ core/             程序入口与示例场景
├─ res/
│  ├─ Models/
│  │  └─ Taffy/         当前角色模型及 MTL
│  ├─ Textures/
│  │  └─ Taffy/         当前角色基础色贴图
│  ├─ Skyboxes/         天空盒图片
│  ├─ Shaders/
│  │  ├─ Source/        HLSL/GLSL 源文件
│  │  ├─ Bin/           编译后的 SPIR-V
│  │  └─ IBL/           IBL 着色器源码与二进制
│  └─ Scripts/          着色器编译与资源辅助脚本
└─ docs/                补充文档
```

## 依赖

- Windows
- CMake 3.15 或更高版本
- 支持 C++20 的编译器
- Vulkan SDK
- DirectX Shader Compiler（DXC）
- vcpkg

第三方依赖由根目录的 `vcpkg.json` 管理，主要包括：

- Vulkan
- GLFW
- GLM
- Assimp
- ImGui
- Taskflow
- spdlog
- stb
- SPIRV-Cross

## 构建与运行

在项目根目录执行：

```powershell
cmake -B cmake-build-debug -DCMAKE_TOOLCHAIN_FILE=C:/Libraries/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build cmake-build-debug --config Debug
```

运行调试版本：

```powershell
.\cmake-build-debug\src\core\TasrovyCore.exe
```

构建 Release 版本：

```powershell
cmake --build cmake-build-debug --config Release
```

## 编译着色器

普通渲染着色器和 IBL 着色器分别由以下脚本编译：

```powershell
.\res\Scripts\compile.bat
.\res\Scripts\compile_ibl.bat
```

脚本通过 DXC 将 HLSL 编译为 Vulkan 使用的 SPIR-V，并将结果写入 `res/Shaders` 对应的 `Bin` 目录。

## 当前模型资源位置

```text
res/Models/Taffy/Taffy.obj
res/Models/Taffy/Taffy.mtl
res/Textures/Taffy/cloth.png
res/Textures/Taffy/hair.png
res/Textures/Taffy/face.png
```

当前 OBJ 按面角展开顶点，不执行顶点去重，以完整保留 UV 接缝和材质边界。模型包含三个子网格，不包含骨骼和动画。
