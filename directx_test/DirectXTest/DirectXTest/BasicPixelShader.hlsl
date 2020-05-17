#include "BasicShaderHeader.hlsli"

float4 BasicPS(VSOutput input) : SV_TARGET
{
	float light = normalize(float3(1, -1, 1));
	float brightness = dot(-light, input.normal);
	return float4(brightness, brightness, brightness, 1);
}