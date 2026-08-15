# GPUScene 与 Uniform 数据拆分

## 目标

旧路径为每个 Draw 生成一份约 1600 字节的 `FrameUniformBuffer`，其中重复包含 Camera、Object、Material、Light 和后处理参数。Draw 数量增加时，CPU 内存复制、Uniform Buffer 数量和描述符更新都会线性增长。

新路径将数据按更新频率和所有权拆开。

| Binding | 数据 | 更新粒度 | 用途 |
|---|---|---|---|
| `b0` | Pass Constants | 每 Pass、每帧 | 当前算法独有参数 |
| `b20` | `ViewUniform` | 每 View、每帧 | 当前/历史 View、投影、Camera、分辨率、Jitter |
| `t21` | `ObjectData` SSBO | 每 Render Primitive、每帧 | Model、Previous Model、Material Index、投影翻转标志 |
| `t22` | `MaterialData` SSBO | Material 数据更新时 | PBR 标量、颜色、描边和每纹理 UV 采样参数 |
| `t23` | Scene Light SSBO | Scene Light 更新时 | 灯光元数据、主光源和灯光数组 |

## 稳定 objectIndex

`Object` 创建时获得单调递增的 `renderId`。不可变 Scene Snapshot 克隆对象时保留该 ID，因此渲染线程不能再因对象地址变化而得到不同身份。

真正用于 GPU 的 `objectIndex` 以 `(renderId, submeshIndex)` 为稳定键分配。这里的 ObjectData 实际对应 Render Primitive：同一 Object 的多个 submesh 可以绑定不同的 `materialIndex`，同时共享相同的 Model/Previous Model 内容。

`FrameDrawPacket` 不再保存 Model、Previous Model、Material ID 或 per-draw uniform 字节，只保留：

- Mesh ID；
- `objectIndex` 与 `materialIndex`；
- submesh 和 Index Buffer 范围；
- Front Face 所需的投影翻转标志。

执行器把 `objectIndex` 写入 indexed draw 的 `firstInstance`，HLSL 顶点入口通过 `SV_InstanceID` 读取 `ObjectData`。

## Vulkan 描述符与上传

GPUScene 为每个 in-flight frame 保存一组 View/Object/Material/Light Buffer。更新发生在该 frame fence 已等待完成之后，因此 CPU 不会覆盖仍被 GPU 使用的数据。

不同材质纹理目前仍可使用不同 Descriptor Set，但同一 Pass 的所有 Draw Set 会引用同一个 per-frame Pass Constant Buffer。Camera 和 Pass 数据不再按 Draw 次数复制。

UV 方向不再由子网格名称决定。每个 `TextureBinding` 独立保存方向、缩放和偏移，
因此同一材质的 Base Color、Normal、Emissive 与 MRA 贴图可以使用不同的采样变换。
材质描述文件同时兼容原来的字符串写法和对象写法，例如：

```json
{
  "name": "baseColorTexture",
  "type": "texture2D",
  "value": {
    "path": "res/Textures/Taffy/face.png",
    "uvMode": "flipY",
    "scale": [1.0, 1.0],
    "offset": [0.0, 0.0]
  }
}
```

支持的 `uvMode` 为 `identity`、`flipY`、`flipX`、`flipXY`、`swapXY`、
`swapXYFlipY` 和 `swapXYFlipX`。未指定时使用 `identity`。

## 独立 Pass Constants

- TAA/TAAU：`TemporalPassConstants`，16 字节；
- Bloom：`BloomPassConstants`，16 字节；
- SSAO/HBAO：`SsaoPassConstants`，16 字节；
- Lighting：`LightingPassConstants`，480 字节；
- Shadow：`ShadowPassConstants`，192 字节。

GBuffer 与 Transparent 不再需要 b0；它们直接消费 View/Object/Material/Light 数据。

## 场景更新与 RenderGraph 重建

旧 `processScene()` 已拆成两个职责：

- `applySceneUpdates()`：应用 Mesh、Material Texture、Skybox 等场景 GPU 资源更新；
- `rebuildRenderGraph()`：重新生成 Pipeline、RenderGraph、FramePacket 和 RHI Execution Plan，在重建边界等待 in-flight frame，并重新编译图资源和 GPUScene 容量。

当前结构性 Scene dirty 仍会触发 `rebuildRenderGraph()`；普通每帧 Transform、Camera、Material 标量和 Light 数据由 GPUScene 的 per-frame update 上传，不需要生成 per-draw UBO。
