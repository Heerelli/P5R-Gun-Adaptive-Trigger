// Host-side unit tests for the pure parts (effects.h, hookstub.h).
//   g++ -std=c++17 -I src tests/test_effects.cpp -o test && ./test
// tests/check_stub.sh also disassembles the generated stub with objdump.
#include "effects.h"
#include "hookstub.h"
#include <cassert>
#include <cstdio>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool Eq(const ds::Effect& e, std::initializer_list<int> want) {
    int i = 0; for (int w : want) { if (e.b[i] != (uint8_t)w) return false; ++i; }
    for (; i < 11; ++i) if (e.b[i] != 0) return false;
    return true;
}

int main(int argc, char** argv) {
    // Off
    CHECK(Eq(ds::Off(), { 0x05 }));
    // Feedback(0,8): all 10 zones, force 7 in every 3-bit slot = 0x3FFFFFFF
    CHECK(Eq(ds::Feedback(0, 8), { 0x21, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F }));
    // Feedback(2,4): zones 2..9 = 0x3FC, force 3 << 6.. = 0x36DB6DB6 & mask
    {
        uint32_t f = 0; for (int i = 2; i < 10; ++i) f |= 3u << (3 * i);
        ds::Effect e = ds::Feedback(2, 4);
        CHECK(e.b[0] == 0x21 && e.b[1] == 0xFC && e.b[2] == 0x03);
        CHECK(e.b[3] == (uint8_t)f && e.b[4] == (uint8_t)(f >> 8) && e.b[5] == (uint8_t)(f >> 16) && e.b[6] == (uint8_t)(f >> 24));
    }
    // Weapon(2,5,8): zones (1<<2)|(1<<5) = 0x24, strength-1 = 7
    CHECK(Eq(ds::Weapon(2, 5, 8), { 0x25, 0x24, 0x00, 0x07 }));
    CHECK(Eq(ds::Weapon(4, 8, 6), { 0x25, 0x10, 0x01, 0x05 }));
    CHECK(Eq(ds::Weapon(1, 5, 8), { 0x05 }));       // start < 2 -> off
    // Vibration(0,8,45): all zones, amp 7, freq at [9]
    CHECK(Eq(ds::Vibration(0, 8, 45), { 0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 45 }));
    // Machine(0,9,7,4,35,2)
    CHECK(Eq(ds::Machine(0, 9, 7, 4, 35, 2), { 0x27, 0x01, 0x02, 0x27, 35, 2 }));

    // Parser
    ds::Effect e;
    CHECK(ds::Parse("weapon 3 6 5", e) && Eq(e, { 0x25, 0x48, 0x00, 0x04 }));
    CHECK(ds::Parse("  Vibration 0, 8, 45 ; comment", e) && e.b[0] == 0x26 && e.b[9] == 45);
    CHECK(ds::Parse("off", e) && e.b[0] == 0x05);
    CHECK(!ds::Parse("weapon 9 9 9", e));
    CHECK(!ds::Parse("laser 1 2", e));

    // CRC32 check value
    CHECK(ds::Crc32((const uint8_t*)"123456789", 9) == ~0xCBF43926u);

    // USB report layout
    uint8_t usb[ds::kUsbReportSize];
    ds::Effect w = ds::Weapon(3, 6, 5);
    ds::BuildUsb(usb, &w, nullptr);
    CHECK(usb[0] == 0x02 && usb[1] == ds::kFlagRightTrigger && usb[2] == 0);
    CHECK(usb[11] == 0x25 && usb[12] == 0x48 && usb[14] == 0x04 && usb[22] == 0);
    ds::BuildUsb(usb, &w, &w);
    CHECK(usb[1] == (ds::kFlagRightTrigger | ds::kFlagLeftTrigger) && usb[22] == 0x25);

    // Bluetooth report layout + CRC
    uint8_t bt[ds::kBtReportSize];
    ds::BuildBt(bt, &w, nullptr, 3);
    CHECK(bt[0] == 0x31 && bt[1] == 0x30 && bt[2] == 0x10 && bt[3] == ds::kFlagRightTrigger && bt[13] == 0x25);
    {
        uint8_t seed = 0xA2; uint32_t c = ~ds::Crc32(bt, 74, ds::Crc32(&seed, 1));
        CHECK(bt[74] == (uint8_t)c && bt[77] == (uint8_t)(c >> 24));
    }

    // Pattern search
    uint8_t code[64] = {};
    const uint8_t site[11] = { 0x66, 0xFF, 0xC8, 0x66, 0x41, 0x89, 0x87, 0xD0, 0x79, 0x00, 0x00 };
    memcpy(code + 20, site, 11);
    size_t hits[4];
    CHECK(hook::FindAll(code, sizeof code, hook::kShotPattern, hook::kShotPatternLen, hits, 4) == 1 && hits[0] == 20);

    // Site patch
    uint8_t patch[11];
    CHECK(hook::BuildSitePatch(patch, 0x140001000, 0x140801000));
    CHECK(patch[0] == 0xE9 && patch[5] == 0x90 && patch[10] == 0x90);
    { int32_t rel; memcpy(&rel, patch + 1, 4); CHECK(rel == 0x800000 - 5); }
    CHECK(!hook::BuildSitePatch(patch, 0x140001000, 0x7FF000000000));

    // Stub: write it out for objdump (tests/check_stub.sh)
    uint8_t stub[hook::kStubMax];
    size_t n = hook::BuildStub(stub, site, 0x1122334455667788ull, 0x14000100Bull);
    CHECK(n <= hook::kStubMax);
    if (argc > 1) { FILE* f = fopen(argv[1], "wb"); fwrite(stub, 1, n, f); fclose(f); }

    printf(fails ? "%d test(s) FAILED\n" : "all tests passed\n", fails);
    return fails ? 1 : 0;
}
