#pragma once

struct mzDxContext;
struct mzDxResource;
struct mzDxPipelineState;

mzDxContext* mzCreateDxContext(HWND Window);
void mzDestroyDxContext(mzDxContext* Dx);
ID3D12GraphicsCommandList* mzBeginFrame(mzDxContext* Dx);
void mzEndFrame(mzDxContext* Dx, u32 SwapInterval);
void mzWaitForGpu(mzDxContext* Dx);

D3D12_CPU_DESCRIPTOR_HANDLE mzAllocateCpuDescriptors(mzDxContext* Dx, D3D12_DESCRIPTOR_HEAP_TYPE Type, u32 Num);
void mzAllocateGpuDescriptors(mzDxContext* Dx, u32 Num, D3D12_CPU_DESCRIPTOR_HANDLE* OutCpu, D3D12_GPU_DESCRIPTOR_HANDLE* OutGpu);
D3D12_GPU_DESCRIPTOR_HANDLE mzCopyDescriptorsToGpuHeap(mzDxContext* Dx, u32 Num, D3D12_CPU_DESCRIPTOR_HANDLE SrcBaseHandle);
void mzAllocateUploadBufferRegion(mzDxContext* Dx, u32 Size, u8** OutCpuAddr, ID3D12Resource** OutBuffer, u64* OutBufferOffset);

mzDxResource* mzCreateCommittedResource(
	mzDxContext* Dx, D3D12_HEAP_TYPE HeapType, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* Desc,
	D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* ClearValue);
u32 mzReleaseResource(mzDxContext* Dx, mzDxResource* InResource);
ID3D12Resource* mzGetRawResource(mzDxContext* Dx, mzDxResource* Resource);

mzDxPipelineState* mzCreateGraphicsPipeline(mzDxContext* Dx, D3D12_GRAPHICS_PIPELINE_STATE_DESC* PsoDesc, const char* VsName, const char* PsName);
mzDxPipelineState* mzCreateGraphicsPipeline(
	mzDxContext* Dx,
	D3D12_GRAPHICS_PIPELINE_STATE_DESC* PsoDesc,
	const std::vector<u8>* VsBytecode,
	const std::vector<u8>* PsBytecode);
mzDxPipelineState* mzCreateComputePipeline(mzDxContext* Dx, D3D12_COMPUTE_PIPELINE_STATE_DESC* PsoDesc, const char* CsName);
mzDxPipelineState* mzCreateComputePipeline(mzDxContext* Dx, D3D12_COMPUTE_PIPELINE_STATE_DESC* PsoDesc, const std::vector<u8>* CsBytecode);
u32 mzReleasePipeline(mzDxContext* Dx, mzDxPipelineState* Pipeline);

void mzGetBackBuffer(mzDxContext* Dx, mzDxResource** OutResource, D3D12_CPU_DESCRIPTOR_HANDLE* OutRtv);
ID3D12Device* mzGetDevice(mzDxContext* Dx);
ID3D12GraphicsCommandList* mzGetCommandList(mzDxContext* Dx);
ID3D12CommandQueue* mzGetCommandQueue(mzDxContext* Dx);
u32 mzGetFrameIndex(mzDxContext* Dx);

void mzEncodeTransitionBarrier(mzDxContext* Dx, mzDxResource* Resource, D3D12_RESOURCE_STATES StateAfter);
void mzEncodeSetPipelineState(mzDxContext* Dx, mzDxPipelineState* Pipeline);
