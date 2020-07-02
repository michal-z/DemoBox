#include "Pch.h"
#include "Library.h"
#include <dxcapi.h>

std::vector<u8> mzLoadFile(const char* Name)
{
	assert(Name);
	FILE* File = fopen(Name, "rb");
	assert(File);
	fseek(File, 0, SEEK_END);
	long Size = ftell(File);
	if (Size <= 0)
	{
		assert(0);
		return std::vector<u8>();
	}
	std::vector<u8> Content(Size);
	fseek(File, 0, SEEK_SET);
	fread(Content.data(), 1, Content.size(), File);
	fclose(File);
	return Content;
}

void mzUpdateFrameStats(HWND Window, const char* Name, f64* OutTime, f32* OutDeltaTime)
{
	assert(Name && OutTime && OutDeltaTime);

	static f64 PreviousTime = -1.0;
	static f64 HeaderRefreshTime = 0.0;
	static u32 FrameCount = 0;

	if (PreviousTime < 0.0)
	{
		PreviousTime = mzGetTime();
		HeaderRefreshTime = PreviousTime;
	}

	*OutTime = mzGetTime();
	*OutDeltaTime = (f32)(*OutTime - PreviousTime);
	PreviousTime = *OutTime;

	if ((*OutTime - HeaderRefreshTime) >= 1.0)
	{
		const f64 Fps = FrameCount / (*OutTime - HeaderRefreshTime);
		const f64 Ms = (1.0 / Fps) * 1000.0;
		char Header[256];
		snprintf(Header, sizeof(Header), "[%.1f fps  %.3f ms] %s", Fps, Ms, Name);
		SetWindowText(Window, Header);
		HeaderRefreshTime = *OutTime;
		FrameCount = 0;
	}
	FrameCount++;
}

f64 mzGetTime()
{
	static LARGE_INTEGER StartCounter;
	static LARGE_INTEGER Frequency;
	if (StartCounter.QuadPart == 0)
	{
		QueryPerformanceFrequency(&Frequency);
		QueryPerformanceCounter(&StartCounter);
	}
	LARGE_INTEGER Counter;
	QueryPerformanceCounter(&Counter);
	return (Counter.QuadPart - StartCounter.QuadPart) / (f64)Frequency.QuadPart;
}

static LRESULT CALLBACK _mzProcessWindowMessage(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	switch (Message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_KEYDOWN:
		if (WParam == VK_ESCAPE)
		{
			PostQuitMessage(0);
			return 0;
		}
		break;
	}
	return DefWindowProc(Window, Message, WParam, LParam);
}

HWND mzCreateWindow(const char* Name, u32 Width, u32 Height)
{
	WNDCLASS WinClass = {};
	WinClass.lpfnWndProc = _mzProcessWindowMessage;
	WinClass.hInstance = GetModuleHandle(nullptr);
	WinClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	WinClass.lpszClassName = Name;
	if (!RegisterClass(&WinClass))
	{
		assert(0);
	}

	RECT Rect = { 0, 0, (LONG)Width, (LONG)Height };
	if (!AdjustWindowRect(&Rect, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX, 0))
	{
		assert(0);
	}

	const HWND Window = CreateWindowEx(
		0, Name, Name, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
		Rect.right - Rect.left, Rect.bottom - Rect.top, nullptr, nullptr, nullptr, 0);

	assert(Window);
	return Window;
}

#pragma comment(lib, "dxcompiler.lib")

struct mzShaderCompiler
{
	IDxcLibrary* Library;
	IDxcCompiler* Compiler;
};

mzShaderCompiler* mzCreateShaderCompiler()
{
	IDxcLibrary* Library;
	HRESULT Result = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&Library));
	if (FAILED(Result))
	{
		return nullptr;
	}

	IDxcCompiler* Compiler;
	Result = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&Compiler));
	if (FAILED(Result))
	{
		mzSafeRelease(Library);
		return nullptr;
	}

	return new mzShaderCompiler{ Library, Compiler };
}

void mzDestroyShaderCompiler(mzShaderCompiler* Compiler)
{
	assert(Compiler && Compiler->Library && Compiler->Compiler);

	mzSafeRelease(Compiler->Compiler);
	mzSafeRelease(Compiler->Library);

	delete Compiler;
}

std::vector<u8> mzCompileShaderFromFile(mzShaderCompiler* Compiler, const char* InSourceFile, const char* InEntryPoint, const char* InTargetProfile)
{
	assert(Compiler && Compiler->Library && Compiler->Compiler && InSourceFile && InEntryPoint && InTargetProfile);

	wchar_t SourceFile[MAX_PATH];
	wchar_t EntryPoint[MAX_PATH];
	wchar_t TargetProfile[32];
	mbstowcs(SourceFile, InSourceFile, ARRAYSIZE(SourceFile));
	mbstowcs(EntryPoint, InEntryPoint, ARRAYSIZE(EntryPoint));
	mbstowcs(TargetProfile, InTargetProfile, ARRAYSIZE(TargetProfile));

	u32 CodePage = CP_UTF8;
	IDxcBlobEncoding* SourceBlob;
	HRESULT Result = Compiler->Library->CreateBlobFromFile(SourceFile, &CodePage, &SourceBlob);
	if (FAILED(Result))
	{
		return std::vector<u8>();
	}

	IDxcOperationResult* CompilationResult;
#ifdef _DEBUG
	const wchar_t* Arguments[] = { L"/Ges", L"/Zi", L"/Od" };
#else
	const wchar_t* Arguments[] = { L"/Ges" };
#endif
	Result = Compiler->Compiler->Compile(
		SourceBlob,
		SourceFile,
		EntryPoint,
		TargetProfile,
		Arguments, ARRAYSIZE(Arguments),
		nullptr, 0, nullptr,
		&CompilationResult);

	if (SUCCEEDED(Result))
	{
		CompilationResult->GetStatus(&Result);
	}

	if (FAILED(Result))
	{
		if (CompilationResult)
		{
			IDxcBlobEncoding* ErrorBlob;
			Result = CompilationResult->GetErrorBuffer(&ErrorBlob);
			if (SUCCEEDED(Result) && ErrorBlob)
			{
				char Message[512];
				snprintf(Message, sizeof(Message), "Compilation failed with errors:\n%s\n", (const char*)ErrorBlob->GetBufferPointer());
				OutputDebugStringA(Message);
			}
			mzSafeRelease(ErrorBlob);
			mzSafeRelease(CompilationResult);
		}

		mzSafeRelease(SourceBlob);
		return std::vector<u8>();
	}
	assert(CompilationResult);

	IDxcBlob* BytecodeBlob;
	CompilationResult->GetResult(&BytecodeBlob);

	std::vector<u8> Bytecode((u8*)BytecodeBlob->GetBufferPointer(), (u8*)BytecodeBlob->GetBufferPointer() + BytecodeBlob->GetBufferSize());

	mzSafeRelease(SourceBlob);
	mzSafeRelease(BytecodeBlob);

	return Bytecode;
}
