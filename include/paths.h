// Where Git Gud Gym keeps its files. Shared by the app and the game modules.
//   %LOCALAPPDATA%\GitGudGym\settings.ini          app + gameplay settings
//   %LOCALAPPDATA%\GitGudGym\modules\<id>\          installed game modules
//   %LOCALAPPDATA%\GitGudGym\data\<id>\             stats\ and gitgudgym.log per game
#pragma once
#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <string>

namespace ggg::paths {

inline std::filesystem::path Root() {
    PWSTR local = nullptr;
    std::filesystem::path root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) root = local;
    CoTaskMemFree(local);
    root /= L"GitGudGym";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    return root;
}

inline std::filesystem::path SettingsIni() { return Root() / L"settings.ini"; }
inline std::filesystem::path ModulesDir() { return Root() / L"modules"; }

inline std::filesystem::path DataDir(const std::wstring& moduleId) {
    std::filesystem::path dir = Root() / L"data" / moduleId;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

}  // namespace ggg::paths
