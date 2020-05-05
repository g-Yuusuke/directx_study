#include "BasicShaderHeader.hlsli"

float4 BasicPS(VSOutput input) : SV_TARGET
{
	return float4(tex.Sample(samp, input.uv));
}