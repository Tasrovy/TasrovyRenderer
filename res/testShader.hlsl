cbuffer UBO : register(b0,space0)
{
    matrix model;
    matrix view;
    matrix proj;
    float3 lightDir; // 世界空间中的光照方向
    float _pad0; // 填充
    float3 lightColor; // 光的颜色
    float lightIntensity; // 光的强度
    float3 camPos; // 世界空间中的相机位置
    float uMetallic;
    float uRoughness;
    float uAo;
};
struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
    float2 texcoord : TEXCOORD;
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
    float3 albedo = albedoMap.Sample(pbrSampler, input.texcoord).rgb; // sRGB -> Linear
    float metallic = mraMap.Sample(pbrSampler3, input.texcoord).r*uMetallic; // 假设 glTF: B=Metallic
    float roughness = 1-mraMap.Sample(pbrSampler3, input.texcoord).a*uRoughness; // G=Roughness
    float ao = mraMap.Sample(pbrSampler3, input.texcoord).b*uAo;       // R=AO
    
    // --- 2. 获取世界空间法线 (来自法线贴图) ---
    float3 tangentNormal = normalMap.Sample(pbrSampler1, input.texcoord).xyz * 2.0 - 1.0;
    float3 N = normalize(mul(tangentNormal, input.TBN));

    // --- 3. 准备通用向量 ---
    float3 V = normalize(camPos - input.worldPos); // 观察方向
    float3 R = reflect(-V, N);                     // 反射方向
    float NdotV = max(dot(N, V), 0.0);

    // --- 4. 计算 F0 (0度入射角的菲涅尔反射率) ---
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    
    // =================================================================
    //  直接光照 (Direct Lighting) - 假设只有一个定向光
    // =================================================================
    float3 L = normalize(lightDir);
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
    float3 Lo_direct = (diffuse_direct + (specular_direct * mraMap.Sample(pbrSampler3, input.texcoord).g)) * radiance;

    // =================================================================
    //  间接光照 (Indirect Lighting - IBL)
    // =================================================================

    // a. Indirect Specular
    const float MAX_REFLECTION_LOD = 7.0;
    float3 prefilteredColor = prefilteredMap.SampleLevel(iblSampler2, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdf = brdfLUT.Sample(iblSampler3, float2(NdotV, roughness)).rg;
    float3 specular_IBL = prefilteredColor * (F * brdf.x + brdf.y) * mraMap.Sample(pbrSampler3, input.texcoord).g;

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
    color += emissiveMap.Sample(pbrSampler2, input.texcoord).rgb;
    
    // HDR 色调映射 (Reinhard)
    color = color / (color + float3(1.0, 1.0, 1.0));
    
    return float4(color, 1.0);
}