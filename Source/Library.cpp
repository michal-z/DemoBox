#include "Pch.h"
#include "Library.h"

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
