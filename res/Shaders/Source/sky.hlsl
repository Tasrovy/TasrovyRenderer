// UBO 只需要 View 和 Proj 矩阵
cbuffer SkyboxUBO : register(b0)
{
    matrix view;
    matrix proj;
};

// 顶点着色器输入只有一个位置
struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD0; // 传递给PS的采样方向
};

// 顶点着色器
VSOutput VSMain(VSInput input)
{
    VSOutput output;

    // 关键技巧：移除 view 矩阵的平移部分
    // 这会让立方体始终以相机为中心，看起来无穷大
    matrix viewNoTranslation = view;
    viewNoTranslation[3][0] = 0; // x
    viewNoTranslation[3][1] = 0; // y
    viewNoTranslation[3][2] = 0; // z
    
    // 应用旋转和投影
    float4 pos = mul(float4(input.position, 1.0f), viewNoTranslation);
    output.position = mul(pos, proj);

    // 将 z 分量强制设为 w 分量。
    // 这会使顶点的深度值在经过透视除法后始终为 1.0 (最远)
    // 确保天空盒总是在所有其他物体的后面
    output.position.z = output.position.w;

    // 将顶点位置作为采样方向传递给片段着色器
    output.texcoord = input.position;
    
    return output;
}

// 立方体贴图和采样器
TextureCube skyboxTexture : register(t1); // <-- 使用 TextureCube 类型
SamplerState skyboxSampler : register(s1);

// 片段着色器
float4 PSMain(VSOutput input) : SV_TARGET
{
    // 使用从VS传递过来的方向，在立方体贴图上采样
    return skyboxTexture.Sample(skyboxSampler, normalize(input.texcoord));
}