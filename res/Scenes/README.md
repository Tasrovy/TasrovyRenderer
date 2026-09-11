# Scenes

TasrovyRenderer discovers scene files recursively under `res` when their file
name ends with `.scene.json`. Invalid or unsupported scene files are excluded
from the startup catalog.

When the catalog contains multiple scenes, the executable prints their scene
names and paths and waits for a selection before creating the renderer. A scene
can also be selected directly:

```text
TasrovyCore.exe res/Scenes/CornellTaffy.scene.json
TasrovyCore.exe CornellBox
```

Scene files declare cameras, lights, primitive or imported-model objects,
transforms, and material descriptor references. Runtime construction must not
be added to `main.cpp`; add new content as another `.scene.json` file instead.

Material fields accept either a descriptor path:

```json
"material": "res/Materials/Cornell/Floor.material.json"
```

or an object containing `descriptor` plus serialized parameter overrides.
Imported models can assign materials per submesh with `submeshMaterials`.
