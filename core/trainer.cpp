#include "trainer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "game_adapter.h"
#include "ini.h"
#include "paths.h"
#include "shared_state.h"
#include "stats.h"
#include "version.h"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using ggg::EventType;

namespace {

// Game-independent knobs, from the [general] and [boss] sections of the module's ini.
struct CoreConfig {
    int pollIntervalMs = 2;
    std::string sessionLabel;
    float bossSearchRadius = 80.0f;
    int bossMinMaxHp = 300;
};

CoreConfig LoadCoreConfig(const fs::path& file) {
    CoreConfig c;
    ini::ReadNum(file, L"general", L"poll_interval_ms", c.pollIntervalMs);
    c.sessionLabel = ini::ToUtf8(ini::ReadStr(file, L"general", L"session_label"));
    ini::ReadNum(file, L"boss", L"search_radius", c.bossSearchRadius);
    ini::ReadNum(file, L"boss", L"min_max_hp", c.bossMinMaxHp);
    if (c.pollIntervalMs < 1) c.pollIntervalMs = 1;
    return c;
}

// Talks to the desktop app through shared memory, and mirrors every event to gitgudgym.log.
class Link {
public:
    Link(const fs::path& logPath, const fs::path& ini) : logFile_(logPath, std::ios::app) {
        bool created = false;
        state_ = ggg::OpenShared(mapping_, created);
        if (!state_) state_ = &fallback_;  // keep working (hotkeys only) even if the mapping fails
        if (created) state_->settings = ggg::ReadSettings(ini);
    }

    ~Link() {
        ggg::Store(state_->status.running, 0);
        if (state_ != &fallback_) UnmapViewOfFile(state_);
        if (mapping_) CloseHandle(mapping_);
    }

    ggg::SharedState& state() { return *state_; }

    ggg::Settings Settings() const {
        const ggg::Settings& s = state_->settings;
        ggg::Settings out;
        out.playerImmortal = ggg::Load(s.playerImmortal);
        out.bossImmortal = ggg::Load(s.bossImmortal);
        out.fallDeath = ggg::Load(s.fallDeath);
        out.fallHeight = ggg::Load(s.fallHeight);
        out.passThrough = ggg::Load(s.passThrough);
        return out;
    }

    void Event(EventType type, int count, float attemptSec, const char* fmt, ...) {
        char text[sizeof(ggg::Event::text)];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(text, sizeof(text), fmt, args);
        va_end(args);

        const uint32_t seq = ggg::Load(state_->eventSeq) + 1;
        ggg::Event& e = state_->events[(seq - 1) % ggg::kMaxEvents];
        e.type = type;
        e.count = count;
        e.attemptSec = attemptSec;
        std::memcpy(e.text, text, sizeof(text));
        ggg::Store(e.seq, seq);
        MemoryBarrier();
        ggg::Store(state_->eventSeq, seq);

        SYSTEMTIME t;
        GetLocalTime(&t);
        char line[160];
        std::snprintf(line, sizeof(line), "[%02u:%02u:%02u] %s", t.wHour, t.wMinute, t.wSecond, text);
        logFile_ << line << '\n';
        logFile_.flush();
    }

    template <typename... Args>
    void Log(const char* fmt, Args... args) {
        Event(EventType::Info, 0, 0, fmt, args...);
    }

private:
    HANDLE mapping_ = nullptr;
    ggg::SharedState* state_ = nullptr;
    ggg::SharedState fallback_;
    std::ofstream logFile_;
};

bool Pressed(int vk) {
    static bool down[256]{};
    const bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool edge = now && !down[vk];
    down[vk] = now;
    return edge;
}

bool GameFocused() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

class Trainer {
public:
    Trainer(const CoreConfig& cfg, const GameAdapter& game, Link& link, const fs::path& statsDir)
        : cfg_(cfg), game_(game), link_(link), stats_(statsDir, cfg.sessionLabel) {}

    void Run() {
        auto lastPublish = Clock::time_point{};
        while (!unload_) {
            if (GameFocused()) HandleKeys();
            HandleCommand();
            settings_ = link_.Settings();
            Tick();
            if (Clock::now() - lastPublish > 33ms) {
                PublishStatus();
                lastPublish = Clock::now();
            }
            Sleep(cfg_.pollIntervalMs);
        }
        Shutdown();
    }

private:
    void HandleKeys() {
        ggg::Settings& s = link_.state().settings;
        if (Pressed(VK_F5)) {
            const int32_t on = !(settings_.playerImmortal || settings_.bossImmortal);
            ggg::Store(s.playerImmortal, on);
            ggg::Store(s.bossImmortal, on);
            link_.Log("Immortality %s", on ? "ON" : "OFF");
        }
        if (Pressed(VK_F6)) CycleBoss();
        if (Pressed(VK_F7)) ggg::Store(s.passThrough, static_cast<int32_t>(!settings_.passThrough));
        if (Pressed(VK_F8)) Probe();
        if (Pressed(VK_F9)) NewSession();
        if (Pressed(VK_F10)) unload_ = true;
    }

    void HandleCommand() {
        ggg::SharedState& st = link_.state();
        const uint32_t seq = ggg::Load(st.cmdSeq);
        if (seq == ggg::Load(st.cmdAck)) return;
        switch (ggg::Load(st.cmd)) {
            case ggg::Command::NextBoss: CycleBoss(); break;
            case ggg::Command::NewSession: NewSession(); break;
            case ggg::Command::Probe: Probe(); break;
            case ggg::Command::Unload: unload_ = true; break;
            default: break;
        }
        ggg::Store(st.cmdAck, seq);
    }

    void NewSession() {
        stats_.ResetSession();
        link_.Log("Session saved; new session started");
    }

    void Tick() {
        player_.reset();
        boss_.reset();
        uintptr_t p = game_.Player();
        auto ps = p ? game_.ReadChr(p) : std::nullopt;
        if (!ps) {
            // Loading screen or main menu: everything we tracked is gone.
            playerChr_ = 0;
            safe_.reset();
            if (bossChr_) DropBoss("player not loaded", true);
            return;
        }
        if (ps->chr != playerChr_) {
            playerChr_ = ps->chr;
            playerLastHp_ = ps->hp;
            safe_.reset();
            stableSamples_ = 0;
        }
        UpdatePlayer(*ps);
        player_ = ps;
        UpdateBoss(*ps);
    }

    // Remembers the last spot the player stood still on, vertically, for half a second.
    // That's solid ground: jump apexes and slopes never stay level that long.
    void TrackGround(const ChrState& ps) {
        if (!ps.hasPos) return;
        if (lastPos_ && Distance(*lastPos_, ps.pos) > 50.0f) {
            // Coordinates jumped (warp, or crossing a map tile boundary): old ground is meaningless.
            safe_.reset();
            stableSamples_ = 0;
        }
        lastPos_ = ps.pos;
        if (Clock::now() - lastSampleAt_ < 100ms) return;
        stableSamples_ = std::abs(ps.pos.y - lastSampleY_) < 0.15f ? stableSamples_ + 1 : 0;
        lastSampleY_ = ps.pos.y;
        lastSampleAt_ = Clock::now();
        if (stableSamples_ >= 5) safe_ = ps.pos;
    }

    void UpdatePlayer(ChrState& ps) {
        const bool immortal = settings_.playerImmortal != 0;
        game_.SetNoDead(ps, immortal, true);
        TrackGround(ps);

        const int prev = playerLastHp_;
        playerLastHp_ = ps.hp;
        if (!immortal || prev <= 0) return;

        // Right after a respawn the game may still apply landing damage from the fall; swallow it.
        if (Clock::now() < respawnGraceUntil_) {
            if (ps.hp < ps.maxHp) {
                game_.SetHp(ps, ps.maxHp);
                ps.hp = playerLastHp_ = ps.maxHp;
            }
            return;
        }

        // Only fights against a tracked boss count toward attempt stats.
        if (bossChr_ && ps.hp < prev) stats_.PlayerDamaged(prev - ps.hp);
        if (bossChr_ && ps.hp > prev) stats_.PlayerHealed(ps.hp - prev);

        const bool fellPastLimit =
            settings_.fallDeath && safe_ && ps.hasPos && ps.pos.y < safe_->y - settings_.fallHeight;
        const bool hpDepleted = ps.hp <= 1 && prev > 1;
        if (!fellPastLimit && !hpDepleted) return;

        // A fatal landing somewhere below solid ground is also a fall: don't leave the player down there.
        const bool fell = fellPastLimit || (settings_.fallDeath && safe_ && ps.hasPos && ps.pos.y < safe_->y - 5.0f);
        const float attemptSec = static_cast<float>(stats_.CurrentAttemptSec());
        stats_.PlayerDied(fell);
        if (fell) {
            game_.SetPosition(ps, {safe_->x, safe_->y + 0.2f, safe_->z});
            lastPos_ = safe_;
            respawnGraceUntil_ = Clock::now() + 1500ms;
        }
        if (ps.hp == 0 && !fell) {
            link_.Event(EventType::Warning, 0, 0,
                        "Player HP hit 0: the no-dead flag isn't taking effect. Run Probe and send the log.");
            return;
        }
        game_.SetHp(ps, ps.maxHp);
        ps.hp = playerLastHp_ = ps.maxHp;
        const int deaths = stats_.totals().deaths;
        if (fell) {
            link_.Event(EventType::PlayerFell, deaths, attemptSec, "Fell to your death (#%d). Back on solid ground.",
                        deaths);
        } else {
            link_.Event(EventType::PlayerDeath, deaths, attemptSec, "You died (#%d). HP refilled.", deaths);
        }
    }

    void UpdateBoss(const ChrState& player) {
        if (bossChr_ && !game_.IsLoaded(bossChr_)) DropBoss("boss unloaded", true);

        std::optional<ChrState> bs = bossChr_ ? game_.ReadChr(bossChr_) : std::nullopt;
        if (bossChr_ && !bs) DropBoss("boss unreadable", true);

        if (!bossChr_) {
            if (Clock::now() - lastScan_ > 250ms) {
                lastScan_ = Clock::now();
                auto candidates = Candidates(player);
                if (!candidates.empty()) SelectBoss(candidates.front());
            }
            return;
        }

        // Between attempts, keep following the best candidate so walking into an arena picks up its boss
        // instead of sticking to whatever big enemy was nearby first.
        if (!stats_.current() && Clock::now() - lastScan_ > 250ms) {
            lastScan_ = Clock::now();
            if (Distance(player, *bs) > cfg_.bossSearchRadius) {
                DropBoss("out of range", false);
                return;
            }
            if (!manualPick_) {
                auto candidates = Candidates(player);
                if (!candidates.empty() && candidates.front().chr != bossChr_) {
                    DropBoss(nullptr, false);
                    SelectBoss(candidates.front());
                    return;
                }
            }
        }

        const bool passThrough = settings_.passThrough != 0;
        const bool immortal = settings_.bossImmortal && !passThrough;
        game_.SetNoDead(*bs, immortal, false);

        const int prev = bossLastHp_;
        bossLastHp_ = bs->hp;
        if (prev > 0 && bs->hp < prev) stats_.BossDamaged(prev - bs->hp, bs->hp, bs->maxHp);

        if (passThrough && bs->hp == 0) {
            // A death-triggered phase change (e.g. Rennala, Radagon): let the next body get picked up,
            // and keep the current attempt running.
            ggg::Store(link_.state().settings.passThrough, 0);
            link_.Log("Boss died for real (pass-through). Looking for the next phase's body...");
            DropBoss(nullptr, false);
            return;
        }

        if (immortal && bs->hp <= 1 && prev > 1) {
            if (bs->hp == 1) {
                const float attemptSec = static_cast<float>(stats_.CurrentAttemptSec());
                stats_.BossDefeated();
                game_.SetHp(*bs, bs->maxHp);
                bs->hp = bossLastHp_ = bs->maxHp;
                const int defeats = stats_.totals().defeats;
                link_.Event(EventType::BossDefeated, defeats, attemptSec, "Boss defeated (#%d)! HP refilled.",
                            defeats);
            } else {
                link_.Event(EventType::Warning, 0, 0,
                            "Boss HP hit 0: the no-dead flag isn't taking effect. Run Probe and send the log.");
            }
        }
        boss_ = bs;
    }

    std::vector<ChrState> Candidates(const ChrState& player) const {
        std::vector<ChrState> out;
        for (uintptr_t chr : game_.Characters()) {
            if (chr == player.chr) continue;
            auto s = game_.ReadChr(chr);
            if (!s || s->hp <= 0 || s->maxHp < cfg_.bossMinMaxHp) continue;
            if (Distance(player, *s) > cfg_.bossSearchRadius) continue;
            out.push_back(*s);
        }
        std::sort(out.begin(), out.end(), [](const ChrState& a, const ChrState& b) {
            return a.maxHp != b.maxHp ? a.maxHp > b.maxHp : a.chr < b.chr;
        });
        return out;
    }

    void SelectBoss(const ChrState& s) {
        bossChr_ = s.chr;
        bossLastHp_ = s.hp;
        link_.Event(EventType::BossTracked, 0, 0, "Tracking boss: %d HP, %.0fm away", s.maxHp,
                    player_ ? Distance(*player_, s) : 0.0f);
    }

    void DropBoss(const char* why, bool abandonAttempt) {
        if (auto bs = game_.ReadChr(bossChr_)) game_.SetNoDead(*bs, false, false);
        if (abandonAttempt) stats_.Abandon();
        if (why) link_.Event(EventType::BossLost, 0, 0, "Stopped tracking boss (%s)", why);
        manualPick_ = false;
        bossChr_ = 0;
        bossLastHp_ = -1;
    }

    void CycleBoss() {
        if (!player_) return;
        auto c = Candidates(*player_);
        if (c.empty()) {
            link_.Log("No boss candidates within %.0fm", cfg_.bossSearchRadius);
            return;
        }
        auto it = std::find_if(c.begin(), c.end(), [&](const ChrState& s) { return s.chr == bossChr_; });
        const ChrState next = (it == c.end() || std::next(it) == c.end()) ? c.front() : *std::next(it);
        if (next.chr == bossChr_) {
            link_.Log("Only one boss candidate in range");
            return;
        }
        DropBoss(nullptr, true);
        SelectBoss(next);
        manualPick_ = true;
    }

    void Probe() {
        link_.Log("--- probe ---");
        uintptr_t p = game_.Player();
        auto ps = game_.ReadChr(p);
        for (const std::string& line : game_.Describe(ps ? &*ps : nullptr)) link_.Log("%s", line.c_str());
        link_.Log("Player %p", (void*)p);
        if (!ps) return;
        link_.Log("  hp %d/%d pos (%.1f, %.1f, %.1f)%s", ps->hp, ps->maxHp, ps->pos.x, ps->pos.y, ps->pos.z,
                  ps->hasPos ? "" : " UNREADABLE");
        if (safe_) link_.Log("  last solid ground (%.1f, %.1f, %.1f)", safe_->x, safe_->y, safe_->z);
        auto chars = game_.Characters();
        int readable = 0;
        for (uintptr_t c : chars) readable += game_.ReadChr(c).has_value();
        link_.Log("  character list: %zu entries, %d readable", chars.size(), readable);
        auto c = Candidates(*ps);
        for (size_t i = 0; i < c.size() && i < 5; ++i) {
            link_.Log("  candidate %zu: %p hp %d/%d dist %.1fm", i, (void*)c[i].chr, c[i].hp, c[i].maxHp,
                      Distance(*ps, c[i]));
        }
    }

    void PublishStatus() {
        ggg::Status s;
        s.heartbeat = GetTickCount64();
        s.running = 1;
        if (player_) {
            s.playerLoaded = 1;
            s.playerHp = player_->hp;
            s.playerMaxHp = player_->maxHp;
        }
        if (boss_) {
            s.bossTracked = 1;
            s.bossHp = boss_->hp;
            s.bossMaxHp = boss_->maxHp;
            s.bossManual = manualPick_;
            s.bossDistance = player_ ? Distance(*player_, *boss_) : 0;
        }
        const SessionTotals& t = stats_.totals();
        s.deaths = t.deaths;
        s.defeats = t.defeats;
        s.attempts = t.attempts;
        s.falls = t.falls;
        s.damageDealt = t.damageDealt;
        s.damageTaken = t.damageTaken;
        s.healing = t.healing;
        s.hitsLanded = t.hitsLanded;
        s.hitsTaken = t.hitsTaken;
        s.bestBossHpPct = t.bestBossHpPct;
        s.fastestDefeatSec = t.fastestDefeatSec ? static_cast<float>(*t.fastestDefeatSec) : -1.0f;
        s.avgAttemptSec = t.attempts ? static_cast<float>(t.totalAttemptSec / t.attempts) : 0.0f;
        if (const auto& a = stats_.current()) {
            s.attemptActive = 1;
            s.attemptIndex = a->index;
            s.attemptSec = static_cast<float>(stats_.CurrentAttemptSec());
            s.attemptBossLowPct = a->bossLowestHpPct;
            s.attemptDealt = a->damageDealt;
            s.attemptTaken = a->damageTaken;
            s.attemptHitsLanded = a->hitsLanded;
            s.attemptHitsTaken = a->hitsTaken;
        } else {
            s.attemptIndex = t.attempts + 1;
        }
        // Display-only data: a torn read on the app side just shows one stale frame.
        std::memcpy(static_cast<void*>(&link_.state().status), &s, sizeof(s));
    }

    void Shutdown() {
        if (auto ps = game_.ReadChr(game_.Player())) game_.SetNoDead(*ps, false, true);
        if (bossChr_) DropBoss(nullptr, true);
        stats_.FlushSession();
    }

    const CoreConfig& cfg_;
    const GameAdapter& game_;
    Link& link_;
    Stats stats_;
    ggg::Settings settings_;
    bool unload_ = false;

    bool manualPick_ = false;  // chosen with F6 / Next boss: don't auto-switch away from it
    uintptr_t playerChr_ = 0;
    int playerLastHp_ = -1;
    uintptr_t bossChr_ = 0;
    int bossLastHp_ = -1;
    Clock::time_point lastScan_{};
    std::optional<ChrState> player_;
    std::optional<ChrState> boss_;

    std::optional<Vec3> safe_;     // last solid ground, for fall respawns
    std::optional<Vec3> lastPos_;
    float lastSampleY_ = 0;
    int stableSamples_ = 0;
    Clock::time_point lastSampleAt_{};
    Clock::time_point respawnGraceUntil_{};
};

}  // namespace

void RunTrainer(HMODULE self, GameAdapter& game, const wchar_t* moduleId) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(self, path, MAX_PATH);
    const fs::path moduleIni = fs::path(path).replace_extension(L".ini");
    const fs::path data = ggg::paths::DataDir(moduleId);

    const CoreConfig cfg = LoadCoreConfig(moduleIni);
    Link link(data / L"gitgudgym.log", ggg::paths::SettingsIni());
    link.Log("Git Gud Gym v" GGG_V_STRING " loaded");

    std::string err;
    for (int tries = 0; !game.Init(err); ++tries) {
        if (tries == 0) link.Event(EventType::Warning, 0, 0, "Waiting for the game: %s", err.c_str());
        if (Pressed(VK_F10)) return;
        Sleep(500);
    }
    for (const std::string& line : game.Describe(nullptr)) link.Log("%s", line.c_str());

    Trainer trainer(cfg, game, link, data / L"stats");
    trainer.Run();
    link.Log("Unloaded");
}
