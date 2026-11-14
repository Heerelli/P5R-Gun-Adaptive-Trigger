// DualSense adaptive-trigger effects and output reports.
// Pure C++ (no Windows headers) so it can be unit-tested anywhere: see tests/test_effects.cpp.
//
// Effect encodings follow the factory effects documented by Nielk1's
// TriggerEffectGenerator (MIT) - the same 11-byte blocks DualSense games send.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cstdio>

namespace ds {

// One trigger effect = 11 bytes in the output report.
struct Effect {
    uint8_t b[11] = { 0x05 };   // 0x05 = "off" (reset)
};

inline Effect Off() { Effect e; return e; }

// Resistance from `position` (0-9) to the end, `strength` 1-8.
inline Effect Feedback(int position, int strength) {
    if (position < 0 || position > 9 || strength <= 0) return Off();
    if (strength > 8) strength = 8;
    uint32_t force = 0; uint16_t zones = 0;
    const uint32_t v = (uint32_t)(strength - 1) & 7;
    for (int i = position; i < 10; ++i) { force |= v << (3 * i); zones |= (uint16_t)(1u << i); }
    Effect e; e.b[0] = 0x21;
    e.b[1] = (uint8_t)zones; e.b[2] = (uint8_t)(zones >> 8);
    e.b[3] = (uint8_t)force; e.b[4] = (uint8_t)(force >> 8); e.b[5] = (uint8_t)(force >> 16); e.b[6] = (uint8_t)(force >> 24);
    return e;
}

// A "gun trigger": resistance between start (2-7) and end (start+1 - 8), then it gives way. strength 1-8.
inline Effect Weapon(int start, int end, int strength) {
    if (start < 2 || start > 7 || end <= start || end > 8 || strength <= 0) return Off();
    if (strength > 8) strength = 8;
    const uint16_t zones = (uint16_t)((1u << start) | (1u << end));
    Effect e; e.b[0] = 0x25;
    e.b[1] = (uint8_t)zones; e.b[2] = (uint8_t)(zones >> 8);
    e.b[3] = (uint8_t)(strength - 1);
    return e;
}

// Buzz from `position` (0-9) to the end. amplitude 1-8, frequency in Hz (1-255).
// Position 0 is felt even with the finger only resting on the trigger.
inline Effect Vibration(int position, int amplitude, int frequency) {
    if (position < 0 || position > 9 || amplitude <= 0 || frequency <= 0) return Off();
    if (amplitude > 8) amplitude = 8;
    if (frequency > 255) frequency = 255;
    uint32_t amp = 0; uint16_t zones = 0;
    const uint32_t v = (uint32_t)(amplitude - 1) & 7;
    for (int i = position; i < 10; ++i) { amp |= v << (3 * i); zones |= (uint16_t)(1u << i); }
    Effect e; e.b[0] = 0x26;
    e.b[1] = (uint8_t)zones; e.b[2] = (uint8_t)(zones >> 8);
    e.b[3] = (uint8_t)amp; e.b[4] = (uint8_t)(amp >> 8); e.b[5] = (uint8_t)(amp >> 16); e.b[6] = (uint8_t)(amp >> 24);
    e.b[9] = (uint8_t)frequency;
    return e;
}

// Alternating buzz between amplitudes A and B (0-7) in the zone start (0-8) .. end (start+1 - 9).
// frequency in Hz, period in 1/10 s units.
inline Effect Machine(int start, int end, int ampA, int ampB, int frequency, int period) {
    if (start < 0 || start > 8 || end <= start || end > 9 || frequency <= 0) return Off();
    ampA = ampA < 0 ? 0 : ampA > 7 ? 7 : ampA;
    ampB = ampB < 0 ? 0 : ampB > 7 ? 7 : ampB;
    if (frequency > 255) frequency = 255;
    period = period < 0 ? 0 : period > 255 ? 255 : period;
    const uint16_t zones = (uint16_t)((1u << start) | (1u << end));
    Effect e; e.b[0] = 0x27;
    e.b[1] = (uint8_t)zones; e.b[2] = (uint8_t)(zones >> 8);
    e.b[3] = (uint8_t)((ampA & 7) | ((ampB & 7) << 3));
    e.b[4] = (uint8_t)frequency;
    e.b[5] = (uint8_t)period;
    return e;
}

// Parses "off", "feedback P S", "weapon A B S", "vibration P A F", "machine A B X Y F T".
// Separators: spaces, commas or tabs. Returns false (and Off) on a bad line.
inline bool Parse(const char* text, Effect& out) {
    out = Off();
    if (!text) return false;
    while (*text && isspace((unsigned char)*text)) ++text;
    char name[16] = {}; int n = 0;
    while (*text && isalpha((unsigned char)*text) && n < 15) name[n++] = (char)tolower((unsigned char)*text++);
    int v[6] = {}; int count = 0;
    while (*text && count < 6) {
        while (*text && (isspace((unsigned char)*text) || *text == ',')) ++text;
        if (!*text || *text == ';' || *text == '#') break;
        char* endp = nullptr;
        long x = strtol(text, &endp, 10);
        if (endp == text) return false;
        v[count++] = (int)x; text = endp;
    }
    if (!strcmp(name, "off") || !strcmp(name, "none")) return true;
    if (!strcmp(name, "feedback") && count == 2) { out = Feedback(v[0], v[1]); return out.b[0] == 0x21; }
    if (!strcmp(name, "weapon") && count == 3) { out = Weapon(v[0], v[1], v[2]); return out.b[0] == 0x25; }
    if (!strcmp(name, "vibration") && count == 3) { out = Vibration(v[0], v[1], v[2]); return out.b[0] == 0x26; }
    if (!strcmp(name, "machine") && count == 6) { out = Machine(v[0], v[1], v[2], v[3], v[4], v[5]); return out.b[0] == 0x27; }
    return false;
}

inline void Describe(const Effect& e, char* buf, size_t size) {
    snprintf(buf, size, "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
        e.b[0], e.b[1], e.b[2], e.b[3], e.b[4], e.b[5], e.b[6], e.b[7], e.b[8], e.b[9], e.b[10]);
}

// ── output reports ──────────────────────────────────────────────────────────
// Only the trigger "valid" flags are set, so the controller keeps whatever the
// game or Steam set for rumble, lightbar, LEDs and audio.

constexpr int kUsbReportSize = 48;   // report 0x02
constexpr int kBtReportSize  = 78;   // report 0x31 (incl. CRC32)

constexpr uint8_t kFlagRightTrigger = 0x04;
constexpr uint8_t kFlagLeftTrigger  = 0x08;

inline uint32_t Crc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

// Common payload (what follows the report id on USB, or id+seq+tag on Bluetooth): 47 bytes.
inline void FillPayload(uint8_t* p, const Effect* right, const Effect* left) {
    memset(p, 0, 47);
    uint8_t flags = 0;
    if (right) { flags |= kFlagRightTrigger; memcpy(p + 10, right->b, 11); }
    if (left)  { flags |= kFlagLeftTrigger;  memcpy(p + 21, left->b, 11); }
    p[0] = flags;
}

// USB: [0]=0x02, payload from [1]. Right trigger at [11..21], left at [22..32].
inline void BuildUsb(uint8_t out[kUsbReportSize], const Effect* right, const Effect* left) {
    memset(out, 0, kUsbReportSize);
    out[0] = 0x02;
    FillPayload(out + 1, right, left);
}

// Bluetooth: [0]=0x31, [1]=sequence<<4, [2]=tag 0x10, payload from [3], CRC32 over 0xA2 + [0..73] at [74..77].
// (Same layout as Linux hid-playstation's dualsense_output_report_bt.)
inline void BuildBt(uint8_t out[kBtReportSize], const Effect* right, const Effect* left, uint8_t sequence) {
    memset(out, 0, kBtReportSize);
    out[0] = 0x31;
    out[1] = (uint8_t)((sequence & 0x0F) << 4);
    out[2] = 0x10;
    FillPayload(out + 3, right, left);
    const uint8_t seed = 0xA2;
    uint32_t crc = Crc32(&seed, 1);
    crc = ~Crc32(out, 74, crc);
    out[74] = (uint8_t)crc; out[75] = (uint8_t)(crc >> 8); out[76] = (uint8_t)(crc >> 16); out[77] = (uint8_t)(crc >> 24);
}

} // namespace ds
