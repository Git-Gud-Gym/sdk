// State shared between the in-game mod (gitgudgym.dll) and the desktop app (GitGudGym.exe)
// through a named memory mapping. The app writes settings and commands; the mod writes status and events.
#pragma once
#include <windows.h>
#include <sddl.h>

#include <cstdint>
#include <filesystem>
#include <new>
#include <string>

namespace ggg {

constexpr const wchar_t* kSharedName = L"Local\\GitGudGym.State.v1";
constexpr uint32_t kMagic = 0x31474747;  // "GGG1"
constexpr uint32_t kMaxEvents = 64;

enum class EventType : int32_t { Info, Warning, PlayerDeath, PlayerFell, BossDefeated, BossTracked, BossLost };
enum class Command : int32_t { None, NextBoss, NewSession, Unload, Probe };

// Gameplay toggles. Either side may flip them (app checkboxes, in-game hotkeys).
struct Settings {
    int32_t playerImmortal = 1;
    int32_t bossImmortal = 1;
    int32_t fallDeath = 1;
    float fallHeight = 20.0f;  // meters below the last solid ground that counts as a fatal fall
    int32_t passThrough = 0;   // let the boss die once; the mod clears it when used
};

struct Status {
    uint64_t heartbeat = 0;  // GetTickCount64() of the mod's last tick
    int32_t running = 0;
    int32_t playerLoaded = 0, playerHp = 0, playerMaxHp = 0;
    int32_t bossTracked = 0, bossHp = 0, bossMaxHp = 0, bossManual = 0;
    float bossDistance = 0;
    int32_t deaths = 0, defeats = 0, attempts = 0, falls = 0;
    int64_t damageDealt = 0, damageTaken = 0, healing = 0;
    int32_t hitsLanded = 0, hitsTaken = 0;
    float bestBossHpPct = 100, fastestDefeatSec = -1, avgAttemptSec = 0;
    int32_t attemptActive = 0, attemptIndex = 0;
    float attemptSec = 0, attemptBossLowPct = 100;
    int64_t attemptDealt = 0, attemptTaken = 0;
    int32_t attemptHitsLanded = 0, attemptHitsTaken = 0;
};

struct Event {
    uint32_t seq = 0;
    EventType type = EventType::Info;
    int32_t count = 0;       // death / defeat number for those events
    float attemptSec = 0;    // how long the attempt that just ended lasted
    char text[112] = {};
};

struct SharedState {
    uint32_t magic = kMagic;
    uint32_t size = sizeof(SharedState);
    Settings settings;
    uint32_t cmdSeq = 0;
    Command cmd = Command::None;
    uint32_t cmdAck = 0;
    Status status;
    uint32_t eventSeq = 0;  // number of events ever published; slot = (seq - 1) % kMaxEvents
    Event events[kMaxEvents];
};

template <typename T>
T Load(const T& field) {
    return *const_cast<const volatile T*>(&field);
}
template <typename T>
void Store(T& field, T value) {
    *const_cast<volatile T*>(&field) = value;
}

// Opens the shared block, creating and initializing it if needed. The DACL lets any integrity level
// open it, so a non-elevated app can still talk to the mod inside an elevated game.
inline SharedState* OpenShared(HANDLE& mapping, bool& created) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)", SDDL_REVISION_1, &sd,
                                                         nullptr);
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, sd ? &sa : nullptr, PAGE_READWRITE, 0, sizeof(SharedState),
                                 kSharedName);
    created = GetLastError() != ERROR_ALREADY_EXISTS;
    if (sd) LocalFree(sd);
    if (!mapping) return nullptr;

    auto* s = static_cast<SharedState*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
    if (s && (created || s->magic != kMagic || s->size != sizeof(SharedState))) {
        new (s) SharedState();
        created = true;
    }
    return s;
}

// Gameplay settings persist in the [settings] section of gitgudgym.ini so both sides start from the same values.
inline Settings ReadSettings(const std::filesystem::path& ini) {
    Settings s;
    auto num = [&](const wchar_t* key, double def) {
        wchar_t buf[64]{};
        GetPrivateProfileStringW(L"settings", key, L"", buf, 64, ini.c_str());
        return buf[0] ? _wtof(buf) : def;
    };
    s.playerImmortal = num(L"player_immortal", s.playerImmortal) != 0;
    s.bossImmortal = num(L"boss_immortal", s.bossImmortal) != 0;
    s.fallDeath = num(L"fall_death", s.fallDeath) != 0;
    s.fallHeight = static_cast<float>(num(L"fall_height", s.fallHeight));
    return s;
}

inline void WriteSettings(const std::filesystem::path& ini, const Settings& s) {
    auto put = [&](const wchar_t* key, const std::wstring& v) {
        WritePrivateProfileStringW(L"settings", key, v.c_str(), ini.c_str());
    };
    put(L"player_immortal", std::to_wstring(s.playerImmortal));
    put(L"boss_immortal", std::to_wstring(s.bossImmortal));
    put(L"fall_death", std::to_wstring(s.fallDeath));
    put(L"fall_height", std::to_wstring(static_cast<int>(s.fallHeight)));
}

}  // namespace ggg
