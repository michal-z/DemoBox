#include "Pch.h"
#include "Graphics.h"
#include "Library.h"

#define _mzWindowName "Demo01"
#define _mzWindowWidth 1024
#define _mzWindowHeight 1024

struct mzDemo01
{
	f64 FrameTime;
	f32 FrameDeltaTime;
	HWND Window;
	mzDxContext* Dx;
	mzDxPipelineState* Pso;
};

void mzUpdate(mzDemo01* Demo)
{
	assert(Demo);

	mzDxContext* Dx = Demo->Dx;
	
	mzUpdateFrameStats(Demo->Window, _mzWindowName, &Demo->FrameTime, &Demo->FrameDeltaTime);

	ID3D12GraphicsCommandList* CmdList = mzBeginFrame(Dx);

	mzDxResource* BackBuffer;
	D3D12_CPU_DESCRIPTOR_HANDLE BackBufferRtv;
	mzGetBackBuffer(Dx, &BackBuffer, &BackBufferRtv);
	mzEncodeTransitionBarrier(Dx, BackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);

	CmdList->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, _mzWindowWidth, _mzWindowHeight));
	CmdList->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, _mzWindowWidth, _mzWindowHeight));
	CmdList->ClearRenderTargetView(BackBufferRtv, XMVECTORF32{ 0.2f, 0.4f, 0.8f, 1.0f }, 0, nullptr);
	CmdList->OMSetRenderTargets(1, &BackBufferRtv, TRUE, nullptr);

	mzEncodeSetPipelineState(Dx, Demo->Pso);

	CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	CmdList->DrawInstanced(3, 1, 0, 0);

	mzEncodeTransitionBarrier(Dx, BackBuffer, D3D12_RESOURCE_STATE_PRESENT);
	mzEndFrame(Dx, 0);
}

bool mzInit(mzDemo01** OutDemo)
{
	assert(OutDemo);

	static mzDemo01 DemoStorage;
	mzDemo01* Demo = &DemoStorage;
	*OutDemo = Demo;

	Demo->Window = mzCreateWindow(_mzWindowName, _mzWindowWidth, _mzWindowHeight);
	Demo->Dx = mzCreateDxContext(Demo->Window);

	mzDxContext* Dx = Demo->Dx;

	// Test pipeline.
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC PsoDesc = {};
		PsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		PsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		PsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		PsoDesc.DepthStencilState.DepthEnable = FALSE;
		PsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		PsoDesc.NumRenderTargets = 1;
		PsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		PsoDesc.SampleMask = UINT32_MAX;
		PsoDesc.SampleDesc.Count = 1;
		Demo->Pso = mzCreateGraphicsPipeline(Dx, &PsoDesc, "TestVsPs.vs.cso", "TestVsPs.ps.cso");
	}

	ID3D12GraphicsCommandList* CmdList = mzBeginFrame(Dx);

	// ...

	CmdList->Close();
	mzGetCommandQueue(Dx)->ExecuteCommandLists(1, (ID3D12CommandList**)&CmdList);
	mzWaitForGpu(Dx);

	return true;
}

void mzDeinit(mzDemo01* Demo)
{
	assert(Demo);

	mzWaitForGpu(Demo->Dx);
	mzReleasePipeline(Demo->Dx, Demo->Pso);
	mzDestroy(Demo->Dx);
}
