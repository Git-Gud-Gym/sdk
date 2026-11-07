#pragma once
#include <windows.h>

#include <cwchar>
#include <filesystem>
#include <string>
#include <type_traits>

namespace ini {

inline std::wstring ReadStr(const std::filesystem::path& file, const wchar_t* section, const wchar_t* key) {
    wchar_t buf[512]{};
    GetPrivateProfileStringW(section, key, L"", buf, static_cast<DWORD>(std::size(buf)), file.c_str());
    return buf;
}

// Accepts decimal or 0x-prefixed hex; keeps the default when the key is missing or empty.
template <typename T>
void ReadNum(const std::filesystem::path& file, const wchar_t* section, const wchar_t* key, T& out) {
    std::wstring s = ReadStr(file, section, key);
    if (s.empty()) return;
    if constexpr (std::is_floating_point_v<T>) {
        out = static_cast<T>(std::wcstod(s.c_str(), nullptr));
    } else {
        out = static_cast<T>(std::wcstoll(s.c_str(), nullptr, 0));
    }
}

inline std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

}  // namespace ini
