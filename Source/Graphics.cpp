#include "Pch.h"
#include "Graphics.h"
#include "Library.h"
#include "meow_hash_x64_aesni.h"

#define _mzMaxNumResources 256
#define _mzMaxNumPipelines 128
#define _mzNumBufferedFrames 2
#define _mzNumSwapBuffers 4

struct _mzResourceInfo
{
	ID3D12Resource* Raw;
	D3D12_RESOURCE_STATES State;
	DXGI_FORMAT Format;
};

struct _mzResourcePool
{
	_mzResourceInfo* Resources;
	u32* Generations;
};

struct _mzPipelineInfo
{
	ID3D12PipelineState* Pso;
	ID3D12RootSignature* RootSignature;
	bool bIsCompute;
};

struct _mzPipelinePool
{
	_mzPipelineInfo* Pipelines;
	u32* Generations;
	std::unordered_map<u64, mzDxPipelineState*> Map;
};

struct _mzDescriptorHeap
{
	ID3D12DescriptorHeap* Heap;
	D3D12_CPU_DESCRIPTOR_HANDLE CpuStart;
	D3D12_GPU_DESCRIPTOR_HANDLE GpuStart;
	u32 Size;
	u32 Capacity;
	u32 DescriptorSize;
};

struct _mzGpuMemoryHeap
{
	ID3D12Resource* Heap;
	u8* CpuStart;
	D3D12_GPU_VIRTUAL_ADDRESS GpuStart;
	u32 Size;
	u32 Capacity;
};

struct mzDxContext
{
	ID3D12Device* Device;
	ID3D12GraphicsCommandList* CmdList;
	ID3D12CommandQueue* CmdQueue;
	ID3D12CommandAllocator* CmdAlloc[_mzNumBufferedFrames];
	u32 Resolution[2];
	u32 FrameIndex;
	u32 BackBufferIndex;
	IDXGISwapChain3* SwapChain;
	mzDxResource* SwapBuffers[_mzNumSwapBuffers];
	_mzDescriptorHeap RtvHeap;
	_mzDescriptorHeap DsvHeap;
	_mzDescriptorHeap CbvSrvUavCpuHeap;
	_mzDescriptorHeap CbvSrvUavGpuHeaps[_mzNumBufferedFrames];
	_mzGpuMemoryHeap UploadMemoryHeaps[_mzNumBufferedFrames];
	_mzResourcePool ResourcePool;
	_mzPipelinePool PipelinePool;
	ID3D12Fence* FrameFence;
	HANDLE FrameFenceEvent;
	u64 NumFrames;
	HWND Window;
	mzDxPipelineState* CurrentPipeline;
};

static D3D12_CPU_DESCRIPTOR_HANDLE _mzAllocateCpuDescriptorsFromHeap(_mzDescriptorHeap* DescriptorHeap, u32 Num)
{
	assert(DescriptorHeap && (DescriptorHeap->Size + Num) < DescriptorHeap->Capacity);

	D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle;
	CpuHandle.ptr = DescriptorHeap->CpuStart.ptr + (u64)DescriptorHeap->Size * DescriptorHeap->DescriptorSize;
	DescriptorHeap->Size += Num;

	return CpuHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE mzAllocateCpuDescriptors(mzDxContext* Dx, D3D12_DESCRIPTOR_HEAP_TYPE Type, u32 Num)
{
	assert(Dx);
	if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
	{
		return _mzAllocateCpuDescriptorsFromHeap(&Dx->RtvHeap, Num);
	}
	if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
	{
		return _mzAllocateCpuDescriptorsFromHeap(&Dx->DsvHeap, Num);
	}
	if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
	{
		return _mzAllocateCpuDescriptorsFromHeap(&Dx->CbvSrvUavCpuHeap, Num);
	}
	assert(0);
	return D3D12_CPU_DESCRIPTOR_HANDLE{ 0 };
}

void mzAllocateGpuDescriptors(mzDxContext* Dx, u32 Num, D3D12_CPU_DESCRIPTOR_HANDLE* OutCpu, D3D12_GPU_DESCRIPTOR_HANDLE* OutGpu)
{
	assert(Dx && OutCpu && OutGpu && Num > 0);

	_mzDescriptorHeap* DescriptorHeap = &Dx->CbvSrvUavGpuHeaps[Dx->FrameIndex];

	assert((DescriptorHeap->Size + Num) < DescriptorHeap->Capacity);

	OutCpu->ptr = DescriptorHeap->CpuStart.ptr + (u64)DescriptorHeap->Size * DescriptorHeap->DescriptorSize;
	OutGpu->ptr = DescriptorHeap->GpuStart.ptr + (u64)DescriptorHeap->Size * DescriptorHeap->DescriptorSize;

	DescriptorHeap->Size += Num;
}

D3D12_GPU_DESCRIPTOR_HANDLE mzCopyDescriptorsToGpuHeap(mzDxContext* Dx, u32 Num, D3D12_CPU_DESCRIPTOR_HANDLE SrcBaseHandle)
{
	assert(Dx);

	D3D12_CPU_DESCRIPTOR_HANDLE CpuBaseHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE GpuBaseHandle;
	mzAllocateGpuDescriptors(Dx, Num, &CpuBaseHandle, &GpuBaseHandle);
	Dx->Device->CopyDescriptorsSimple(Num, CpuBaseHandle, SrcBaseHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	return GpuBaseHandle;
}

static void _mzAllocateGpuMemory(_mzGpuMemoryHeap* MemoryHeap, u32 Size, u8** OutCpuAddr, D3D12_GPU_VIRTUAL_ADDRESS* OutGpuAddr)
{
	assert(MemoryHeap && Size > 0 && OutCpuAddr && OutGpuAddr);

	if (Size & 0xff)
	{
		// Always align to 256 bytes.
		Size = (Size + 255) & ~0xff;
	}

	if ((MemoryHeap->Size + Size) >= MemoryHeap->Capacity)
	{
		*OutCpuAddr = nullptr;
		*OutGpuAddr = 0;
		return;
	}

	*OutCpuAddr = MemoryHeap->CpuStart + MemoryHeap->Size;
	*OutGpuAddr = MemoryHeap->GpuStart + MemoryHeap->Size;

	MemoryHeap->Size += Size;
}

static void _mzAllocateUploadMemory(mzDxContext* Dx, u32 Size, u8** OutCpuAddr, D3D12_GPU_VIRTUAL_ADDRESS* OutGpuAddr)
{
	assert(Dx && Size > 0 && OutCpuAddr && OutGpuAddr);

	u8* CpuAddr;
	D3D12_GPU_VIRTUAL_ADDRESS GpuAddr;
	_mzAllocateGpuMemory(&Dx->UploadMemoryHeaps[Dx->FrameIndex], Size, &CpuAddr, &GpuAddr);

	if (CpuAddr == nullptr && GpuAddr == 0)
	{
		mzV(Dx->CmdList->Close());
		Dx->CmdQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&Dx->CmdList);

		mzWaitForGpu(Dx);
		mzBeginFrame(Dx);
	}

	_mzAllocateGpuMemory(&Dx->UploadMemoryHeaps[Dx->FrameIndex], Size, &CpuAddr, &GpuAddr);
	assert(CpuAddr != nullptr && GpuAddr != 0);

	*OutCpuAddr = CpuAddr;
	*OutGpuAddr = GpuAddr;
}

void mzAllocateUploadBufferRegion(mzDxContext* Dx, u32 Size, u8** OutCpuAddr, ID3D12Resource** OutBuffer, u64* OutBufferOffset)
{
	assert(Dx && Size > 0 && OutCpuAddr && OutBuffer && OutBufferOffset);

	if (Size & 0xff)
	{
		// Always align to 256 bytes.
		Size = (Size + 255) & ~0xff;
	}

	u8* CpuAddr;
	D3D12_GPU_VIRTUAL_ADDRESS GpuAddr;
	_mzAllocateUploadMemory(Dx, Size, &CpuAddr, &GpuAddr);

	*OutCpuAddr = CpuAddr;
	*OutBuffer = Dx->UploadMemoryHeaps[Dx->FrameIndex].Heap;
	*OutBufferOffset = Dx->UploadMemoryHeaps[Dx->FrameIndex].Size - Size;
}

static _mzDescriptorHeap _mzCreateDescriptorHeap(ID3D12Device* Device, u32 Capacity, D3D12_DESCRIPTOR_HEAP_TYPE Type, D3D12_DESCRIPTOR_HEAP_FLAGS Flags)
{
	assert(Device && Capacity > 0);

	_mzDescriptorHeap DescriptorHeap = {};
	DescriptorHeap.Capacity = Capacity;

	D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
	Desc.NumDescriptors = Capacity;
	Desc.Type = Type;
	Desc.Flags = Flags;
	mzV(Device->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&DescriptorHeap.Heap)));

	DescriptorHeap.CpuStart = DescriptorHeap.Heap->GetCPUDescriptorHandleForHeapStart();
	if (Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
	{
		DescriptorHeap.GpuStart = DescriptorHeap.Heap->GetGPUDescriptorHandleForHeapStart();
	}

	DescriptorHeap.DescriptorSize = Device->GetDescriptorHandleIncrementSize(Type);
	return DescriptorHeap;
}

static _mzGpuMemoryHeap _mzCreateGpuMemoryHeap(ID3D12Device* Device, u32 Capacity, D3D12_HEAP_TYPE Type)
{
	assert(Device && Capacity > 0);

	_mzGpuMemoryHeap MemoryHeap = {};
	MemoryHeap.Capacity = Capacity;

	const auto Desc = CD3DX12_RESOURCE_DESC::Buffer(Capacity);
	mzV(Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(Type), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&MemoryHeap.Heap)));

	mzV(MemoryHeap.Heap->Map(0, &CD3DX12_RANGE(0, 0), (void**)&MemoryHeap.CpuStart));
	MemoryHeap.GpuStart = MemoryHeap.Heap->GetGPUVirtualAddress();

	return MemoryHeap;
}

static mzDxResource* _mzAddResource(_mzResourcePool* Pool, ID3D12Resource* Raw, D3D12_RESOURCE_STATES State, DXGI_FORMAT Format)
{
	assert(Pool && Raw);

	u32 SlotIdx = 1;
	for (; SlotIdx <= _mzMaxNumResources; ++SlotIdx)
	{
		if (Pool->Resources[SlotIdx].Raw == nullptr)
		{
			break;
		}
	}
	assert(SlotIdx <= _mzMaxNumResources);

	Pool->Resources[SlotIdx].Raw = Raw;
	Pool->Resources[SlotIdx].State = State;
	Pool->Resources[SlotIdx].Format = Format;

	return (mzDxResource*)(((u64)SlotIdx << 32) | (u64)(Pool->Generations[SlotIdx] += 1));
}

static _mzResourceInfo* _mzGetResourceInfo(mzDxContext* Dx, mzDxResource* Resource)
{
	assert(Dx && Resource);

	const u64 Id = (u64)Resource;
	const u32 Generation = Id & 0xffffffff;
	const u32 SlotIdx = (Id >> 32) & 0xffffffff;
	assert(SlotIdx > 0 && SlotIdx <= _mzMaxNumResources);
	assert(Generation > 0 && Generation == Dx->ResourcePool.Generations[SlotIdx]);
	assert(Dx->ResourcePool.Resources[SlotIdx].Raw);

	return &Dx->ResourcePool.Resources[SlotIdx];
}

ID3D12Resource* mzGetRawResource(mzDxContext* Dx, mzDxResource* Resource)
{
	return _mzGetResourceInfo(Dx, Resource)->Raw;
}

mzDxResource* mzCreateCommittedResource(
	mzDxContext* Dx, D3D12_HEAP_TYPE HeapType, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* Desc,
	D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* ClearValue)
{
	assert(Dx && Desc);

	ID3D12Resource* Raw;
	mzV(Dx->Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(HeapType), HeapFlags, Desc, InitialState, ClearValue, IID_PPV_ARGS(&Raw)));

	return _mzAddResource(&Dx->ResourcePool, Raw, InitialState, Desc->Format);
}

u32 mzReleaseResource(mzDxContext* Dx, mzDxResource* Resource)
{
	assert(Dx && Resource);

	_mzResourceInfo* Info = _mzGetResourceInfo(Dx, Resource);

	const u32 NumReferences = Info->Raw->Release();
	if (NumReferences == 0)
	{
		memset(Info, 0, sizeof(*Info));
	}

	return NumReferences;
}

static mzDxPipelineState* _mzAddPipeline(_mzPipelinePool* Pool, ID3D12PipelineState* Pso, ID3D12RootSignature* RootSignature, bool bIsCompute)
{
	assert(Pool && Pso && RootSignature);

	u32 SlotIdx = 1;
	for (; SlotIdx <= _mzMaxNumPipelines; ++SlotIdx)
	{
		if (Pool->Pipelines[SlotIdx].Pso == nullptr)
		{
			break;
		}
	}
	assert(SlotIdx <= _mzMaxNumPipelines);

	Pool->Pipelines[SlotIdx].Pso = Pso;
	Pool->Pipelines[SlotIdx].RootSignature = RootSignature;
	Pool->Pipelines[SlotIdx].bIsCompute = bIsCompute;

	return (mzDxPipelineState*)(((u64)SlotIdx << 32) | (u64)(Pool->Generations[SlotIdx] += 1));
}

static _mzPipelineInfo* _mzGetPipelineInfo(mzDxContext* Dx, mzDxPipelineState* Pipeline)
{
	assert(Dx && Pipeline);

	const u64 Id = (u64)Pipeline;
	const u32 Generation = Id & 0xffffffff;
	const u32 SlotIdx = (Id >> 32) & 0xffffffff;
	assert(SlotIdx > 0 && SlotIdx <= _mzMaxNumPipelines);
	assert(Generation > 0 && Generation == Dx->PipelinePool.Generations[SlotIdx]);
	assert(Dx->PipelinePool.Pipelines[SlotIdx].Pso);
	assert(Dx->PipelinePool.Pipelines[SlotIdx].RootSignature);

	return &Dx->PipelinePool.Pipelines[SlotIdx];
}

static u64 _mzGetGraphicsPipelineHash(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* PsoDesc)
{
	assert(PsoDesc && PsoDesc->VS.pShaderBytecode && PsoDesc->PS.pShaderBytecode);

	meow_state Hasher = {};
	MeowBegin(&Hasher, MeowDefaultSeed);

	MeowAbsorb(&Hasher, PsoDesc->VS.BytecodeLength, (void*)PsoDesc->VS.pShaderBytecode);
	MeowAbsorb(&Hasher, PsoDesc->PS.BytecodeLength, (void*)PsoDesc->PS.pShaderBytecode);
	MeowAbsorb(&Hasher, sizeof(PsoDesc->BlendState), (void*)&PsoDesc->BlendState);
	MeowAbsorb(&Hasher, sizeof(PsoDesc->SampleMask), (void*)&PsoDesc->SampleMask);
	MeowAbsorb(&Hasher, sizeof(PsoDesc->RasterizerState), (void*)&PsoDesc->RasterizerState);
	MeowAbsorb(&Hasher, sizeof(PsoDesc->DepthStencilState), (void*)&PsoDesc->DepthStencilState);

	for (u32 ElementIdx = 0; ElementIdx < PsoDesc->InputLayout.NumElements; ++ElementIdx)
	{
		MeowAbsorb(&Hasher, sizeof(PsoDesc->InputLayout.pInputElementDescs[ElementIdx]), (void*)&PsoDesc->InputLayout.pInputElementDescs[ElementIdx]);
	}

	MeowAbsorb(&Hasher, sizeof(PsoDesc->IBStripCutValue), (void*)&PsoDesc->IBStripCutValue);
	MeowAbsorb(&Hasher, sizeof(PsoDesc->PrimitiveTopologyType), (void*)&PsoDesc->PrimitiveTopologyType);

	for (u32 TargetIdx = 0; TargetIdx < PsoDesc->NumRenderTargets; ++TargetIdx)
	{
		MeowAbsorb(&Hasher, sizeof(PsoDesc->RTVFormats[TargetIdx]), (void*)&PsoDesc->RTVFormats[TargetIdx]);
	}

	MeowAbsorb(&Hasher, sizeof(PsoDesc->DSVFormat), (void*)&PsoDesc->DSVFormat);
	MeowAbsorb(&Hasher, sizeof(PsoDesc->SampleDesc), (void*)&PsoDesc->SampleDesc);
	MeowAbsorb(&Hasher, sizeof(PsoDesc->NodeMask), (void*)&PsoDesc->NodeMask);
	MeowAbsorb(&Hasher, sizeof(PsoDesc->Flags), (void*)&PsoDesc->Flags);

	return MeowU64From(MeowEnd(&Hasher, nullptr), 0);
}

static u64 _mzGetComputePipelineHash(const D3D12_COMPUTE_PIPELINE_STATE_DESC* PsoDesc)
{
	assert(PsoDesc && PsoDesc->CS.pShaderBytecode);

	meow_state Hasher = {};
	MeowBegin(&Hasher, MeowDefaultSeed);

	MeowAbsorb(&Hasher, PsoDesc->CS.BytecodeLength, (void*)PsoDesc->CS.pShaderBytecode);
	MeowAbsorb(&Hasher, sizeof(PsoDesc->NodeMask), (void*)&PsoDesc->NodeMask);
	MeowAbsorb(&Hasher, sizeof(PsoDesc->Flags), (void*)&PsoDesc->Flags);

	return MeowU64From(MeowEnd(&Hasher, nullptr), 0);
}

mzDxPipelineState* mzCreateGraphicsPipeline(mzDxContext* Dx, D3D12_GRAPHICS_PIPELINE_STATE_DESC* PsoDesc, const char* VsName, const char* PsName)
{
	assert(Dx && PsoDesc && VsName && PsName);

	std::vector<u8> VsBytecode;
	{
		char Path[256];
		snprintf(Path, sizeof(Path), "Data/Shaders/%s", VsName);
		VsBytecode = mzLoadFile(Path);
	}

	std::vector<u8> PsBytecode;
	{
		char Path[256];
		snprintf(Path, sizeof(Path), "Data/Shaders/%s", PsName);
		PsBytecode = mzLoadFile(Path);
	}

	PsoDesc->VS.pShaderBytecode = VsBytecode.data();
	PsoDesc->VS.BytecodeLength = VsBytecode.size();
	PsoDesc->PS.pShaderBytecode = PsBytecode.data();
	PsoDesc->PS.BytecodeLength = PsBytecode.size();

	const u64 Hash = _mzGetGraphicsPipelineHash(PsoDesc);
	auto Found = Dx->PipelinePool.Map.find(Hash);
	if (Found != Dx->PipelinePool.Map.end())
	{
		_mzPipelineInfo* Info = _mzGetPipelineInfo(Dx, Found->second);
		Info->Pso->AddRef();
		Info->RootSignature->AddRef();
		return Found->second;
	}

	ID3D12RootSignature* RootSignature = nullptr;
	mzV(Dx->Device->CreateRootSignature(0, VsBytecode.data(), VsBytecode.size(), IID_PPV_ARGS(&RootSignature)));

	ID3D12PipelineState* Pso = nullptr;
	mzV(Dx->Device->CreateGraphicsPipelineState(PsoDesc, IID_PPV_ARGS(&Pso)));

	mzDxPipelineState* Pipeline = _mzAddPipeline(&Dx->PipelinePool, Pso, RootSignature, /* bIsCompute */ false);

	Dx->PipelinePool.Map.insert(std::make_pair(Hash, Pipeline));

	return Pipeline;
}

mzDxPipelineState* mzCreateComputePipeline(mzDxContext* Dx, D3D12_COMPUTE_PIPELINE_STATE_DESC* PsoDesc, const char* CsName)
{
	assert(Dx && PsoDesc && CsName);

	std::vector<u8> CsBytecode;
	{
		char Path[256];
		snprintf(Path, sizeof(Path), "Data/Shaders/%s", CsName);
		CsBytecode = mzLoadFile(Path);
	}

	PsoDesc->CS.pShaderBytecode = CsBytecode.data();
	PsoDesc->CS.BytecodeLength = CsBytecode.size();

	const u64 Hash = _mzGetComputePipelineHash(PsoDesc);
	auto Found = Dx->PipelinePool.Map.find(Hash);
	if (Found != Dx->PipelinePool.Map.end())
	{
		_mzPipelineInfo* Info = _mzGetPipelineInfo(Dx, Found->second);
		Info->Pso->AddRef();
		Info->RootSignature->AddRef();
		return Found->second;
	}

	ID3D12RootSignature* RootSignature = nullptr;
	mzV(Dx->Device->CreateRootSignature(0, CsBytecode.data(), CsBytecode.size(), IID_PPV_ARGS(&RootSignature)));

	ID3D12PipelineState* Pso = nullptr;
	mzV(Dx->Device->CreateComputePipelineState(PsoDesc, IID_PPV_ARGS(&Pso)));

	mzDxPipelineState* Pipeline = _mzAddPipeline(&Dx->PipelinePool, Pso, RootSignature, /* bIsCompute */ true);

	Dx->PipelinePool.Map.insert(std::make_pair(Hash, Pipeline));

	return Pipeline;
}

u32 mzReleasePipeline(mzDxContext* Dx, mzDxPipelineState* Pipeline)
{
	assert(Dx && Pipeline);

	_mzPipelineInfo* Info = _mzGetPipelineInfo(Dx, Pipeline);

	const u32 NumReferences = Info->Pso->Release();
	if (Info->RootSignature->Release() != NumReferences)
	{
		assert(0);
	}

	if (NumReferences == 0)
	{
		u64 HashToRemove = 0;

		for (auto Iter = Dx->PipelinePool.Map.begin(); Iter != Dx->PipelinePool.Map.end(); ++Iter)
		{
			if (Iter->second == Pipeline)
			{
				HashToRemove = Iter->first;
				break;
			}
		}
		assert(HashToRemove != 0);

		Dx->PipelinePool.Map.erase(HashToRemove);

		memset(Info, 0, sizeof(*Info));
	}

	return NumReferences;
}

mzDxContext* mzCreateDxContext(HWND Window)
{
	assert(Window);

	static mzDxContext Dx;

	IDXGIFactory4* Factory;
#ifdef _DEBUG
	mzV(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&Factory)));
#else
	VHR(CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory)));
#endif

#ifdef _DEBUG
	{
		ID3D12Debug* Debug;
		D3D12GetDebugInterface(IID_PPV_ARGS(&Debug));
		if (Debug)
		{
			Debug->EnableDebugLayer();
			ID3D12Debug1* Debug1;
			Debug->QueryInterface(IID_PPV_ARGS(&Debug1));
			if (Debug1)
			{
				Debug1->SetEnableGPUBasedValidation(TRUE);
			}
			mzSafeRelease(Debug);
			mzSafeRelease(Debug1);
		}
	}
#endif

	if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_1, IID_PPV_ARGS(&Dx.Device))))
	{
		MessageBox(Window, "This application requires DirectX 12 support.", "D3D12CreateDevice failed", MB_OK | MB_ICONERROR);
		return nullptr;
	}

	Dx.Window = Window;

	D3D12_COMMAND_QUEUE_DESC CmdQueueDesc = {};
	CmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	CmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	CmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	mzV(Dx.Device->CreateCommandQueue(&CmdQueueDesc, IID_PPV_ARGS(&Dx.CmdQueue)));

	DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};
	SwapChainDesc.BufferCount = _mzNumSwapBuffers;
	SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapChainDesc.OutputWindow = Window;
	SwapChainDesc.SampleDesc.Count = 1;
	SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	SwapChainDesc.Windowed = TRUE;

	IDXGISwapChain* TempSwapChain;
	mzV(Factory->CreateSwapChain(Dx.CmdQueue, &SwapChainDesc, &TempSwapChain));
	mzV(TempSwapChain->QueryInterface(IID_PPV_ARGS(&Dx.SwapChain)));
	mzSafeRelease(TempSwapChain);
	mzSafeRelease(Factory);

	RECT Rect;
	GetClientRect(Window, &Rect);
	Dx.Resolution[0] = (u32)Rect.right;
	Dx.Resolution[1] = (u32)Rect.bottom;

	for (u32 FrameIdx = 0; FrameIdx < _mzNumBufferedFrames; ++FrameIdx)
	{
		mzV(Dx.Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Dx.CmdAlloc[FrameIdx])));
	}

	Dx.RtvHeap = _mzCreateDescriptorHeap(Dx.Device, 1024, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
	Dx.DsvHeap = _mzCreateDescriptorHeap(Dx.Device, 1024, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
	Dx.CbvSrvUavCpuHeap = _mzCreateDescriptorHeap(Dx.Device, 16 * 1024, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

	for (u32 FrameIdx = 0; FrameIdx < _mzNumBufferedFrames; ++FrameIdx)
	{
		Dx.CbvSrvUavGpuHeaps[FrameIdx] = _mzCreateDescriptorHeap(Dx.Device, 16 * 1024, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
		Dx.UploadMemoryHeaps[FrameIdx] = _mzCreateGpuMemoryHeap(Dx.Device, 8 * 1024 * 1024, D3D12_HEAP_TYPE_UPLOAD);
	}

	Dx.ResourcePool.Resources = new _mzResourceInfo[_mzMaxNumResources + 1];
	Dx.ResourcePool.Generations = new u32[_mzMaxNumResources + 1];
	memset(Dx.ResourcePool.Resources, 0, sizeof(_mzResourceInfo) * (_mzMaxNumResources + 1));
	memset(Dx.ResourcePool.Generations, 0, sizeof(u32) * (_mzMaxNumResources + 1));

	Dx.PipelinePool.Pipelines = new _mzPipelineInfo[_mzMaxNumPipelines + 1];
	Dx.PipelinePool.Generations = new u32[_mzMaxNumPipelines + 1];
	memset(Dx.PipelinePool.Pipelines, 0, sizeof(_mzPipelineInfo) * (_mzMaxNumPipelines + 1));
	memset(Dx.PipelinePool.Generations, 0, sizeof(u32) * (_mzMaxNumPipelines + 1));

	// Swap-buffer render targets.
	{
		D3D12_CPU_DESCRIPTOR_HANDLE Handle = mzAllocateCpuDescriptors(&Dx, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, _mzNumSwapBuffers);

		for (u32 BufferIdx = 0; BufferIdx < _mzNumSwapBuffers; ++BufferIdx)
		{
			ID3D12Resource* Resource;
			mzV(Dx.SwapChain->GetBuffer(BufferIdx, IID_PPV_ARGS(&Resource)));
			Dx.SwapBuffers[BufferIdx] = _mzAddResource(&Dx.ResourcePool, Resource, D3D12_RESOURCE_STATE_PRESENT, SwapChainDesc.BufferDesc.Format);
			Dx.Device->CreateRenderTargetView(mzGetRawResource(&Dx, Dx.SwapBuffers[BufferIdx]), nullptr, Handle);
			Handle.ptr += Dx.RtvHeap.DescriptorSize;
		}
	}

	mzV(Dx.Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Dx.CmdAlloc[0], nullptr, IID_PPV_ARGS(&Dx.CmdList)));
	mzV(Dx.CmdList->Close());

	mzV(Dx.Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Dx.FrameFence)));
	Dx.FrameFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);

	return &Dx;
}

void mzDestroy(mzDxContext* Dx)
{
	assert(Dx);

	for (u32 SwapBufferIdx = 0; SwapBufferIdx < _mzNumSwapBuffers; ++SwapBufferIdx)
	{
		mzReleaseResource(Dx, Dx->SwapBuffers[SwapBufferIdx]);
	}

	for (u32 ResourceIdx = _mzNumSwapBuffers; ResourceIdx <= _mzMaxNumResources; ++ResourceIdx)
	{
		assert(Dx->ResourcePool.Resources[ResourceIdx].Raw == nullptr);
	}

	delete[] Dx->ResourcePool.Resources;
	delete[] Dx->ResourcePool.Generations;

	for (u32 PipelineIdx = 0; PipelineIdx <= _mzMaxNumPipelines; ++PipelineIdx)
	{
		assert(Dx->PipelinePool.Pipelines[PipelineIdx].Pso == nullptr);
		assert(Dx->PipelinePool.Pipelines[PipelineIdx].RootSignature == nullptr);
	}
	assert(Dx->PipelinePool.Map.empty() == true);

	delete[] Dx->PipelinePool.Pipelines;
	delete[] Dx->PipelinePool.Generations;

	CloseHandle(Dx->FrameFenceEvent);
	mzSafeRelease(Dx->CmdList);
	mzSafeRelease(Dx->RtvHeap.Heap);
	mzSafeRelease(Dx->DsvHeap.Heap);
	for (u32 BufferIdx = 0; BufferIdx < _mzNumBufferedFrames; ++BufferIdx)
	{
		mzSafeRelease(Dx->CmdAlloc[BufferIdx]);
		mzSafeRelease(Dx->CbvSrvUavGpuHeaps[BufferIdx].Heap);
		mzSafeRelease(Dx->UploadMemoryHeaps[BufferIdx].Heap);
	}
	mzSafeRelease(Dx->CbvSrvUavCpuHeap.Heap);
	mzSafeRelease(Dx->FrameFence);
	mzSafeRelease(Dx->SwapChain);
	mzSafeRelease(Dx->CmdQueue);
	mzSafeRelease(Dx->Device);
}

ID3D12GraphicsCommandList* mzBeginFrame(mzDxContext* Dx)
{
	assert(Dx);
	ID3D12CommandAllocator* CmdAlloc = Dx->CmdAlloc[Dx->FrameIndex];
	ID3D12GraphicsCommandList* CmdList = Dx->CmdList;
	mzV(CmdAlloc->Reset());
	mzV(CmdList->Reset(CmdAlloc, nullptr));
	CmdList->SetDescriptorHeaps(1, &Dx->CbvSrvUavGpuHeaps[Dx->FrameIndex].Heap);
	Dx->CurrentPipeline = nullptr;
	return CmdList;
}

void mzEndFrame(mzDxContext* Dx, u32 SwapInterval)
{
	assert(Dx);

	mzV(Dx->CmdList->Close());
	Dx->CmdQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&Dx->CmdList);

	mzV(Dx->SwapChain->Present(SwapInterval, 0));
	mzV(Dx->CmdQueue->Signal(Dx->FrameFence, ++Dx->NumFrames));

	const u64 NumGpuFrames = Dx->FrameFence->GetCompletedValue();

	if ((Dx->NumFrames - NumGpuFrames) >= _mzNumBufferedFrames)
	{
		Dx->FrameFence->SetEventOnCompletion(NumGpuFrames + 1, Dx->FrameFenceEvent);
		WaitForSingleObject(Dx->FrameFenceEvent, INFINITE);
	}

	Dx->FrameIndex = (Dx->FrameIndex + 1) % _mzNumBufferedFrames;
	Dx->BackBufferIndex = Dx->SwapChain->GetCurrentBackBufferIndex();
	Dx->CbvSrvUavGpuHeaps[Dx->FrameIndex].Size = 0;
	Dx->UploadMemoryHeaps[Dx->FrameIndex].Size = 0;
}

void mzWaitForGpu(mzDxContext* Dx)
{
	assert(Dx);

	Dx->CmdQueue->Signal(Dx->FrameFence, ++Dx->NumFrames);
	Dx->FrameFence->SetEventOnCompletion(Dx->NumFrames, Dx->FrameFenceEvent);
	WaitForSingleObject(Dx->FrameFenceEvent, INFINITE);

	Dx->CbvSrvUavGpuHeaps[Dx->FrameIndex].Size = 0;
	Dx->UploadMemoryHeaps[Dx->FrameIndex].Size = 0;
}

void mzGetBackBuffer(mzDxContext* Dx, mzDxResource** OutResource, D3D12_CPU_DESCRIPTOR_HANDLE* OutRtv)
{
	assert(Dx && OutResource && OutRtv);

	*OutResource = Dx->SwapBuffers[Dx->BackBufferIndex];
	*OutRtv = Dx->RtvHeap.CpuStart;
	OutRtv->ptr += (u64)Dx->BackBufferIndex * Dx->RtvHeap.DescriptorSize;

	assert(mzGetRawResource(Dx, *OutResource));
}

ID3D12Device* mzGetDevice(mzDxContext* Dx)
{
	assert(Dx && Dx->Device);
	return Dx->Device;
}

ID3D12GraphicsCommandList* mzGetCommandList(mzDxContext* Dx)
{
	assert(Dx && Dx->CmdList);
	return Dx->CmdList;
}

ID3D12CommandQueue* mzGetCommandQueue(mzDxContext* Dx)
{
	assert(Dx && Dx->CmdQueue);
	return Dx->CmdQueue;
}

u32 mzGetFrameIndex(mzDxContext* Dx)
{
	assert(Dx);
	return Dx->FrameIndex;
}

void mzEncodeTransitionBarrier(mzDxContext* Dx, mzDxResource* Resource, D3D12_RESOURCE_STATES StateAfter)
{
	assert(Dx);

	_mzResourceInfo* Info = _mzGetResourceInfo(Dx, Resource);

	if (StateAfter != Info->State)
	{
		// NOTE(mziulek): Buffer barriers and submit all at once? Support subresource granularity?
		Dx->CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(Info->Raw, Info->State, StateAfter));

		Info->State = StateAfter;
	}
}

void mzEncodeSetPipelineState(mzDxContext* Dx, mzDxPipelineState* Pipeline)
{
	assert(Dx);

	_mzPipelineInfo* Info = _mzGetPipelineInfo(Dx, Pipeline);

	if (Pipeline != Dx->CurrentPipeline)
	{
		Dx->CmdList->SetPipelineState(Info->Pso);

		if (Info->bIsCompute)
		{
			Dx->CmdList->SetComputeRootSignature(Info->RootSignature);
		}
		else
		{
			Dx->CmdList->SetGraphicsRootSignature(Info->RootSignature);
		}

		Dx->CurrentPipeline = Pipeline;
	}
}
