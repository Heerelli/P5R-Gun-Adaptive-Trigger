// Aim tracking and the "R2 fires while aiming" remap.
// Pure C++ (no Windows headers): unit-tested in tests/test_effects.cpp.
//
// P5R battle: D-pad Up = hold the gun (aim), Cross = fire, Circle = back, R2 = Baton Pass.
// While aiming, R2 is turned into Cross, so the trigger with the adaptive effect is the one that fires.
// Outside aiming R2 is passed through untouched (Baton Pass, dash in dungeons).
#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>

namespace input {

// Normalised pad state, whatever the source (XInput or DualSense HID report).
struct Pad {
    bool up = false, down = false, left = false, right = false;
    bool cross = false, circle = false, square = false, triangle = false;
    bool l1 = false, r1 = false, options = false;
    uint8_t l2 = 0, r2 = 0;        // analog 0-255
};

struct AimConfig {
    int fireThreshold = 170;       // R2 analog value that counts as "fire" (weapon effect gives way around here)
    int releaseThreshold = 120;    // R2 below this = released (hysteresis)
    uint32_t afterFireMs = 1500;   // keep aiming this long after the last fire release (repeat shots)
    uint32_t timeoutMs = 20000;    // give up if nothing happens
};

class AimTracker {
public:
    AimConfig cfg;

    // Feed the raw (un-remapped) pad. Returns true while aiming.
    bool Update(const Pad& p, uint32_t now) {
        const bool r2Fire = p.r2 >= cfg.fireThreshold || (r2Held_ && p.r2 >= cfg.releaseThreshold);
        const bool fire = p.cross || r2Fire;

        if (p.up && !prev_.up) Enter(now);                                    // "hold gun"
        if (aiming_) {
            const bool cancel = Rise(p.circle, prev_.circle) || Rise(p.triangle, prev_.triangle) ||
                Rise(p.square, prev_.square) || Rise(p.l1, prev_.l1) || Rise(p.r1, prev_.r1) ||
                Rise(p.options, prev_.options) || Rise(p.down, prev_.down);
            if (cancel) Leave();
            else {
                if (fire) { firing_ = true; exitAt_ = 0; lastActivity_ = now; }
                else if (firing_) { firing_ = false; exitAt_ = now + cfg.afterFireMs; }
                if (Rise(p.left, prev_.left) || Rise(p.right, prev_.right)) lastActivity_ = now;   // choosing a target
                if (exitAt_ && (int32_t)(now - exitAt_) >= 0) Leave();
                if (aiming_ && !firing_ && now - lastActivity_ >= cfg.timeoutMs) Leave();
            }
        }
        r2Held_ = aiming_ && r2Fire;
        // An R2 press that started as "fire" stays hidden until released, so it can never
        // turn into a Baton Pass when aiming ends while the trigger is still down.
        if (r2Held_) swallow_ = true;
        else if (p.r2 < cfg.releaseThreshold / 2) swallow_ = false;
        prev_ = p;
        return aiming_;
    }

    bool Aiming() const { return aiming_; }
    // True when R2 should be reported as Cross right now (call after Update).
    bool RemapR2() const { return r2Held_; }
    // True when R2 should be hidden from the game (reported released) without pressing Cross.
    bool SwallowR2() const { return swallow_ && !r2Held_; }
    // Bumped on every enter/leave so other threads can see a change.
    uint32_t Generation() const { return gen_; }

private:
    static bool Rise(bool now, bool before) { return now && !before; }
    void Enter(uint32_t now) { if (!aiming_) ++gen_; aiming_ = true; firing_ = false; exitAt_ = 0; lastActivity_ = now; }
    void Leave() { if (aiming_) ++gen_; aiming_ = false; firing_ = false; exitAt_ = 0; }

    Pad prev_;
    bool aiming_ = false, firing_ = false, r2Held_ = false, swallow_ = false;
    uint32_t exitAt_ = 0, lastActivity_ = 0, gen_ = 0;
};

// ── XInput ──────────────────────────────────────────────────────────────────
// Layout of XINPUT_GAMEPAD (no Windows headers needed).
struct XGamepad { uint16_t wButtons; uint8_t bLeftTrigger, bRightTrigger; int16_t sThumbLX, sThumbLY, sThumbRX, sThumbRY; };
enum : uint16_t {
    X_UP = 0x0001, X_DOWN = 0x0002, X_LEFT = 0x0004, X_RIGHT = 0x0008, X_START = 0x0010,
    X_LB = 0x0100, X_RB = 0x0200, X_A = 0x1000, X_B = 0x2000, X_X = 0x4000, X_Y = 0x8000,
};

inline Pad FromXInput(const XGamepad& g) {
    Pad p;
    p.up = g.wButtons & X_UP; p.down = g.wButtons & X_DOWN; p.left = g.wButtons & X_LEFT; p.right = g.wButtons & X_RIGHT;
    p.cross = g.wButtons & X_A; p.circle = g.wButtons & X_B; p.square = g.wButtons & X_X; p.triangle = g.wButtons & X_Y;
    p.l1 = g.wButtons & X_LB; p.r1 = g.wButtons & X_RB; p.options = g.wButtons & X_START;
    p.l2 = g.bLeftTrigger; p.r2 = g.bRightTrigger;
    return p;
}

inline void RemapXInput(XGamepad& g, bool toCross) { if (toCross) g.wButtons |= X_A; g.bRightTrigger = 0; }

// ── DualSense HID input reports ─────────────────────────────────────────────
// USB 0x01 (64 bytes) and Bluetooth 0x31 (78 bytes, CRC32 at the end) share one layout,
// Bluetooth shifted by one byte. Bluetooth "simple" 0x01 (10 bytes) is different.
struct DsLayout { int r2, l2, buttons0, buttons1; int crcLen; };   // crcLen: bytes covered by the CRC (0 = none)

inline bool DsLayoutFor(const uint8_t* r, size_t n, DsLayout& out) {
    if (n >= 64 && r[0] == 0x01) { out = { 6, 5, 8, 9, 0 }; return true; }          // USB full
    if (n >= 78 && r[0] == 0x31) { out = { 7, 6, 9, 10, 74 }; return true; }        // Bluetooth full
    if (n >= 10 && n < 64 && r[0] == 0x01) { out = { 9, 8, 5, 6, 0 }; return true; } // Bluetooth simple
    return false;
}

inline Pad FromDualSense(const uint8_t* r, const DsLayout& l) {
    Pad p;
    const uint8_t b0 = r[l.buttons0], b1 = r[l.buttons1], hat = b0 & 0x0F;
    p.up = hat == 0 || hat == 1 || hat == 7;
    p.right = hat == 1 || hat == 2 || hat == 3;
    p.down = hat == 3 || hat == 4 || hat == 5;
    p.left = hat == 5 || hat == 6 || hat == 7;
    p.square = b0 & 0x10; p.cross = b0 & 0x20; p.circle = b0 & 0x40; p.triangle = b0 & 0x80;
    p.l1 = b1 & 0x01; p.r1 = b1 & 0x02; p.options = b1 & 0x20;
    p.l2 = r[l.l2]; p.r2 = r[l.r2];
    return p;
}

inline uint32_t Crc32(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; ++i) { c ^= d[i]; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return c;
}

inline void RemapDualSense(uint8_t* r, const DsLayout& l, bool toCross) {
    if (toCross) r[l.buttons0] |= 0x20;  // Cross on
    r[l.buttons1] &= (uint8_t)~0x08; // R2 digital off
    r[l.r2] = 0;                     // R2 analog released
    if (l.crcLen) {                  // Bluetooth: input CRC is seeded with 0xA1
        const uint8_t seed = 0xA1;
        uint32_t c = ~Crc32(r, (size_t)l.crcLen, Crc32(&seed, 1));
        memcpy(r + l.crcLen, &c, 4);
    }
}

} // namespace input
