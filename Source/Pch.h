#pragma once
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <stdint.h>
#include <stdlib.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <assert.h>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <DirectXMath.h>
#define D3DX12_NO_STATE_OBJECT_HELPERS
#include "d3dx12.h"
using namespace DirectX;

typedef char i8;
typedef short i16;
typedef int i32;
typedef long long i64;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef float f32;
typedef double f64;

#define mzV(Hr) if (FAILED(Hr)) { assert(0); }
#define mzSafeRelease(Obj) if ((Obj)) { (Obj)->Release(); (Obj) = nullptr; }

#define ID3D12Device ID3D12Device2
#define ID3D12GraphicsCommandList ID3D12GraphicsCommandList3
