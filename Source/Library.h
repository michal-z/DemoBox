#pragma once

std::vector<u8> mzLoadFile(const char* Name);
void mzUpdateFrameStats(HWND Window, const char* Name, f64* OutTime, f32* OutDeltaTime);
f64 mzGetTime();
HWND mzCreateWindow(const char* Name, u32 Width, u32 Height);
