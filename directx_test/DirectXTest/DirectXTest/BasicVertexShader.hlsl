#include "BasicShaderHeader.hlsli"

VSOutput BasicVS(float4 pos : POSITION, float2 uv : TEXCOORD)
{
	VSOutput output;
	output.svpos = mul(mat,pos);
	output.uv = uv;
	return output;
};