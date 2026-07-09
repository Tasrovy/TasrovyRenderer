// ====================================================================
//    IBL Specular Prefilter - Compute Shader (HLSL)
// ====================================================================

// 输入: 原始的 HDR 环境立方体图
[[vk::combinedImageSampler]] TextureCube  environmentMap     : register(t0);
[[vk::combinedImageSampler]] SamplerState environmentSampler : register(s0);

// 输出: 预过滤图的某一个 Mip Level (作为 Storage Image 写入)
RWTexture2DArray<half4> prefilteredMap : register(u1);

// 通过 Push Constants 接收的参数
struct PushConstants
{
    float roughness;
};
[[vk::push_constant]] PushConstants pc;


#define PI 3.14159265359
#define SAMPLE_COUNT 128u

float RadicalInverse_VdC(uint bits) 
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
float2 Hammersley(uint i, uint N)
{
    return float2(float(i)/float(N), RadicalInverse_VdC(i));
}
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
	float a = roughness*roughness;
	
	float phi = 2.0 * PI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
	
	float3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;
	
	float3 up        = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
	float3 tangent   = normalize(cross(up, N));
	float3 bitangent = cross(N, tangent);
	
	float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
	return normalize(sampleVec);
}
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;
    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float2 IntegrateBRDF(float NdotV, float roughness)
{
    float3 V;
    V.x = sqrt(1.0 - NdotV*NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    float3 N = float3(0.0, 0.0, 1.0);

    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if(NdotL > 0.0)
        {
            float G = GeometrySmith(N, V, L, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return float2(A, B);
}

// ----------------------------------------------------------------------------
[numthreads(32, 32, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height, numFaces;
    prefilteredMap.GetDimensions(width, height, numFaces);

    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height) {
        return;
    }

    // 将像素坐标转换为 3D 采样方向 (这个方向是我们的观察向量 V)
    float2 uv = (float2(dispatchThreadID.xy) + 0.5f) / float2(width, height);
    uv = uv * 2.0f - 1.0f;
    
    float3 V; // 也就是 R (反射向量)，因为我们假设 V = N = R
    switch(dispatchThreadID.z)
    {
        case 0: V = normalize(float3( 1.0f, -uv.y, -uv.x)); break;
        case 1: V = normalize(float3(-1.0f, -uv.y,  uv.x)); break;
        case 2: V = normalize(float3( uv.x,  1.0f,  uv.y)); break;
        case 3: V = normalize(float3( uv.x, -1.0f, -uv.y)); break;
        case 4: V = normalize(float3( uv.x, -uv.y,  1.0f)); break;
        case 5: V = normalize(float3(-uv.x, -uv.y, -1.0)); break;
    }
    float3 N = V; // 对于环境贴图预过滤，我们假设法线和观察方向相同
    
    float3 prefilteredColor = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;
    
    // --- 蒙特卡洛重要性采样 ---
    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, pc.roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V); // 反射向量

        float NdotL = max(dot(N, L), 0.0);
        if(NdotL > 0.0)
        {
            prefilteredColor += environmentMap.SampleLevel(environmentSampler, L, 0).rgb * NdotL;
            totalWeight      += NdotL;
        }
    }
    
    prefilteredColor = prefilteredColor / totalWeight;

    prefilteredMap[dispatchThreadID] = half4(prefilteredColor, 1.0);
}