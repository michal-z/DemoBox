#define _mzRootSignature "RootFlags(0)"

struct _mzVertexInput
{
	uint VertexIdx : SV_VertexId;
};

struct _mzVertexOutput
{
	float4 Position : SV_Position;
	float3 Color : _Color;
};

[RootSignature(_mzRootSignature)]
_mzVertexOutput _mzMainVs(_mzVertexInput Input)
{
	float2 Positions[3] = { float2(-1.0f, -1.0f), float2(0.0f, 1.0f), float2(1.0f, -1.0f) };
	_mzVertexOutput Output;
	Output.Position = float4(Positions[Input.VertexIdx], 0.0f, 1.0f);
	Output.Color = float3(0.0f, 0.5f, 0.0f);

	return Output;
}

[RootSignature(_mzRootSignature)]
float4 _mzMainPs(_mzVertexOutput Input) : SV_Target0
{
    return float4(Input.Color, 1.0f);
}
