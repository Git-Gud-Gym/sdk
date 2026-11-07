#include "stats.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace {

std::string Timestamp(const char* fmt) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return buf;
}

std::string CsvQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) out += (c == '"') ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

constexpr const char* kAttemptsHeader =
    "timestamp,session_id,label,attempt,outcome,duration_s,boss_max_hp,damage_dealt,damage_taken,"
    "hits_landed,hits_taken,healing,boss_lowest_hp_pct";
constexpr const char* kSessionsHeader =
    "timestamp,session_id,label,attempts,deaths,defeats,damage_dealt,damage_taken,hits_landed,hits_taken,"
    "healing,best_boss_hp_pct,fastest_defeat_s,total_fight_s";

}  // namespace

Stats::Stats(std::filesystem::path dir, std::string label)
    : dir_(std::move(dir)), label_(std::move(label)), sessionId_(Timestamp("%04u%02u%02u-%02u%02u%02u")) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
}

Attempt& Stats::EnsureAttempt() {
    if (!current_) {
        current_.emplace();
        current_->index = totals_.attempts + 1;
        current_->start = std::chrono::steady_clock::now();
    }
    return *current_;
}

double Stats::CurrentAttemptSec() const {
    if (!current_) return 0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - current_->start).count();
}

void Stats::PlayerDamaged(int amount) {
    Attempt& a = EnsureAttempt();
    a.damageTaken += amount;
    a.hitsTaken++;
}

void Stats::PlayerHealed(int amount) {
    // Healing outside a fight (e.g. at a grace) isn't interesting.
    if (current_) current_->healing += amount;
}

void Stats::BossDamaged(int amount, int bossHp, int bossMaxHp) {
    Attempt& a = EnsureAttempt();
    a.damageDealt += amount;
    a.hitsLanded++;
    a.bossMaxHp = bossMaxHp;
    a.bossLowestHpPct = std::min(a.bossLowestHpPct, 100.0f * bossHp / bossMaxHp);
}

void Stats::PlayerDied(bool fell) {
    totals_.deaths++;
    if (fell) totals_.falls++;
    // Deaths away from a boss fight still count, but only end an attempt if one is running.
    if (current_) EndAttempt(fell ? "fall" : "death");
}

void Stats::BossDefeated() {
    Attempt& a = EnsureAttempt();
    a.bossLowestHpPct = 0;
    totals_.defeats++;
    EndAttempt("defeat");
}

void Stats::Abandon() {
    if (current_) EndAttempt("abandoned");
}

void Stats::EndAttempt(const char* outcome) {
    const Attempt a = *current_;
    current_.reset();
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - a.start).count();

    totals_.attempts++;
    totals_.damageDealt += a.damageDealt;
    totals_.damageTaken += a.damageTaken;
    totals_.hitsLanded += a.hitsLanded;
    totals_.hitsTaken += a.hitsTaken;
    totals_.healing += a.healing;
    totals_.totalAttemptSec += sec;
    totals_.bestBossHpPct = std::min(totals_.bestBossHpPct, a.bossLowestHpPct);
    if (std::string_view(outcome) == "defeat" && (!totals_.fastestDefeatSec || sec < *totals_.fastestDefeatSec)) {
        totals_.fastestDefeatSec = sec;
    }

    char row[512];
    std::snprintf(row, sizeof(row), "%s,%s,%s,%d,%s,%.2f,%d,%lld,%lld,%d,%d,%lld,%.1f",
                  Timestamp("%04u-%02u-%02u %02u:%02u:%02u").c_str(), sessionId_.c_str(), CsvQuote(label_).c_str(),
                  a.index, outcome, sec, a.bossMaxHp, a.damageDealt, a.damageTaken, a.hitsLanded, a.hitsTaken,
                  a.healing, a.bossLowestHpPct);
    AppendCsv(dir_ / "attempts.csv", kAttemptsHeader, row);
}

void Stats::FlushSession() {
    Abandon();
    if (totals_.attempts == 0) return;
    const SessionTotals& t = totals_;
    char fastest[32] = "";
    if (t.fastestDefeatSec) std::snprintf(fastest, sizeof(fastest), "%.2f", *t.fastestDefeatSec);
    char row[512];
    std::snprintf(row, sizeof(row), "%s,%s,%s,%d,%d,%d,%lld,%lld,%d,%d,%lld,%.1f,%s,%.1f",
                  Timestamp("%04u-%02u-%02u %02u:%02u:%02u").c_str(), sessionId_.c_str(), CsvQuote(label_).c_str(),
                  t.attempts, t.deaths, t.defeats, t.damageDealt, t.damageTaken, t.hitsLanded, t.hitsTaken, t.healing,
                  t.bestBossHpPct, fastest, t.totalAttemptSec);
    AppendCsv(dir_ / "sessions.csv", kSessionsHeader, row);
}

void Stats::ResetSession() {
    FlushSession();
    totals_ = {};
    sessionId_ = Timestamp("%04u%02u%02u-%02u%02u%02u");
}

void Stats::AppendCsv(const std::filesystem::path& file, const char* header, const std::string& row) {
    const bool isNew = !std::filesystem::exists(file);
    std::ofstream out(file, std::ios::app);
    if (!out) return;
    if (isNew) out << header << '\n';
    out << row << '\n';
}
