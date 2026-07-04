
TextureCube environmentMap : register(t0);
SamplerState environmentSampler : register(s0);

RWTexture2DArray<float4> irradianceMap : register(u1);


#define PI 3.14159265359

[numthreads(32, 32, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	uint width, height, numFaces;
	irradianceMap.GetDimensions(width, height, numFaces);
	
	if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
	{
		return;
	}
	
	float2 uv = (float2(dispatchThreadID.xy) + 0.5f) / float2(width, height);
	uv = uv * 2.0f - 1.0f;
    
	float3 N;
	switch (dispatchThreadID.z)
	{
		case 0:
			N = normalize(float3(1.0f, -uv.y, -uv.x));
			break; // +X
		case 1:
			N = normalize(float3(-1.0f, -uv.y, uv.x));
			break; // -X
		case 2:
			N = normalize(float3(uv.x, 1.0f, uv.y));
			break; // +Y
		case 3:
			N = normalize(float3(uv.x, -1.0f, -uv.y));
			break; // -Y
		case 4:
			N = normalize(float3(uv.x, -uv.y, 1.0f));
			break; // +Z
		case 5:
			N = normalize(float3(-uv.x, -uv.y, -1.0));
			break; // -Z
	}
    
	float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    
	float3 up = abs(N.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
	float3 right = normalize(cross(up, N));
	float3 tangent = cross(N, right);

	float sampleDelta = 0.025f;
	float nrSamples = 0.0f;
    
	for (float phi = 0.0f; phi < 2.0f * PI; phi += sampleDelta)
	{
		for (float theta = 0.0f; theta < 0.5f * PI; theta += sampleDelta)
		{
			float3 tempVec = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
			float3 sampleVec = tempVec.x * right + tempVec.y * tangent + tempVec.z * N;
            
			irradiance += environmentMap.SampleLevel(environmentSampler, sampleVec, 0).rgb * cos(theta) * sin(theta);
			nrSamples++;
		}
	}
	irradiance = PI * irradiance / nrSamples;
	
	irradianceMap[dispatchThreadID] = float4(irradiance, 1.0);
}