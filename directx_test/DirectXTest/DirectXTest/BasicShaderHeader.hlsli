struct VSOutput
{
	float4 svpos : SV_POSITION;
	float4 normal : NORMAL;
	float2 uv : TEXCOORD;
};

SamplerState samp : register(s0);
cbuffer cbuff0 :register(b0)
{
	matrix world;
	matrix view_proj;
};

cbuffer mat :register(b1)
{
	float4 diffuse;
	float4 specular;
	float3 ambient;
};
