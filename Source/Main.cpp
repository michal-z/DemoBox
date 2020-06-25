#include "Pch.h"
#define mzSelectedDemo mzDemo01

struct mzSelectedDemo;
bool mzInit(mzSelectedDemo** OutDemo);
void mzDeinit(mzSelectedDemo* Demo);
void mzUpdate(mzSelectedDemo* Demo);

i32 __stdcall WinMain(HINSTANCE, HINSTANCE, LPSTR, i32)
{
	SetProcessDPIAware();

	mzSelectedDemo* Demo = nullptr;

	if (mzInit(&Demo))
	{
		assert(Demo);

		for (;;)
		{
			MSG Message = {};
			if (PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
			{
				DispatchMessage(&Message);
				if (Message.message == WM_QUIT)
				{
					break;
				}
			}
			else
			{
				mzUpdate(Demo);
			}
		}

		mzDeinit(Demo);
	}

	return 0;
}
