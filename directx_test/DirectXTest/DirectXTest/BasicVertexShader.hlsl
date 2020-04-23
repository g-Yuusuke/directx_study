struct VSOutput {
	float4 pos : POSITION;
	float4 svpos : SV_POSITION;
};

VSOutput BasicVS(float4 pos : POSITION)
{
	VSOutput output;
	output.pos = pos;
	output.svpos = pos;
	return output;
}