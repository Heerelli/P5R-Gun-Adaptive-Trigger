// Shared declarations between dllmain.cpp, inputhook.cpp and haptics.cpp.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdint>

#define MOD_VERSION "0.2.0"

void Log(const char* fmt, ...);
bool GameFocused();
extern std::atomic<bool> g_quit;
extern HANDLE g_wake;            // wakes the trigger worker (shots, aim changes)

// ── input (inputhook.cpp) ──
struct InputConfig {
    bool remap = true;           // R2 fires while aiming
    int fireThreshold = 170;
    int afterFireMs = 1500;
    int timeoutMs = 20000;
    bool logInput = true;
};
void Input_Configure(const InputConfig& c);
int  Input_InstallHooks();       // patches P5R.exe's import table; safe to call again (re-patches)
bool Input_Aiming();
bool Input_Seen();               // the game has read a pad through a hooked path
uint32_t Input_AimGeneration();

// ── haptics (haptics.cpp) ──
struct HapticConfig {
    bool enabled = true;
    double gain = 1.0;
    wchar_t deviceName[128] = L"Wireless Controller";
    int bufferMs = 10;
    int leftChannel = 2, rightChannel = 3;
    bool muteUnfocused = true;
};
void Haptics_Configure(const HapticConfig& c);
void Haptics_Start();
void Haptics_Play(int character, double gain);   // character 0-10 (0 = default)
