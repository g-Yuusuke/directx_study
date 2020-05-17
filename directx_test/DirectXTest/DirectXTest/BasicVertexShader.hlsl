#include "BasicShaderHeader.hlsli"

VSOutput BasicVS(
	float4 pos : POSITION, 
	float4 normal : NORMAL,
	float2 uv : TEXCOORD,
	min16uint2 bone_no : BONE_NO,
	min16uint weight : WEIGHT,
	min16uint edge_flag : EDGE_FLAG)
{
	VSOutput output;
	output.svpos = mul(mul(view_proj, world), pos);
	normal.w = 0;
	output.normal = mul(world, normal);
	output.uv = uv;
	return output;
};