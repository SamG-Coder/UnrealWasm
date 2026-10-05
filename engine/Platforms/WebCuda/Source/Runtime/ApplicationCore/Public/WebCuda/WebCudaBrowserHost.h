#pragma once
#include <stdint.h>

namespace WebCudaBrowser
{
enum class Kind : uint8_t { Move, Down, Up, Wheel, KeyDown, KeyUp, Character, Resize, Focus, Reset };
struct Event { Kind Type; int A=0, B=0; double X=0, Y=0; bool Repeat=false; };
// Single browser-main-thread host. No engine dependencies: the same code is
// exercised by browser validation and linked into ApplicationCore.
bool Initialize();
void Shutdown();
bool Poll(Event& Out);
void Size(int& Width, int& Height);
bool Resize(int Width, int Height);
bool Foreground();
void Focus();
void Visible(bool Show);
void Cursor(const char* Style);
void Title(const char* Text);
using FrameCallback = bool(*)(double Milliseconds, void* User);
bool StartFrames(FrameCallback Callback, void* User);
void StopFrames();
}
