// DualSense voice-coil haptics: a per-character recoil waveform on every shot.
// ---------------------------------------------------------------------------
// Over USB the DualSense is also a 4-channel audio device; channels 3/4 drive
// the left/right voice coils. A small synth renders short sine layers there
// (same engine as SMTVV DualSense Haptic). Bluetooth has no such channel, so
// haptics only work over USB; the adaptive triggers work either way.
// ---------------------------------------------------------------------------
#include "common.h"
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <cmath>
#include <cstring>
#include <cwchar>

#pragma comment(lib, "ole32.lib")

static HapticConfig g_hcfg;

// A pattern = up to 8 short sine layers (f0 -> f1 sweep) with a fast attack and an exponential decay.
// Right side is a bit stronger: the gun hand / the trigger that fires.
struct Layer { float f0, f1, startMs, lenMs, ampL, ampR; };
struct Pattern { const char* name; int n; Layer layers[8]; };

static const Pattern kPatterns[11] = {
    //            f0   f1   start len   L      R
    { "Default", 2, { { 210, 200,  0,  12, 0.60f, 0.85f }, {  75,  60,   0,  55, 0.70f, 0.90f } } },
    { "Joker",   2, { { 210, 200,  0,  12, 0.60f, 0.85f }, {  75,  60,   0,  55, 0.70f, 0.90f } } },            // handgun: tight tap
    { "Skull",   3, { { 230, 210,  0,  15, 0.80f, 1.00f }, {  45,  34,   5, 170, 1.00f, 1.00f },
                      {  30,  28,  60, 260, 0.50f, 0.55f } } },                                                // shotgun: crack + big boom
    { "Mona",    2, { { 320, 260,  0,  10, 0.45f, 0.60f }, { 130,  95,   4,  35, 0.35f, 0.45f } } },            // slingshot: light snap
    { "Panther", 6, { { 180, 170,  0,  10, 0.55f, 0.75f }, {  80,  70,   0,  35, 0.55f, 0.70f },
                      { 180, 170, 60,  10, 0.55f, 0.75f }, {  80,  70,  60,  35, 0.55f, 0.70f },
                      { 180, 170, 120, 10, 0.55f, 0.75f }, {  80,  70, 120,  45, 0.60f, 0.75f } } },           // SMG: rattling burst
    { "Fox",     2, { { 260, 240,  0,  12, 0.70f, 0.95f }, {  55,  44,   3, 125, 0.85f, 1.00f } } },            // rifle: sharp crack, deep tail
    { "Queen",   2, { { 230, 215,  0,  14, 0.70f, 0.95f }, {  60,  48,   3, 110, 0.80f, 0.95f } } },            // revolver
    { "Noir",    3, { {  85,  70,   0,  30, 0.70f, 0.80f }, {  40,  30,   0, 260, 1.00f, 1.00f },
                      {  36,  32, 190, 200, 0.70f, 0.70f } } },                                                // grenade launcher: thunk + blast
    { "Oracle",  1, { { 150, 140,  0,  40, 0.30f, 0.30f } } },
    { "Crow",    2, { { 420, 240,  0, 150, 0.40f, 0.55f }, { 180, 160,   0, 150, 0.30f, 0.40f } } },            // ray gun: falling electric whine
    { "Violet",  2, { { 220, 205,  0,  12, 0.60f, 0.85f }, {  70,  58,   0,  60, 0.70f, 0.90f } } },
};

struct Voice { int pattern; double t; float gain; bool live; };
static SRWLOCK g_vLock = SRWLOCK_INIT;
static Voice g_pending[16]; static int g_pendingCount;
static std::atomic<bool> g_active{ false };

void Haptics_Configure(const HapticConfig& c) { g_hcfg = c; }

void Haptics_Play(int character, double gain) {
    if (!g_hcfg.enabled || character < 0 || character > 10) return;
    AcquireSRWLockExclusive(&g_vLock);
    if (g_pendingCount < 16) g_pending[g_pendingCount++] = Voice{ character, 0.0, (float)gain, true };
    ReleaseSRWLockExclusive(&g_vLock);
}

static float Env(double tMs, double lenMs) {
    if (tMs < 0 || tMs >= lenMs) return 0.f;
    const double atk = 1.5;
    if (tMs < atk) return (float)(tMs / atk);
    double e = std::exp(-4.6 * (tMs - atk) / (lenMs - atk));
    if (lenMs - tMs < 3.0) e *= (lenMs - tMs) / 3.0;
    return (float)e;
}

struct Synth {
    double sr = 48000;
    Voice voices[16]{};

    void Pull() {
        AcquireSRWLockExclusive(&g_vLock);
        for (int i = 0; i < g_pendingCount; ++i)
            for (auto& v : voices) if (!v.live) { v = g_pending[i]; break; }
        g_pendingCount = 0;
        ReleaseSRWLockExclusive(&g_vLock);
    }

    void Render(float* outL, float* outR, UINT frames, bool silent) {
        if (silent) for (auto& v : voices) v.live = false;
        const float g = (float)g_hcfg.gain;
        for (UINT i = 0; i < frames; ++i) {
            float L = 0, R = 0;
            for (auto& v : voices) {
                if (!v.live) continue;
                const Pattern& p = kPatterns[v.pattern];
                const double tMs = v.t * 1000.0 / sr;
                double maxEnd = 0;
                for (int k = 0; k < p.n; ++k) {
                    const Layer& ly = p.layers[k];
                    const double lt = tMs - ly.startMs;
                    if (ly.startMs + ly.lenMs > maxEnd) maxEnd = ly.startMs + ly.lenMs;
                    const float e = Env(lt, ly.lenMs);
                    if (e <= 0) continue;
                    const double ts = lt / 1000.0, lenS = ly.lenMs / 1000.0;
                    const double ph = 2 * 3.14159265358979 * (ly.f0 * ts + (ly.f1 - ly.f0) * ts * ts / (2 * lenS));
                    const float s = (float)std::sin(ph) * e * v.gain;
                    L += s * ly.ampL; R += s * ly.ampR;
                }
                v.t += 1;
                if (tMs > maxEnd) v.live = false;
            }
            L *= g; R *= g;
            float l = (float)std::tanh(L * 1.2) / 0.834f * 0.98f, r = (float)std::tanh(R * 1.2) / 0.834f * 0.98f;
            outL[i] = l > 1 ? 1 : l < -1 ? -1 : l;
            outR[i] = r > 1 ? 1 : r < -1 ? -1 : r;
        }
    }
};

enum SampleFmt { SF_F32, SF_I16, SF_I24, SF_I32 };

static bool DetectFormat(const WAVEFORMATEX* wf, SampleFmt& sf) {
    bool isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT, isPcm = wf->wFormatTag == WAVE_FORMAT_PCM;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto ext = (const WAVEFORMATEXTENSIBLE*)wf;
        isFloat = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
        isPcm = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) != 0;
    }
    if (isFloat && wf->wBitsPerSample == 32) { sf = SF_F32; return true; }
    if (isPcm && wf->wBitsPerSample == 16) { sf = SF_I16; return true; }
    if (isPcm && wf->wBitsPerSample == 24) { sf = SF_I24; return true; }
    if (isPcm && wf->wBitsPerSample == 32) { sf = SF_I32; return true; }
    return false;
}

static void WriteFrames(BYTE* dst, const WAVEFORMATEX* wf, SampleFmt sf, const float* L, const float* R, UINT frames) {
    const int ch = wf->nChannels, bytes = wf->wBitsPerSample / 8;
    memset(dst, 0, (size_t)frames * wf->nBlockAlign);
    for (UINT i = 0; i < frames; ++i) {
        BYTE* frame = dst + (size_t)i * wf->nBlockAlign;
        for (int side = 0; side < 2; ++side) {
            const int c = side ? g_hcfg.rightChannel : g_hcfg.leftChannel;
            if (c < 0 || c >= ch) continue;
            const float s = side ? R[i] : L[i];
            BYTE* p = frame + c * bytes;
            switch (sf) {
            case SF_F32: *(float*)p = s; break;
            case SF_I16: *(short*)p = (short)(s * 32767.0f); break;
            case SF_I24: { int v = (int)(s * 8388607.0f); p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); p[2] = (BYTE)(v >> 16); } break;
            case SF_I32: *(int*)p = (int)(s * 2147483647.0); break;
            }
        }
    }
}

static bool NameMatches(const wchar_t* name) {
    return name && (wcsstr(name, g_hcfg.deviceName) || wcsstr(name, L"DualSense"));
}

static IMMDevice* FindDualSense(IMMDeviceEnumerator* en) {
    IMMDeviceCollection* col = nullptr;
    if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col))) return nullptr;
    UINT n = 0; col->GetCount(&n);
    IMMDevice* found = nullptr;
    for (UINT i = 0; i < n && !found; ++i) {
        IMMDevice* d = nullptr;
        if (FAILED(col->Item(i, &d))) continue;
        IPropertyStore* ps = nullptr;
        if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps))) {
            PROPVARIANT v; PropVariantInit(&v);
            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && NameMatches(v.pwszVal)) {
                char nm[256]; WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, nm, 256, nullptr, nullptr);
                Log("haptics: audio endpoint %s", nm);
                found = d; d = nullptr;
            }
            PropVariantClear(&v);
            ps->Release();
        }
        if (d) d->Release();
    }
    col->Release();
    return found;
}

static void RunDevice(IMMDevice* dev, Synth& syn) {
    IAudioClient* ac = nullptr; IAudioRenderClient* rc = nullptr; WAVEFORMATEX* wf = nullptr;
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    float* bufL = nullptr; float* bufR = nullptr;
    SampleFmt sf;
    UINT32 bufFrames = 0;
    HRESULT hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac);
    if (FAILED(hr)) { Log("haptics: Activate failed 0x%08lX", hr); goto done; }
    if (FAILED(hr = ac->GetMixFormat(&wf))) { Log("haptics: GetMixFormat failed 0x%08lX", hr); goto done; }
    if (wf->nChannels < 4) { Log("haptics: endpoint has %u channels, need 4 (USB cable required)", wf->nChannels); goto done; }
    if (!DetectFormat(wf, sf)) { Log("haptics: unsupported sample format"); goto done; }
    hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, (REFERENCE_TIME)g_hcfg.bufferMs * 10000, 0, wf, nullptr);
    if (FAILED(hr)) { Log("haptics: Initialize failed 0x%08lX", hr); goto done; }
    ac->SetEventHandle(ev);
    ac->GetBufferSize(&bufFrames);
    if (FAILED(hr = ac->GetService(__uuidof(IAudioRenderClient), (void**)&rc))) { Log("haptics: GetService failed 0x%08lX", hr); goto done; }
    bufL = new float[bufFrames]; bufR = new float[bufFrames];
    syn.sr = wf->nSamplesPerSec;
    if (FAILED(hr = ac->Start())) { Log("haptics: Start failed 0x%08lX", hr); goto done; }
    g_active = true;
    Log("HAPTICS ACTIVE (%u ch, %lu Hz, buffer %u frames)", wf->nChannels, wf->nSamplesPerSec, bufFrames);
    while (!g_quit.load()) {
        WaitForSingleObject(ev, 200);
        UINT32 pad = 0;
        if (FAILED(hr = ac->GetCurrentPadding(&pad))) { Log("haptics: device lost 0x%08lX", hr); break; }
        const UINT32 n = bufFrames - pad;
        if (!n) continue;
        BYTE* data = nullptr;
        if (FAILED(hr = rc->GetBuffer(n, &data))) { Log("haptics: GetBuffer failed 0x%08lX", hr); break; }
        syn.Pull();
        const bool silent = !g_hcfg.enabled || (g_hcfg.muteUnfocused && !GameFocused());
        syn.Render(bufL, bufR, n, silent);
        WriteFrames(data, wf, sf, bufL, bufR, n);
        rc->ReleaseBuffer(n, 0);
    }
    ac->Stop();
done:
    g_active = false;
    delete[] bufL; delete[] bufR;
    if (rc) rc->Release();
    if (wf) CoTaskMemFree(wf);
    if (ac) ac->Release();
    CloseHandle(ev);
}

static DWORD WINAPI AudioThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))) {
        Log("haptics: MMDeviceEnumerator unavailable");
        return 0;
    }
    Synth syn;
    bool warned = false;
    while (!g_quit.load()) {
        if (IMMDevice* dev = FindDualSense(en)) { warned = false; RunDevice(dev, syn); dev->Release(); }
        else if (!warned) { Log("haptics: no DualSense audio endpoint (Bluetooth, or not plugged in) - triggers only; retrying"); warned = true; }
        for (int i = 0; i < 20 && !g_quit.load(); ++i) Sleep(100);
    }
    en->Release();
    CoUninitialize();
    return 0;
}

void Haptics_Start() {
    if (!g_hcfg.enabled) { Log("haptics: disabled ([Haptics] Enabled=0)"); return; }
    if (g_hcfg.bufferMs < 3) g_hcfg.bufferMs = 3;
    if (g_hcfg.bufferMs > 50) g_hcfg.bufferMs = 50;
    CloseHandle(CreateThread(nullptr, 0, AudioThread, nullptr, 0, nullptr));
}
