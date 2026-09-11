# Material Techniques

`MaterialTechnique` separates a material instance's parameters from the
shaders used by each rendering stage. A material descriptor references one
technique asset:

```json
{
  "name": "ExampleMaterial",
  "technique": "res/MaterialTechniques/Character.technique.json",
  "properties": []
}
```

A technique classifies the material and maps pipeline slots to graphics shader
pairs:

```json
{
  "name": "Character",
  "category": "character",
  "variants": {
    "gbuffer": {
      "vertex": {
        "source": "res/Shaders/Source/character_gbuffer.hlsl",
        "entry": "VSMain"
      },
      "fragment": {
        "source": "res/Shaders/Source/character_gbuffer.hlsl",
        "entry": "PSMain",
        "permutation": 0
      }
    }
  }
}
```

Supported categories are `scene`, `vegetation`, `character`, and `special`.
Standard slots are `shadow`, `depth`, `gbuffer`, `lighting`, `forward`, and
`transparent`. A pipeline may also define a custom slot name.

Resolution order for a mesh draw is:

1. pipeline pass default VS/PS;
2. the material technique shader for the pass slot;
3. explicit shaders assigned directly to the material.

All variants used in one pass share that pass's vertex layout, descriptor-set
layout, attachment formats, depth state, culling, and blending. A variant may
omit one shader stage to inherit it from the pass. The current four technique
assets intentionally point to the existing shaders; they are compatibility
anchors that can be replaced slot by slot while porting the captured pipeline.
