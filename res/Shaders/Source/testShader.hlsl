cbuffer UBO : register(b0,space0)
{
    matrix model;
    matrix view;
    matrix proj;
    float3 lightDir; // 世界空间中的光照方向
    float _pad0; // 填充
    float3 lightColor; // 光的颜色
    float lightIntensity; // 光的强度
    float4 camPosAndMetallic; // xyz: camera position, w: metallic multiplier
    float4 roughnessAo; // x: roughness multiplier, y: ao multiplier
    float4 uvTransform; // xy: scale, zw: offset
};
struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal   : NORMAL;
    [[vk::location(2)]] float3 tangent : TANGENT;
    [[vk::location(3)]] float3 bitangent : BITANGENT;
    [[vk::location(4)]] float2 texcoord : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3x3 TBN : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    
    // 计算世界空间位置
    output.worldPos = mul(float4(input.position, 1.0f), model).xyz;
    float4 viewPos = mul(float4(output.worldPos, 1.0f), view);
    output.position = mul(viewPos, proj);
    
    
    output.texcoord = input.texcoord;
    output.TBN = float3x3(
        normalize(mul(float4(input.tangent, 0.0), model).xyz),
        normalize(mul(float4(input.bitangent, 0.0), model).xyz),
        normalize(mul(float4(input.normal, 0.0), model).xyz)
    );
    return output;
}
[[vk::combinedImageSampler]] Texture2D albedoMap : register(t1);
[[vk::combinedImageSampler]] Texture2D normalMap : register(t2);
[[vk::combinedImageSampler]] Texture2D emissiveMap : register(t3);
[[vk::combinedImageSampler]] Texture2D mraMap : register(t4);

[[vk::combinedImageSampler]] SamplerState pbrSampler : register(s1);
[[vk::combinedImageSampler]] SamplerState pbrSampler1 : register(s2);
[[vk::combinedImageSampler]] SamplerState pbrSampler2 : register(s3);
[[vk::combinedImageSampler]] SamplerState pbrSampler3 : register(s4);

[[vk::combinedImageSampler]] TextureCube  irradianceMap : register(t5); // 辐照度图 (漫反射)
[[vk::combinedImageSampler]] TextureCube  prefilteredMap: register(t6); // 预过滤镜面反射图 (高光)
[[vk::combinedImageSampler]] Texture2D    brdfLUT       : register(t7); // BRDF 查找表
[[vk::combinedImageSampler]] SamplerState iblSampler1      : register(s5); 
[[vk::combinedImageSampler]] SamplerState iblSampler2     : register(s6); 
[[vk::combinedImageSampler]] SamplerState iblSampler3      : register(s7); 
float2 ResolveMaterialUV(float2 uv)
{
    uint mode = (uint)round(roughnessAo.w);
    float2 orientedUv = uv;
    if (mode == 1) {
        orientedUv = float2(uv.x, 1.0f - uv.y);
    }
    else if (mode == 2) {
        orientedUv = float2(1.0f - uv.x, uv.y);
    }
    else if (mode == 3) {
        orientedUv = float2(1.0f - uv.x, 1.0f - uv.y);
    }
    else if (mode == 4) {
        orientedUv = float2(uv.y, uv.x);
    }
    else if (mode == 5) {
        orientedUv = float2(uv.y, 1.0f - uv.x);
    }
    else if (mode == 6) {
        orientedUv = float2(1.0f - uv.y, uv.x);
    }
    return frac(orientedUv * uvTransform.xy + uvTransform.zw);
}
// --- PBR 辅助函数 (Cook-Torrance BRDF) ---
#define PI 3.14159265359

// D - 正态分布函数 (Trowbridge-Reitz GGX)
// 描述了微观表面法线的朝向分布情况
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

// G - 几何遮蔽函数 (Schlick-GGX)
// 描述了微观表面自遮蔽的属性（光线被微观表面自身的凹凸遮挡）
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / denom;
}
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// F - 菲涅尔方程 (Schlick 近似)
// 描述了在不同角度下，表面反射光线所占的比率
float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}


// --- 片段着色器主函数 ---
float4 PSMain(VSOutput input) : SV_TARGET
{
    // --- 1. 获取表面基础属性 ---
    float2 materialUv = ResolveMaterialUV(input.texcoord);
    float3 albedo = albedoMap.Sample(pbrSampler, materialUv).rgb; // sRGB -> Linear
    if ((uint)round(roughnessAo.z) == 1) {
        return float4(albedo, 1.0f);
    }
    float4 mra = mraMap.Sample(pbrSampler3, materialUv);
    float metallic = saturate(mra.r * camPosAndMetallic.w);
    float roughness = saturate((1.0f - mra.a) * roughnessAo.x);
    float ao = saturate(mra.b * roughnessAo.y);
    
    // --- 2. 获取世界空间法线 (来自法线贴图) ---
    float3 tangentNormal = normalMap.Sample(pbrSampler1, materialUv).xyz * 2.0 - 1.0;
    float3 N = normalize(mul(tangentNormal, input.TBN));

    // --- 3. 准备通用向量 ---
    float3 V = normalize(camPosAndMetallic.xyz - input.worldPos); // 观察方向
    float3 R = reflect(-V, N);                     // 反射方向
    float NdotV = max(dot(N, V), 0.0);

    // --- 4. 计算 F0 (0度入射角的菲涅尔反射率) ---
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    
    // =================================================================
    //  直接光照 (Direct Lighting) - 假设只有一个定向光
    // =================================================================
    float3 L = normalize(-lightDir);
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    
    // 计算直接光的辐射度 (Radiance)
    float3 radiance = lightColor * lightIntensity * NdotL;
    
    // Cook-Torrance BRDF for direct light
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    
    float3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.001;
    float3 specular_direct = numerator / denominator;
    
    float3 kS_direct = F;
    float3 kD_direct = float3(1.0, 1.0, 1.0) - kS_direct;
    kD_direct *= (1.0 - metallic);
    
    float3 diffuse_direct = kD_direct * albedo / PI;

    // 直接光照贡献
    float3 Lo_direct = (diffuse_direct + (specular_direct * mraMap.Sample(pbrSampler3, materialUv).g)) * radiance;

    // =================================================================
    //  间接光照 (Indirect Lighting - IBL)
    // =================================================================

    // a. Indirect Specular
    const float MAX_REFLECTION_LOD = 7.0;
    float3 prefilteredColor = prefilteredMap.SampleLevel(iblSampler2, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdf = brdfLUT.Sample(iblSampler3, float2(NdotV, roughness)).rg;
    float3 specular_IBL = prefilteredColor * (F * brdf.x + brdf.y) * mraMap.Sample(pbrSampler3, materialUv).g;

    // b. Indirect Diffuse
    float3 irradiance = irradianceMap.Sample(iblSampler1, N).rgb;
    float3 kS_indirect = fresnelSchlick(NdotV, F0);
    float3 kD_indirect = float3(1.0, 1.0, 1.0) - kS_indirect;
    kD_indirect *= (1.0 - metallic);
    float3 diffuse_IBL = kD_indirect * irradiance * albedo;

    float3 Lo_indirect = diffuse_IBL + specular_IBL;
    
    // =================================================================
    //  最终组合
    // =================================================================
    
    // 最终颜色 = (直接光照 + 间接光照) * AO + 自发光
    float3 color = (Lo_indirect * ao) + Lo_direct;
    
    // 添加自发光
    color += emissiveMap.Sample(pbrSampler2, materialUv).rgb;
    
    return float4(saturate(color), 1.0);
}



