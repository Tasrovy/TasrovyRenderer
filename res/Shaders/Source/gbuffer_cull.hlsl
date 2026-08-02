struct FrameData
{
    matrix view;
    matrix proj;
    matrix previousView;
    matrix previousProj;
    float4 uvTransform;
    float4 taaParams;
    uint drawCount;
    uint3 padding;
};

struct DrawData
{
    matrix model;
    matrix previousModel;
    float4 baseColorFactorAndTexture;
    float4 materialParams;
    float4 materialEmission;
    float4 rimColorAndStrength;
    float4 rimParams;
    float4 worldBounds;
    uint indexCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

struct IndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

cbuffer FrameUBO : register(b0, space0) { FrameData frameData; }
StructuredBuffer<DrawData> draws : register(t1, space0);
RWStructuredBuffer<IndirectCommand> commands : register(u2, space0);

[numthreads(64, 1, 1)]
void CSMain(uint drawIndex : SV_DispatchThreadID)
{
    if (drawIndex >= frameData.drawCount) return;

    DrawData draw = draws[drawIndex];
    float4 viewPosition = mul(float4(draw.worldBounds.xyz, 1.0f), frameData.view);
    float4 clipPosition = mul(viewPosition, frameData.proj);
    float radius = max(draw.worldBounds.w, 0.0f);
    float clipRadiusX = radius * abs(frameData.proj[0][0]);
    float clipRadiusY = radius * abs(frameData.proj[1][1]);
    bool visible =
        clipPosition.w > 0.0f &&
        abs(clipPosition.x) <= clipPosition.w + clipRadiusX &&
        abs(clipPosition.y) <= clipPosition.w + clipRadiusY;

    IndirectCommand command;
    command.indexCount = draw.indexCount;
    command.instanceCount = visible ? 1 : 0;
    command.firstIndex = draw.firstIndex;
    command.vertexOffset = draw.vertexOffset;
    command.firstInstance = draw.firstInstance;
    commands[drawIndex] = command;
}
