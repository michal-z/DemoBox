#pragma once

std::vector<u8> mzLoadFile(const char* Name);
void mzUpdateFrameStats(HWND Window, const char* Name, f64* OutTime, f32* OutDeltaTime);
f64 mzGetTime();
HWND mzCreateWindow(const char* Name, u32 Width, u32 Height);

struct mzShaderCompiler;
mzShaderCompiler* mzCreateShaderCompiler();
void mzDestroyShaderCompiler(mzShaderCompiler* Compiler);
std::vector<u8> mzCompileShaderFromFile(mzShaderCompiler* Compiler, const char* InSourceFile, const char* InEntryPoint, const char* InTargetProfile);
