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

	mzEncodeSetPipelineState(Dx, Demo->Pso);

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
		mzShaderCompiler* Compiler = mzCreateShaderCompiler();
		std::vector<u8> Bytecode = mzCompileShaderFromFile(Compiler, "Data/Shaders/TestCs.hlsl", "_mzMainCs", "cs_6_0");
		mzDestroyShaderCompiler(Compiler);

		D3D12_COMPUTE_PIPELINE_STATE_DESC PsoDesc = {};
		Demo->Pso = mzCreateComputePipeline(Dx, &PsoDesc, &Bytecode);
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
	mzDestroyDxContext(Demo->Dx);
}
