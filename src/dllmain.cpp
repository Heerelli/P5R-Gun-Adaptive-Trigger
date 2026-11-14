// P5R Gun Adaptive Trigger - Reloaded-II native mod for Persona 5 Royal (Steam)
// ---------------------------------------------------------------------------
// Every gun shot in battle spends one bullet. The mod hooks that instruction
// (see hookstub.h), works out who fired, and plays that character's
// adaptive-trigger effect on a DualSense by writing HID output reports itself.
//
//   P5R.exe  dec ax / mov [r15+79D0h],ax ──► stub ──► OnShot(unit, bullets)
//                                                        │  (ring buffer)
//                                                        ▼
//                                         worker thread: who fired? → profile
//                                                        │
//                                                        ▼
//                              DualSense HID output report (trigger flags only)
//
// No other mod, library or app is needed: Reloaded-II only loads the DLL.
// ---------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include "effects.h"
#include "hookstub.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "user32.lib")

#define MOD_VERSION "0.1.0"

// ───────────────────────────── characters ─────────────────────────────
// P5R party unit ids (1-10). 0 = unknown shooter -> [Default].
static const char* kSection[11] = { "Default", "Joker", "Skull", "Mona", "Panther", "Fox", "Queen", "Noir", "Oracle", "Crow", "Violet" };

struct Profile {
    ds::Effect kick;   // played the moment the shot happens
    int kickMs;
    ds::Effect rest;   // held afterwards until HoldMs of no shots
};

// Defaults (same as P5RGunTrigger.ini in the repo).
struct ProfileText { const char* kick; int kickMs; const char* rest; };
static const ProfileText kDefaults[11] = {
    /* Default */ { "vibration 0 8 45", 80,  "weapon 3 6 5" },
    /* Joker   */ { "vibration 0 8 45", 80,  "weapon 3 6 5" },   // handgun: short sharp tap
    /* Skull   */ { "vibration 0 8 25", 170, "weapon 2 7 8" },   // shotgun: heavy, low and long
    /* Mona    */ { "vibration 0 5 90", 60,  "feedback 2 4" },   // slingshot: light snap, elastic pull
    /* Panther */ { "machine 0 9 7 4 35 2", 300, "weapon 3 5 3" }, // SMG: rattling burst
    /* Fox     */ { "vibration 0 8 35", 120, "weapon 4 7 7" },   // rifle: deep crack
    /* Queen   */ { "vibration 0 8 40", 110, "weapon 4 7 6" },   // revolver
    /* Noir    */ { "vibration 0 8 15", 230, "feedback 2 8" },   // grenade launcher: thump, stiff trigger
    /* Oracle  */ { "vibration 0 4 60", 60,  "off" },            // navigator (normally never shoots)
    /* Crow    */ { "vibration 0 4 140", 150, "feedback 1 3" },  // ray gun: thin electric buzz
    /* Violet  */ { "vibration 0 7 50", 90,  "weapon 3 6 5" },
};

// ───────────────────────────── globals / config ─────────────────────────────
static HMODULE g_self;
static wchar_t g_dir[MAX_PATH], g_ini[MAX_PATH], g_logPath[MAX_PATH];
static std::atomic<bool> g_quit{ false };

static struct Config {
    bool enabled = true;
    bool right = true, left = false;  // Trigger=R2 / L2 / Both
    int holdMs = 2500;                // keep the "rest" effect this long after the last shot
    int refreshMs = 400;              // re-send the active effect (in case something overwrote it)
    bool muteUnfocused = true;
    bool hotkeys = true;
    int idMode = 0;                   // 0 = auto (learn), 1 = fixed offset, 2 = off (always [Default])
    int idOffset = -1;                // fixed / learned offset of the unit id inside the battle unit
    bool dumpUnits = true;
    int dumpBytes = 0x200;
    bool logShots = true;
} g_cfg;
static Profile g_profiles[11];

static void Log(const char* fmt, ...) {
    char msg[1024];
    va_list ap; va_start(ap, fmt); _vsnprintf_s(msg, sizeof msg, _TRUNCATE, fmt, ap); va_end(ap);
    SYSTEMTIME st; GetLocalTime(&st);
    char line[1200];
    int n = _snprintf_s(line, sizeof line, _TRUNCATE, "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
    HANDLE h = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w; WriteFile(h, line, (DWORD)(n > 0 ? n : 0), &w, nullptr);
        CloseHandle(h);
    }
}

static void IniString(const char* section, const char* key, const char* def, char* out, DWORD size) {
    wchar_t ws[64], wk[64], wd[256], wo[256];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, ws, 64);
    MultiByteToWideChar(CP_UTF8, 0, key, -1, wk, 64);
    MultiByteToWideChar(CP_UTF8, 0, def, -1, wd, 256);
    GetPrivateProfileStringW(ws, wk, wd, wo, 256, g_ini);
    WideCharToMultiByte(CP_UTF8, 0, wo, -1, out, (int)size, nullptr, nullptr);
}
static int IniInt(const char* section, const char* key, int def) {
    char d[32], buf[64];
    _snprintf_s(d, sizeof d, _TRUNCATE, "%d", def);
    IniString(section, key, d, buf, sizeof buf);
    return (int)strtol(buf, nullptr, 0);   // accepts 0x.. hex
}

static void LoadConfig() {
    g_cfg.enabled       = IniInt("General", "Enabled", 1) != 0;
    char trig[16]; IniString("General", "Trigger", "R2", trig, sizeof trig);
    g_cfg.right = _stricmp(trig, "L2") != 0;
    g_cfg.left  = _stricmp(trig, "L2") == 0 || _stricmp(trig, "Both") == 0;
    g_cfg.holdMs        = IniInt("General", "HoldMs", 2500);
    g_cfg.refreshMs     = IniInt("General", "RefreshMs", 400);
    g_cfg.muteUnfocused = IniInt("General", "MuteWhenUnfocused", 1) != 0;
    g_cfg.hotkeys       = IniInt("General", "TestHotkeys", 1) != 0;
    g_cfg.logShots      = IniInt("General", "LogShots", 1) != 0;

    char mode[16]; IniString("Character", "Mode", "auto", mode, sizeof mode);
    g_cfg.idMode = !_stricmp(mode, "offset") ? 1 : !_stricmp(mode, "off") ? 2 : 0;
    g_cfg.idOffset  = IniInt("Character", "UnitIdOffset", -1);
    if (g_cfg.idOffset < 0 && g_cfg.idMode == 0) g_cfg.idOffset = IniInt("Character", "LearnedOffset", -1);
    g_cfg.dumpUnits = IniInt("Character", "DumpUnits", 1) != 0;
    g_cfg.dumpBytes = IniInt("Character", "DumpBytes", 0x200);
    if (g_cfg.dumpBytes < 0x10) g_cfg.dumpBytes = 0x10;
    if (g_cfg.dumpBytes > 0x400) g_cfg.dumpBytes = 0x400;

    for (int i = 0; i < 11; ++i) {
        char buf[128];
        Profile& p = g_profiles[i];
        IniString(kSection[i], "Kick", kDefaults[i].kick, buf, sizeof buf);
        if (!ds::Parse(buf, p.kick)) { Log("WARN [%s] Kick=\"%s\" is invalid, using default", kSection[i], buf); ds::Parse(kDefaults[i].kick, p.kick); }
        p.kickMs = IniInt(kSection[i], "KickMs", kDefaults[i].kickMs);
        if (p.kickMs < 0) p.kickMs = 0; if (p.kickMs > 2000) p.kickMs = 2000;
        IniString(kSection[i], "Rest", kDefaults[i].rest, buf, sizeof buf);
        if (!ds::Parse(buf, p.rest)) { Log("WARN [%s] Rest=\"%s\" is invalid, using default", kSection[i], buf); ds::Parse(kDefaults[i].rest, p.rest); }
    }
    Log("config: enabled=%d trigger=%s hold=%dms id=%s offset=%d", g_cfg.enabled,
        g_cfg.left && g_cfg.right ? "Both" : g_cfg.left ? "L2" : "R2", g_cfg.holdMs,
        g_cfg.idMode == 0 ? "auto" : g_cfg.idMode == 1 ? "offset" : "off", g_cfg.idOffset);
}

// ───────────────────────────── DualSense HID ─────────────────────────────
static HANDLE g_dev = INVALID_HANDLE_VALUE;
static bool g_bt = false;
static USHORT g_outLen = 0;
static uint8_t g_btSeq = 0;
static HANDLE g_ioEvent;

static bool OpenDualSense() {
    GUID hidGuid; HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    SP_DEVICE_INTERFACE_DATA ifd{ sizeof ifd };
    for (DWORD i = 0; !ok && SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        if (!need) continue;
        auto* det = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)malloc(need);
        if (!det) continue;
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, nullptr)) {
            HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES a{ sizeof a };
                PHIDP_PREPARSED_DATA pp = nullptr; HIDP_CAPS caps{};
                bool match = HidD_GetAttributes(h, &a) && a.VendorID == 0x054C && (a.ProductID == 0x0CE6 || a.ProductID == 0x0DF2);
                if (match && HidD_GetPreparsedData(h, &pp)) {
                    if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.OutputReportByteLength >= ds::kUsbReportSize) {
                        g_dev = h; g_outLen = caps.OutputReportByteLength;
                        g_bt = caps.OutputReportByteLength != ds::kUsbReportSize;
                        ok = true;
                        Log("DualSense%s found (%s, output report %u bytes)", a.ProductID == 0x0DF2 ? " Edge" : "",
                            g_bt ? "Bluetooth" : "USB", g_outLen);
                    }
                    HidD_FreePreparsedData(pp);
                }
                if (!ok) CloseHandle(h);
            }
        }
        free(det);
    }
    SetupDiDestroyDeviceInfoList(set);
    return ok;
}

static void CloseDualSense() {
    if (g_dev != INVALID_HANDLE_VALUE) { CloseHandle(g_dev); g_dev = INVALID_HANDLE_VALUE; }
}

static bool WriteReport(const uint8_t* data, DWORD len) {
    OVERLAPPED ov{}; ov.hEvent = g_ioEvent; ResetEvent(g_ioEvent);
    DWORD done = 0;
    if (!WriteFile(g_dev, data, len, nullptr, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) return false;
        if (WaitForSingleObject(g_ioEvent, 100) != WAIT_OBJECT_0) { CancelIo(g_dev); GetOverlappedResult(g_dev, &ov, &done, TRUE); return false; }
    }
    return GetOverlappedResult(g_dev, &ov, &done, FALSE) != 0;
}

// Sends one effect to the configured trigger(s). Reconnects (at most every 2 s) when the pad is missing.
static DWORD g_lastOpenTry = 0;
static bool g_warnedNoPad = false;
static bool SendEffect(const ds::Effect& e) {
    if (g_dev == INVALID_HANDLE_VALUE) {
        DWORD now = GetTickCount();
        if (g_lastOpenTry && now - g_lastOpenTry < 2000) return false;
        g_lastOpenTry = now;
        if (!OpenDualSense()) {
            if (!g_warnedNoPad) { Log("no DualSense found (USB or Bluetooth) - will keep looking"); g_warnedNoPad = true; }
            return false;
        }
        g_warnedNoPad = false;
    }
    const ds::Effect* r = g_cfg.right ? &e : nullptr;
    const ds::Effect* l = g_cfg.left ? &e : nullptr;
    uint8_t buf[600] = {};
    bool ok;
    if (!g_bt) {
        ds::BuildUsb(buf, r, l);
        ok = WriteReport(buf, ds::kUsbReportSize);
    } else {
        ds::BuildBt(buf, r, l, g_btSeq++);
        DWORD len = g_outLen <= sizeof buf ? g_outLen : (DWORD)sizeof buf;
        ok = WriteReport(buf, len) || HidD_SetOutputReport(g_dev, buf, ds::kBtReportSize);
    }
    if (!ok) { Log("write failed (err %lu) - reconnecting", GetLastError()); CloseDualSense(); }
    return ok;
}

// ───────────────────────────── shot ring (game thread -> worker) ─────────────────────────────
constexpr int kSnapBytes = 0x400;
constexpr uint32_t kRingSize = 32;
struct ShotRec {
    std::atomic<uint32_t> seq;
    uint64_t unit;
    uint32_t bullets;     // before the shot
    uint32_t snapLen;
    uint8_t snap[kSnapBytes];
};
static ShotRec g_ring[kRingSize];
static std::atomic<uint32_t> g_head{ 0 };
static HANDLE g_wake;
static std::atomic<uint32_t> g_hookHits{ 0 };

// No C++ objects here: __try needs a frame without unwinding.
static uint32_t SafeCopy(uint8_t* dst, const void* src, uint32_t len) {
    __try { memcpy(dst, src, len); return len; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Called from the hook stub on the game thread. Keep it short.
static void __fastcall OnShot(uint64_t unit, uint32_t bullets) {
    g_hookHits.fetch_add(1, std::memory_order_relaxed);
    const uint32_t idx = g_head.fetch_add(1, std::memory_order_acq_rel);
    ShotRec& r = g_ring[idx % kRingSize];
    r.unit = unit;
    r.bullets = bullets & 0xFFFF;
    r.snapLen = unit ? SafeCopy(r.snap, (const void*)unit, kSnapBytes) : 0;
    r.seq.store(idx + 1, std::memory_order_release);
    SetEvent(g_wake);
}

// ───────────────────────────── who fired? ─────────────────────────────
// Battle units are game objects; the offset of the unit id in them is not public.
// "auto" learns it: an id field must hold 1-10 in every shooter, stay constant for
// the same unit, and separate at least 3 different shooters. Once exactly one
// 2-byte field fits, it is used and saved as [Character] LearnedOffset.
constexpr int kMaxUnits = 48;
constexpr int kSlots = kSnapBytes / 2;
struct UnitSeen { uint64_t addr; uint16_t v[kSlots]; bool valid; };
static UnitSeen g_units[kMaxUnits];
static int g_unitCount = 0, g_unitNext = 0;
static bool g_dead[kSlots];          // offsets ruled out for good
static bool g_learnDone = false;

static uint16_t U16(const ShotRec& r, int off) { uint16_t v; memcpy(&v, r.snap + off, 2); return v; }

static void DumpUnit(const ShotRec& r) {
    Log("unit 0x%llX dump (first 0x%X bytes):", (unsigned long long)r.unit, g_cfg.dumpBytes);
    char line[128];
    for (int off = 0; off < g_cfg.dumpBytes && off < (int)r.snapLen; off += 16) {
        int n = _snprintf_s(line, sizeof line, _TRUNCATE, "  +%03X:", off);
        for (int k = 0; k < 16 && off + k < (int)r.snapLen; ++k)
            n += _snprintf_s(line + n, sizeof line - n, _TRUNCATE, " %02X", r.snap[off + k]);
        Log("%s", line);
    }
}

static void SaveLearned(int off) {
    wchar_t v[32]; swprintf_s(v, L"0x%X", off);
    WritePrivateProfileStringW(L"Character", L"LearnedOffset", v, g_ini);
}

// Returns the unit id (1-10) or 0 when unknown.
static int Identify(const ShotRec& r) {
    if (g_cfg.idMode == 2 || r.snapLen < kSnapBytes) return 0;

    // Remember this unit (first time seen -> optional dump).
    int u = -1;
    for (int i = 0; i < g_unitCount; ++i) if (g_units[i].addr == r.unit) { u = i; break; }
    if (u < 0) {
        u = g_unitCount < kMaxUnits ? g_unitCount++ : (g_unitNext++ % kMaxUnits);
        g_units[u].addr = r.unit; g_units[u].valid = true;
        for (int s = 0; s < kSlots; ++s) g_units[u].v[s] = U16(r, s * 2);
        if (g_cfg.dumpUnits) DumpUnit(r);
    } else {
        // Same unit again: a real id field cannot have changed.
        for (int s = 0; s < kSlots; ++s) if (g_units[u].v[s] != U16(r, s * 2)) g_dead[s] = true;
    }

    const int off = g_cfg.idOffset;
    if (off >= 0 && off + 2 <= kSnapBytes) {
        int id = U16(r, off);
        return id >= 1 && id <= 10 ? id : 0;
    }
    if (g_cfg.idMode != 0 || g_learnDone) return 0;

    // Learn: which 2-byte fields look like a unit id across every shooter seen so far?
    // Fields holding the same values for every unit (copies of the id) count as one candidate.
    int candidates = 0, pick = -1, pickDistinct = 0;
    int firstOfGroup[kSlots];
    for (int s = 0; s < kSlots; ++s) {
        firstOfGroup[s] = -1;
        if (g_dead[s]) continue;
        bool seenVal[11] = {}; int distinct = 0; bool fits = true;
        for (int i = 0; i < g_unitCount; ++i) {
            uint16_t v = g_units[i].v[s];
            if (v < 1 || v > 10) { fits = false; break; }
            if (!seenVal[v]) { seenVal[v] = true; ++distinct; }
        }
        if (!fits) { g_dead[s] = true; continue; }
        if (distinct < 2) continue;
        for (int t = 0; t < s && firstOfGroup[s] < 0; ++t) {
            if (firstOfGroup[t] != t) continue;
            bool same = true;
            for (int i = 0; i < g_unitCount && same; ++i) same = g_units[i].v[t] == g_units[i].v[s];
            if (same) firstOfGroup[s] = t;
        }
        if (firstOfGroup[s] >= 0) continue;   // a copy of an earlier candidate
        firstOfGroup[s] = s;
        ++candidates;
        if (distinct > pickDistinct) { pick = s; pickDistinct = distinct; }
    }
    if (candidates == 1 && pickDistinct >= 3) {
        g_cfg.idOffset = pick * 2; g_learnDone = true;
        SaveLearned(g_cfg.idOffset);
        Log("LEARNED unit id offset +0x%X (%d different shooters) - saved as [Character] LearnedOffset", g_cfg.idOffset, pickDistinct);
        int id = U16(r, g_cfg.idOffset);
        return id >= 1 && id <= 10 ? id : 0;
    }
    if (g_cfg.logShots) {
        char list[256] = {}; int n = 0, shown = 0;
        for (int s = 0; s < kSlots && shown < 12; ++s) {
            if (g_dead[s] || firstOfGroup[s] != s) continue;
            n += _snprintf_s(list + n, sizeof list - n, _TRUNCATE, " +%X", s * 2); ++shown;
        }
        Log("learning: %d unit(s) seen, %d candidate field(s) with 2+ values:%s", g_unitCount, candidates, list);
    }
    return 0;
}

// ───────────────────────────── hook install ─────────────────────────────
static uint8_t* AllocNear(uintptr_t site, size_t size) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    const uintptr_t start = site & ~(gran - 1);
    for (uintptr_t d = gran; d < 0x7FF00000; d += gran) {
        if (start > d) if (void* p = VirtualAlloc((void*)(start - d), size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return (uint8_t*)p;
        if (void* p = VirtualAlloc((void*)(start + d), size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return (uint8_t*)p;
    }
    return nullptr;
}

static int ScanAndHook() {
    auto* base = (uint8_t*)GetModuleHandleW(nullptr);
    auto* dos = (IMAGE_DOS_HEADER*)base;
    auto* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    size_t hits[8]; int hooked = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* s = base + sec[i].VirtualAddress;
        size_t n = hook::FindAll(s, sec[i].Misc.VirtualSize, hook::kShotPattern, hook::kShotPatternLen, hits, 8);
        for (size_t k = 0; k < n; ++k) {
            uint8_t* site = s + hits[k];
            uint8_t* stub = AllocNear((uintptr_t)site, 4096);
            if (!stub) { Log("ERROR: no memory near the hook site"); continue; }
            uint8_t original[hook::kSiteLen]; memcpy(original, site, hook::kSiteLen);
            hook::BuildStub(stub, original, (uint64_t)(uintptr_t)&OnShot, (uint64_t)(uintptr_t)(site + hook::kSiteLen));
            FlushInstructionCache(GetCurrentProcess(), stub, hook::kStubMax);
            uint8_t patch[hook::kSiteLen];
            if (!hook::BuildSitePatch(patch, (uint64_t)(uintptr_t)site, (uint64_t)(uintptr_t)stub)) { Log("ERROR: stub out of range"); continue; }
            DWORD old;
            VirtualProtect(site, hook::kSiteLen, PAGE_EXECUTE_READWRITE, &old);
            memcpy(site, patch, hook::kSiteLen);
            VirtualProtect(site, hook::kSiteLen, old, &old);
            FlushInstructionCache(GetCurrentProcess(), site, hook::kSiteLen);
            Log("hooked gun shot at P5R.exe+0x%llX", (unsigned long long)(site - base));
            ++hooked;
        }
    }
    return hooked;
}

// The executable may still be packed when Reloaded-II starts us: poll until the code shows up.
static DWORD WINAPI HookThread(LPVOID) {
    for (int i = 0; i < 240 && !g_quit.load(); ++i) {   // up to 2 minutes
        int n = 0;
        __try { n = ScanAndHook(); } __except (EXCEPTION_EXECUTE_HANDLER) { n = 0; }
        if (n > 0) { Log("ready: %d hook(s). Fire a gun in battle.", n); return 0; }
        Sleep(500);
    }
    if (!g_quit.load())
        Log("ERROR: gun shot code not found (different game version, or another mod/cheat already patched it). Nothing will happen.");
    return 0;
}

// ───────────────────────────── worker ─────────────────────────────
static bool GameFocused() {
    HWND fg = GetForegroundWindow(); DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static DWORD WINAPI WorkerThread(LPVOID) {
    enum { Idle, Kick, Rest } state = Idle;
    const Profile* cur = nullptr;
    DWORD kickEnd = 0, restEnd = 0, lastSend = 0;
    uint32_t tail = 0;
    int testIdx = 0;
    bool prevF9 = false, prevF10 = false;

    SendEffect(ds::Off());   // clear anything left over

    while (!g_quit.load()) {
        WaitForSingleObject(g_wake, 15);
        const DWORD now = GetTickCount();
        const bool focused = GameFocused();

        // 1) shots
        const uint32_t head = g_head.load(std::memory_order_acquire);
        if (head - tail > kRingSize) tail = head - kRingSize;
        while (tail != head) {
            ShotRec& r = g_ring[tail % kRingSize];
            if (r.seq.load(std::memory_order_acquire) != tail + 1) break;  // still being written
            int id = Identify(r);
            const Profile& p = g_profiles[id];
            if (g_cfg.logShots)
                Log("SHOT unit=0x%llX bullets %u->%u shooter=%s", (unsigned long long)r.unit, r.bullets,
                    r.bullets ? r.bullets - 1 : 0, id ? kSection[id] : "unknown (Default)");
            if (g_cfg.enabled && (focused || !g_cfg.muteUnfocused)) {
                cur = &p; state = Kick; kickEnd = now + p.kickMs; restEnd = now + p.kickMs + g_cfg.holdMs;
                SendEffect(p.kick); lastSend = now;
            }
            ++tail;
        }

        // 2) test hotkeys: F9 = next character's effect, F10 = reload INI
        if (g_cfg.hotkeys && focused) {
            bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0, f10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
            if (f9 && !prevF9) {
                testIdx = testIdx % 10 + 1;
                Log("TEST %s", kSection[testIdx]);
                cur = &g_profiles[testIdx]; state = Kick;
                kickEnd = now + cur->kickMs; restEnd = now + cur->kickMs + g_cfg.holdMs;
                SendEffect(cur->kick); lastSend = now;
            }
            if (f10 && !prevF10) { LoadConfig(); Log("INI reloaded"); }
            prevF9 = f9; prevF10 = f10;
        }

        // 3) timeline
        if (state != Idle && g_cfg.muteUnfocused && !focused) { SendEffect(ds::Off()); state = Idle; cur = nullptr; }
        if (state == Kick && (int)(now - kickEnd) >= 0) { state = Rest; SendEffect(cur->rest); lastSend = now; }
        if (state == Rest && (int)(now - restEnd) >= 0) { state = Idle; SendEffect(ds::Off()); cur = nullptr; }
        if (state != Idle && g_cfg.refreshMs > 0 && now - lastSend >= (DWORD)g_cfg.refreshMs) {
            SendEffect(state == Kick ? cur->kick : cur->rest); lastSend = now;
        }
    }
    SendEffect(ds::Off());
    return 0;
}

// ───────────────────────────── entry ─────────────────────────────
static void SetPaths() {
    GetModuleFileNameW(g_self, g_dir, MAX_PATH);
    if (wchar_t* s = wcsrchr(g_dir, L'\\')) *s = 0;
    swprintf_s(g_ini, L"%s\\P5RGunTrigger.ini", g_dir);
    swprintf_s(g_logPath, L"%s\\P5RGunTrigger.log", g_dir);
}

static DWORD WINAPI InitThread(LPVOID) {
    SetPaths();
    DeleteFileW(g_logPath);  // one log per run
    Log("P5R Gun Adaptive Trigger " MOD_VERSION " starting");
    LoadConfig();
    if (!g_cfg.enabled) { Log("disabled in INI ([General] Enabled=0)"); return 0; }
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CloseHandle(CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr));
    CloseHandle(CreateThread(nullptr, 0, HookThread, nullptr, 0, nullptr));
    return 0;
}

extern "C" __declspec(dllexport) void ReloadedStart() {
    static std::atomic<bool> started{ false };
    if (started.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr));
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) { g_self = h; DisableThreadLibraryCalls(h); }
    else if (reason == DLL_PROCESS_DETACH) {
        g_quit = true;
        // Game is closing: the other threads are already gone, so release the trigger here (best effort).
        if (reserved && g_dev != INVALID_HANDLE_VALUE && g_ioEvent) SendEffect(ds::Off());
    }
    return TRUE;
}
