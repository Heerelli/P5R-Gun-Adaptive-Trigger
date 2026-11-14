// Mid-function hook for the bullet-decrement instruction pair in P5R.exe.
// Pure C++ (no Windows headers): the byte builders are unit-tested in tests/test_effects.cpp.
//
// Game code at the hook site (one bullet is spent per gun shot):
//     66 FF C8                   dec  ax                  ; ax = bullets left before the shot
//     66 41 89 87 D0 79 00 00    mov  [r15+79D0h], ax     ; r15 = the shooting battle unit
// (Community-known AOB, used by the P5R cheat tables' "infinite bullets" since 2022.)
//
// The 11 bytes are replaced by "jmp stub" + 6 NOPs. The stub saves the volatile
// registers and flags, calls OnShot(r15, ax), restores everything, runs the two
// original instructions unchanged and jumps back.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <climits>
#include <initializer_list>

namespace hook {

// -1 = wildcard. Exact bytes: the original instructions are copied verbatim into the stub.
static const int kShotPattern[] = { 0x66, 0xFF, 0xC8, 0x66, 0x41, 0x89, 0x87, 0xD0, 0x79, 0x00, 0x00 };
constexpr size_t kShotPatternLen = sizeof(kShotPattern) / sizeof(kShotPattern[0]);
constexpr size_t kSiteLen = 11;      // bytes overwritten at the site
constexpr size_t kStubMax = 192;

inline bool MatchAt(const uint8_t* p, const int* pat, size_t len) {
    for (size_t i = 0; i < len; ++i)
        if (pat[i] >= 0 && p[i] != (uint8_t)pat[i]) return false;
    return true;
}

// Finds up to `max` matches in [data, data+size). Returns the count.
inline size_t FindAll(const uint8_t* data, size_t size, const int* pat, size_t len, size_t* offsets, size_t max) {
    size_t found = 0;
    if (size < len) return 0;
    for (size_t i = 0; i + len <= size && found < max; ++i)
        if (MatchAt(data + i, pat, len)) offsets[found++] = i;
    return found;
}

struct Emitter {
    uint8_t* p; size_t n = 0;
    void put(std::initializer_list<uint8_t> bytes) { for (uint8_t b : bytes) p[n++] = b; }
    void put64(uint64_t v) { memcpy(p + n, &v, 8); n += 8; }
    void putBytes(const uint8_t* src, size_t len) { memcpy(p + n, src, len); n += len; }
};

// Builds the stub. `original` = the kSiteLen bytes from the site, `handler` = void(__fastcall*)(uint64 unit, uint32 bulletsBefore),
// `returnTo` = site + kSiteLen. Returns the stub length (<= kStubMax).
inline size_t BuildStub(uint8_t* out, const uint8_t* original, uint64_t handler, uint64_t returnTo) {
    Emitter e{ out };
    e.put({ 0x9C });                         // pushfq
    e.put({ 0x50, 0x51, 0x52 });             // push rax, rcx, rdx
    e.put({ 0x41, 0x50, 0x41, 0x51 });       // push r8, r9
    e.put({ 0x41, 0x52, 0x41, 0x53 });       // push r10, r11
    e.put({ 0x55 });                         // push rbp
    e.put({ 0x48, 0x89, 0xE5 });             // mov  rbp, rsp
    e.put({ 0x48, 0x83, 0xE4, 0xF0 });       // and  rsp, -16
    e.put({ 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 }); // sub rsp, 80h  (20h shadow + 6 xmm)
    static const uint8_t xmmModrm[6] = { 0x44, 0x4C, 0x54, 0x5C, 0x64, 0x6C };
    for (int i = 0; i < 6; ++i) e.put({ 0xF3, 0x0F, 0x7F, xmmModrm[i], 0x24, (uint8_t)(0x20 + 0x10 * i) }); // movdqu [rsp+20h+i*10h], xmmI
    e.put({ 0x4C, 0x89, 0xF9 });             // mov  rcx, r15
    e.put({ 0x0F, 0xB7, 0xD0 });             // movzx edx, ax
    e.put({ 0x48, 0xB8 }); e.put64(handler); // mov  rax, handler
    e.put({ 0xFF, 0xD0 });                   // call rax
    for (int i = 0; i < 6; ++i) e.put({ 0xF3, 0x0F, 0x6F, xmmModrm[i], 0x24, (uint8_t)(0x20 + 0x10 * i) }); // movdqu xmmI, [rsp+...]
    e.put({ 0x48, 0x89, 0xEC });             // mov  rsp, rbp
    e.put({ 0x5D });                         // pop  rbp
    e.put({ 0x41, 0x5B, 0x41, 0x5A });       // pop  r11, r10
    e.put({ 0x41, 0x59, 0x41, 0x58 });       // pop  r9, r8
    e.put({ 0x5A, 0x59, 0x58 });             // pop  rdx, rcx, rax
    e.put({ 0x9D });                         // popfq
    e.putBytes(original, kSiteLen);          // dec ax / mov [r15+79D0h], ax  (original, unchanged)
    e.put({ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 }); e.put64(returnTo); // jmp [rip+0] -> site+11
    return e.n;
}

// The bytes written over the site: jmp rel32 to the stub, then NOPs. Returns false if out of rel32 range.
inline bool BuildSitePatch(uint8_t out[kSiteLen], uint64_t site, uint64_t stub) {
    const int64_t rel = (int64_t)stub - (int64_t)(site + 5);
    if (rel < INT32_MIN || rel > INT32_MAX) return false;
    out[0] = 0xE9;
    const int32_t r = (int32_t)rel;
    memcpy(out + 1, &r, 4);
    for (size_t i = 5; i < kSiteLen; ++i) out[i] = 0x90;
    return true;
}

} // namespace hook
