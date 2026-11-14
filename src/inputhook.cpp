// Input hooks: let R2 fire while aiming.
// ---------------------------------------------------------------------------
// The game may read the pad through XInput (Steam Input on, or an Xbox pad) or
// straight from the DualSense over HID (Steam Input off). Both are hooked in
// P5R.exe's import table only - our own module and the rest of the process are
// untouched:
//
//   XInputGetState (IAT or GetProcAddress)  ──► AimTracker ──► R2 → Cross
//   CreateFile* / ReadFile / GetOverlappedResult* / HidD_GetInputReport
//        on a DualSense handle               ──► AimTracker ──► R2 → Cross
//
// The tracker watches the raw buttons: D-pad Up enters "aiming", Circle and
// the other menu buttons leave it, and a fire press keeps it alive.
// ---------------------------------------------------------------------------
#include "common.h"
#include <hidsdi.h>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include "input.h"

#pragma comment(lib, "hid.lib")

static InputConfig g_icfg;
static SRWLOCK g_inLock = SRWLOCK_INIT;
static input::AimTracker g_xTrack[4], g_hTrack;
static std::atomic<bool> g_aiming{ false }, g_seen{ false };
static std::atomic<uint32_t> g_aimGen{ 0 };
static uint32_t g_lastGenX[4], g_lastGenH;

void Input_Configure(const InputConfig& c) {
    AcquireSRWLockExclusive(&g_inLock);
    g_icfg = c;
    input::AimConfig a;
    a.fireThreshold = c.fireThreshold; a.releaseThreshold = c.fireThreshold * 7 / 10;
    a.afterFireMs = (uint32_t)c.afterFireMs; a.timeoutMs = (uint32_t)c.timeoutMs;
    for (auto& t : g_xTrack) t.cfg = a;
    g_hTrack.cfg = a;
    ReleaseSRWLockExclusive(&g_inLock);
}
bool Input_Aiming() { return g_aiming.load(); }
bool Input_Seen() { return g_seen.load(); }
uint32_t Input_AimGeneration() { return g_aimGen.load(); }

// Called with g_inLock held.
static void Publish(input::AimTracker& t, uint32_t& lastGen) {
    if (t.Generation() == lastGen) return;
    lastGen = t.Generation();
    g_aiming = t.Aiming();
    g_aimGen.fetch_add(1);
    if (g_icfg.logInput) Log(t.Aiming() ? "AIM start (D-pad Up)" : "AIM end");
    if (g_wake) SetEvent(g_wake);
}

static void FirstSeen(const char* what) {
    if (!g_seen.exchange(true)) Log("input: the game reads the pad through %s - R2 fire while aiming is %s",
        what, g_icfg.remap ? "ON" : "off (Remap=0)");
}

// ───────────────────────────── XInput ─────────────────────────────
typedef DWORD(WINAPI* PFN_XGetState)(DWORD, void*);
static PFN_XGetState g_realXGet, g_realXGetEx;

static void ProcessXInput(DWORD idx, void* state) {
    if (idx > 3 || !state) return;
    auto* g = (input::XGamepad*)((uint8_t*)state + 4);   // XINPUT_STATE { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; }
    FirstSeen("XInput");
    AcquireSRWLockExclusive(&g_inLock);
    input::AimTracker& t = g_xTrack[idx];
    t.Update(input::FromXInput(*g), GetTickCount());
    if (g_icfg.remap && (t.RemapR2() || t.SwallowR2())) input::RemapXInput(*g, t.RemapR2());
    Publish(t, g_lastGenX[idx]);
    ReleaseSRWLockExclusive(&g_inLock);
}

static DWORD WINAPI Hook_XInputGetState(DWORD idx, void* state) {
    DWORD r = g_realXGet ? g_realXGet(idx, state) : ERROR_DEVICE_NOT_CONNECTED;
    if (r == ERROR_SUCCESS) ProcessXInput(idx, state);
    return r;
}
static DWORD WINAPI Hook_XInputGetStateEx(DWORD idx, void* state) {
    DWORD r = g_realXGetEx ? g_realXGetEx(idx, state) : ERROR_DEVICE_NOT_CONNECTED;
    if (r == ERROR_SUCCESS) ProcessXInput(idx, state);
    return r;
}

// ───────────────────────────── DualSense over HID ─────────────────────────────
typedef HANDLE(WINAPI* PFN_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE(WINAPI* PFN_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL(WINAPI* PFN_ReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI* PFN_GetOvl)(HANDLE, LPOVERLAPPED, LPDWORD, BOOL);
typedef BOOL(WINAPI* PFN_GetOvlEx)(HANDLE, LPOVERLAPPED, LPDWORD, DWORD, BOOL);
typedef BOOL(WINAPI* PFN_CloseHandle)(HANDLE);
typedef BOOLEAN(__stdcall* PFN_GetInputReport)(HANDLE, PVOID, ULONG);
typedef FARPROC(WINAPI* PFN_GetProcAddress)(HMODULE, LPCSTR);

static PFN_CreateFileW g_realCreateW;
static PFN_CreateFileA g_realCreateA;
static PFN_ReadFile g_realRead;
static PFN_GetOvl g_realGetOvl;
static PFN_GetOvlEx g_realGetOvlEx;
static PFN_CloseHandle g_realClose;
static PFN_GetInputReport g_realGetInput;
static PFN_GetProcAddress g_realGPA;

// Known DualSense handles (small, lock-protected).
static SRWLOCK g_hLock = SRWLOCK_INIT;
static HANDLE g_dsHandles[16];
struct Pending { LPOVERLAPPED ov; uint8_t* buf; DWORD size; };
static Pending g_pending[32];
static std::atomic<int> g_pendingCount{ 0 };   // fast path: most overlapped I/O in the game is not ours

static bool IsDsPath(const wchar_t* p) {
    if (!p) return false;
    wchar_t low[512]; size_t i = 0;
    for (; p[i] && i < 511; ++i) low[i] = (wchar_t)towlower(p[i]);
    low[i] = 0;
    return wcsstr(low, L"vid_054c") && (wcsstr(low, L"pid_0ce6") || wcsstr(low, L"pid_0df2"));
}
static bool IsDsHandle(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) return false;
    AcquireSRWLockShared(&g_hLock);
    bool r = false;
    for (HANDLE x : g_dsHandles) if (x == h) { r = true; break; }
    ReleaseSRWLockShared(&g_hLock);
    return r;
}
static void AddDsHandle(HANDLE h) {
    AcquireSRWLockExclusive(&g_hLock);
    bool added = false;
    for (HANDLE& x : g_dsHandles) if (!x) { x = h; added = true; break; }
    if (!added) g_dsHandles[0] = h;
    ReleaseSRWLockExclusive(&g_hLock);
}
static void ForgetHandle(HANDLE h) {
    AcquireSRWLockExclusive(&g_hLock);
    for (HANDLE& x : g_dsHandles) if (x == h) x = nullptr;
    ReleaseSRWLockExclusive(&g_hLock);
}
static void AddPending(LPOVERLAPPED ov, void* buf, DWORD size) {
    AcquireSRWLockExclusive(&g_hLock);
    Pending* slot = &g_pending[0];
    for (Pending& p : g_pending) { if (p.ov == ov) { slot = &p; break; } if (!p.ov) slot = &p; }
    if (!slot->ov) g_pendingCount.fetch_add(1);
    *slot = { ov, (uint8_t*)buf, size };
    ReleaseSRWLockExclusive(&g_hLock);
}
static bool TakePending(LPOVERLAPPED ov, Pending& out) {
    if (g_pendingCount.load(std::memory_order_relaxed) == 0) return false;
    AcquireSRWLockExclusive(&g_hLock);
    bool r = false;
    for (Pending& p : g_pending) if (p.ov == ov) { out = p; p.ov = nullptr; r = true; g_pendingCount.fetch_sub(1); break; }
    ReleaseSRWLockExclusive(&g_hLock);
    return r;
}

static void ProcessDs(uint8_t* buf, DWORD n) {
    input::DsLayout l;
    if (!buf || !input::DsLayoutFor(buf, n, l)) return;
    FirstSeen(n >= 78 ? "DualSense HID (Bluetooth)" : n >= 64 ? "DualSense HID (USB)" : "DualSense HID (Bluetooth simple)");
    AcquireSRWLockExclusive(&g_inLock);
    g_hTrack.Update(input::FromDualSense(buf, l), GetTickCount());
    if (g_icfg.remap && (g_hTrack.RemapR2() || g_hTrack.SwallowR2())) input::RemapDualSense(buf, l, g_hTrack.RemapR2());
    Publish(g_hTrack, g_lastGenH);
    ReleaseSRWLockExclusive(&g_inLock);
}

static HANDLE WINAPI Hook_CreateFileW(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t) {
    HANDLE h = g_realCreateW(p, a, s, sa, c, f, t);
    if (h != INVALID_HANDLE_VALUE && IsDsPath(p)) { AddDsHandle(h); Log("input: game opened the DualSense (HID)"); }
    return h;
}
static HANDLE WINAPI Hook_CreateFileA(LPCSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t) {
    HANDLE h = g_realCreateA(p, a, s, sa, c, f, t);
    if (h != INVALID_HANDLE_VALUE && p) {
        wchar_t w[512]; MultiByteToWideChar(CP_ACP, 0, p, -1, w, 512);
        if (IsDsPath(w)) { AddDsHandle(h); Log("input: game opened the DualSense (HID)"); }
    }
    return h;
}
static BOOL WINAPI Hook_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD read, LPOVERLAPPED ov) {
    BOOL ok = g_realRead(h, buf, n, read, ov);
    if (!IsDsHandle(h)) return ok;
    DWORD err = GetLastError();
    if (ok) ProcessDs((uint8_t*)buf, read ? *read : ov ? (DWORD)ov->InternalHigh : n);
    else if (err == ERROR_IO_PENDING && ov) AddPending(ov, buf, n);
    SetLastError(err);
    return ok;
}
static BOOL WINAPI Hook_GetOverlappedResult(HANDLE h, LPOVERLAPPED ov, LPDWORD n, BOOL wait) {
    BOOL ok = g_realGetOvl(h, ov, n, wait);
    Pending p;
    if (ok && ov && TakePending(ov, p)) { DWORD err = GetLastError(); ProcessDs(p.buf, n ? *n : p.size); SetLastError(err); }
    return ok;
}
static BOOL WINAPI Hook_GetOverlappedResultEx(HANDLE h, LPOVERLAPPED ov, LPDWORD n, DWORD ms, BOOL alert) {
    BOOL ok = g_realGetOvlEx(h, ov, n, ms, alert);
    Pending p;
    if (ok && ov && TakePending(ov, p)) { DWORD err = GetLastError(); ProcessDs(p.buf, n ? *n : p.size); SetLastError(err); }
    return ok;
}
static BOOL WINAPI Hook_CloseHandle(HANDLE h) {
    if (IsDsHandle(h)) ForgetHandle(h);
    return g_realClose(h);
}
static BOOLEAN __stdcall Hook_HidD_GetInputReport(HANDLE h, PVOID buf, ULONG n) {
    BOOLEAN ok = g_realGetInput(h, buf, n);
    if (ok) ProcessDs((uint8_t*)buf, n);
    return ok;
}

static bool IsXInputModule(HMODULE m) {
    wchar_t path[MAX_PATH];
    if (!m || !GetModuleFileNameW(m, path, MAX_PATH)) return false;
    const wchar_t* name = wcsrchr(path, L'\\'); name = name ? name + 1 : path;
    return _wcsnicmp(name, L"xinput", 6) == 0;
}

static FARPROC WINAPI Hook_GetProcAddress(HMODULE m, LPCSTR name) {
    FARPROC r = g_realGPA(m, name);
    if (!r) return r;
    const bool byName = ((ULONG_PTR)name >> 16) != 0;
    if (IsXInputModule(m)) {
        if (byName && !strcmp(name, "XInputGetState")) { g_realXGet = (PFN_XGetState)r; return (FARPROC)&Hook_XInputGetState; }
        if (!byName && (ULONG_PTR)name == 100) { g_realXGetEx = (PFN_XGetState)r; return (FARPROC)&Hook_XInputGetStateEx; }
    }
    if (byName && !strcmp(name, "HidD_GetInputReport")) { g_realGetInput = (PFN_GetInputReport)r; return (FARPROC)&Hook_HidD_GetInputReport; }
    return r;
}

// ───────────────────────────── import table patching ─────────────────────────────
struct IatHook { const char* name; void** real; void* hook; void* resolved; };

static bool PatchSlot(void** slot, void* value) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// Patches every import slot of `mod` that names one of `hooks` (or, if the name table was stripped,
// holds the same address). Already-patched slots are skipped, so this can run repeatedly.
static int PatchImports(HMODULE mod, IatHook* hooks, int count, bool logFound) {
    auto* base = (BYTE*)mod;
    auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int patched = 0;
    for (auto* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        auto* iat = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        auto* names = imp->OriginalFirstThunk ? (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk) : nullptr;
        for (size_t i = 0; iat[i].u1.Function; ++i) {
            void* cur = (void*)iat[i].u1.Function;
            const char* fn = nullptr;
            if (names && names[i].u1.AddressOfData && !IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal))
                fn = (const char*)((IMAGE_IMPORT_BY_NAME*)(base + names[i].u1.AddressOfData))->Name;
            for (int k = 0; k < count; ++k) {
                IatHook& h = hooks[k];
                if (cur == h.hook) break;   // ours already
                bool match = fn ? !strcmp(fn, h.name) : (h.resolved && cur == h.resolved);
                if (!match) continue;
                if (!*h.real) *h.real = cur;
                if (PatchSlot((void**)&iat[i].u1.Function, h.hook)) {
                    ++patched;
                    if (logFound) Log("input: hooked %s!%s", (const char*)(base + imp->Name), h.name);
                }
                break;
            }
        }
    }
    return patched;
}

static void* Resolve(const wchar_t* dll, const char* fn) {
    HMODULE m = GetModuleHandleW(dll);
    return m ? (void*)GetProcAddress(m, fn) : nullptr;
}

int Input_InstallHooks() {
    static bool logged = false;
    IatHook hooks[] = {
        { "XInputGetState",        (void**)&g_realXGet,     (void*)&Hook_XInputGetState,        nullptr },
        { "CreateFileW",           (void**)&g_realCreateW,  (void*)&Hook_CreateFileW,           Resolve(L"kernel32.dll", "CreateFileW") },
        { "CreateFileA",           (void**)&g_realCreateA,  (void*)&Hook_CreateFileA,           Resolve(L"kernel32.dll", "CreateFileA") },
        { "ReadFile",              (void**)&g_realRead,     (void*)&Hook_ReadFile,              Resolve(L"kernel32.dll", "ReadFile") },
        { "GetOverlappedResult",   (void**)&g_realGetOvl,   (void*)&Hook_GetOverlappedResult,   Resolve(L"kernel32.dll", "GetOverlappedResult") },
        { "GetOverlappedResultEx", (void**)&g_realGetOvlEx, (void*)&Hook_GetOverlappedResultEx, Resolve(L"kernel32.dll", "GetOverlappedResultEx") },
        { "CloseHandle",           (void**)&g_realClose,    (void*)&Hook_CloseHandle,           Resolve(L"kernel32.dll", "CloseHandle") },
        { "HidD_GetInputReport",   (void**)&g_realGetInput, (void*)&Hook_HidD_GetInputReport,   nullptr },
        { "GetProcAddress",        (void**)&g_realGPA,      (void*)&Hook_GetProcAddress,        Resolve(L"kernel32.dll", "GetProcAddress") },
    };
    // Real functions must exist before any hook can run: fill the ones the game does not import directly.
    if (!g_realCreateW) g_realCreateW = (PFN_CreateFileW)hooks[1].resolved;
    if (!g_realCreateA) g_realCreateA = (PFN_CreateFileA)hooks[2].resolved;
    if (!g_realRead) g_realRead = (PFN_ReadFile)hooks[3].resolved;
    if (!g_realGetOvl) g_realGetOvl = (PFN_GetOvl)hooks[4].resolved;
    if (!g_realGetOvlEx) g_realGetOvlEx = (PFN_GetOvlEx)hooks[5].resolved;
    if (!g_realClose) g_realClose = (PFN_CloseHandle)hooks[6].resolved;
    if (!g_realGPA) g_realGPA = (PFN_GetProcAddress)hooks[8].resolved;
    // XInput may already be loaded by the game: patch by address too.
    static const wchar_t* xdlls[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll", L"xinput1_2.dll", L"xinput1_1.dll" };
    for (auto d : xdlls) if (void* p = Resolve(d, "XInputGetState")) { hooks[0].resolved = p; break; }

    int n = PatchImports(GetModuleHandleW(nullptr), hooks, (int)(sizeof hooks / sizeof hooks[0]), !logged);
    if (n && !logged) logged = true;
    return n;
}
