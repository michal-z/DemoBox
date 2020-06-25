#define _mzRootSignature \
    "DescriptorTable(UAV(u0))"

RWTexture2D<float4> _uavImage : register(u0);

[RootSignature(_mzRootSignature)]
[numthreads(8, 8, 1)]
void _mzMainCs(uint3 GlobalId : SV_DispatchThreadId)
{
	float2 Resolution;
	_uavImage.GetDimensions(Resolution.x, Resolution.y);

	float2 P = GlobalId.xy / Resolution;
	_uavImage[GlobalId.xy] = float4(P, 0.0f, 1.0f);
}
