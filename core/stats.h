#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// One try at the boss: starts on the first damage exchanged, ends on a death, a defeat,
// or when the boss is lost (warped away, deselected, mod unloaded).
struct Attempt {
    int index = 0;
    std::chrono::steady_clock::time_point start;
    int bossMaxHp = 0;
    int64_t damageDealt = 0;
    int64_t damageTaken = 0;
    int hitsLanded = 0;
    int hitsTaken = 0;
    int64_t healing = 0;
    float bossLowestHpPct = 100.0f;
};

struct SessionTotals {
    int attempts = 0;
    int deaths = 0;
    int falls = 0;  // deaths caused by falling (also counted in deaths)
    int defeats = 0;
    int64_t damageDealt = 0;
    int64_t damageTaken = 0;
    int hitsLanded = 0;
    int hitsTaken = 0;
    int64_t healing = 0;
    float bestBossHpPct = 100.0f;           // lowest boss HP reached in any attempt
    std::optional<double> fastestDefeatSec;
    double totalAttemptSec = 0;
};

class Stats {
public:
    Stats(std::filesystem::path dir, std::string label);

    void PlayerDamaged(int amount);
    void PlayerHealed(int amount);
    void BossDamaged(int amount, int bossHp, int bossMaxHp);
    void PlayerDied(bool fell = false);
    void BossDefeated();
    void Abandon();

    // Writes the session summary row and starts a fresh session.
    void ResetSession();
    void FlushSession();

    const SessionTotals& totals() const { return totals_; }
    const std::optional<Attempt>& current() const { return current_; }
    double CurrentAttemptSec() const;

private:
    Attempt& EnsureAttempt();
    void EndAttempt(const char* outcome);
    void AppendCsv(const std::filesystem::path& file, const char* header, const std::string& row);

    std::filesystem::path dir_;
    std::string label_;
    std::string sessionId_;
    std::optional<Attempt> current_;
    SessionTotals totals_;
};
