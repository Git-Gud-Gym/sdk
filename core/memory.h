#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mem {

// memcpy that returns false instead of crashing the game on a bad address.
bool SafeCopy(void* dst, const void* src, size_t n);

inline bool IsPlausiblePtr(uintptr_t p) {
    return p >= 0x10000 && p < 0x7FFF'FFFF'FFFF && (p & 7) == 0;
}

template <typename T>
std::optional<T> Read(uintptr_t addr) {
    T v{};
    if (!addr || !SafeCopy(&v, reinterpret_cast<const void*>(addr), sizeof(T))) return std::nullopt;
    return v;
}

// Reads a pointer and rejects values that can't be a heap object.
inline uintptr_t ReadPtr(uintptr_t addr) {
    auto p = Read<uintptr_t>(addr);
    return p && IsPlausiblePtr(*p) ? *p : 0;
}

template <typename T>
bool Write(uintptr_t addr, const T& v) {
    return addr && SafeCopy(reinterpret_cast<void*>(addr), &v, sizeof(T));
}

struct Module {
    uintptr_t base = 0;
    size_t size = 0;
};

Module MainModule();

// IDA-style pattern ("48 8B 05 ?? ?? ?? ??"); returns the match address or 0.
uintptr_t FindPattern(const Module& m, std::string_view pattern);

// Target of a RIP-relative operand: disp32 lives at insn+dispOffset, RIP = insn+insnLen.
uintptr_t ResolveRip(uintptr_t insn, int dispOffset, int insnLen);

}  // namespace mem
