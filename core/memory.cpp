#include "memory.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace mem {

bool SafeCopy(void* dst, const void* src, size_t n) {
    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

Module MainModule() {
    HMODULE exe = GetModuleHandleW(nullptr);
    MODULEINFO info{};
    GetModuleInformation(GetCurrentProcess(), exe, &info, sizeof(info));
    return {reinterpret_cast<uintptr_t>(info.lpBaseOfDll), info.SizeOfImage};
}

namespace {

std::vector<int> ParsePattern(std::string_view p) {
    std::vector<int> bytes;
    for (size_t i = 0; i < p.size();) {
        if (p[i] == ' ') {
            ++i;
        } else if (p[i] == '?') {
            bytes.push_back(-1);
            while (i < p.size() && p[i] == '?') ++i;
        } else {
            bytes.push_back(std::stoi(std::string(p.substr(i, 2)), nullptr, 16));
            i += 2;
        }
    }
    return bytes;
}

bool IsReadable(const MEMORY_BASIC_INFORMATION& mbi) {
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return mbi.State == MEM_COMMIT && (mbi.Protect & kReadable) && !(mbi.Protect & PAGE_GUARD);
}

}  // namespace

uintptr_t FindPattern(const Module& m, std::string_view pattern) {
    const std::vector<int> pat = ParsePattern(pattern);
    if (pat.empty() || pat[0] < 0) return 0;

    // Walk region by region so an unreadable page in the image doesn't abort the scan.
    uintptr_t addr = m.base;
    const uintptr_t end = m.base + m.size;
    while (addr < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) break;
        const uintptr_t regionEnd = std::min(end, reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize);
        if (IsReadable(mbi) && regionEnd - addr >= pat.size()) {
            const auto* data = reinterpret_cast<const uint8_t*>(addr);
            const size_t len = regionEnd - addr - pat.size() + 1;
            for (size_t i = 0; i < len; ++i) {
                if (data[i] != pat[0]) continue;
                size_t j = 1;
                while (j < pat.size() && (pat[j] < 0 || data[i + j] == pat[j])) ++j;
                if (j == pat.size()) return addr + i;
            }
        }
        addr = regionEnd;
    }
    return 0;
}

uintptr_t ResolveRip(uintptr_t insn, int dispOffset, int insnLen) {
    auto disp = Read<int32_t>(insn + dispOffset);
    return disp ? insn + insnLen + *disp : 0;
}

}  // namespace mem
