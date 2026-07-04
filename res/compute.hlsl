cbuffer UBO : register(b0, space0)
{
    float deltaTime;
};

struct Particle
{
    float3 position; float pad1;
    float3 velocity; float pad2;
    float4 color;
};

StructuredBuffer<Particle> inputParticles  : register(t1, space0);
RWStructuredBuffer<Particle> outputParticles : register(u2, space0);

[numthreads(64, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;

    Particle p = inputParticles[idx];

    // 根据速度更新位置
    p.position += p.velocity * deltaTime;
    //p.position += p.velocity * 0.1f;

    // 用速度大小映射到颜色 (简单版：长度 -> RGB 映射)
    float speed = length(p.velocity);

    // 把速度映射到 [0,1] 范围 (自己调 scale)
    float t = saturate(speed * 0.1f);

    // 颜色渐变: 蓝色(慢) -> 红色(快)
    p.color = lerp(float4(0,0,1,1), float4(1,0,0,1), t);

    outputParticles[idx] = p;
}