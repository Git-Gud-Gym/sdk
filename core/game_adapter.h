// The interface every game module implements. The core (trainer, stats, link to the app) only
// talks to the game through this, so adding a game means writing one adapter.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct Vec3 {
    float x, y, z;
};

// Snapshot of one character's state, read from game memory.
struct ChrState {
    uintptr_t chr = 0;  // the game's character object; identifies the character
    int hp = 0;
    int maxHp = 0;
    Vec3 pos{};  // y is up
    bool hasPos = false;
    // Adapter-private handles (e.g. module pointers) so writes don't have to walk pointers again.
    uintptr_t internal[4] = {};
};

float Distance(const Vec3& a, const Vec3& b);
float Distance(const ChrState& a, const ChrState& b);

class GameAdapter {
public:
    virtual ~GameAdapter() = default;

    // Finds the game's globals. Called repeatedly until it returns true.
    virtual bool Init(std::string& err) = 0;

    // The local player's character, or 0 on loading screens and menus.
    virtual uintptr_t Player() const = 0;
    // Every loaded character (enemies, NPCs, the player).
    virtual std::vector<uintptr_t> Characters() const = 0;
    virtual bool IsLoaded(uintptr_t chr) const;

    virtual std::optional<ChrState> ReadChr(uintptr_t chr) const = 0;
    virtual bool SetHp(const ChrState& s, int hp) const = 0;
    virtual bool SetPosition(const ChrState& s, const Vec3& pos) const = 0;
    // Makes HP stop at 1 instead of 0. isPlayer lets adapters use player-only mechanisms.
    virtual bool SetNoDead(const ChrState& s, bool on, bool isPlayer) const = 0;

    // Extra lines for the F8 probe dump (resolved addresses, flag bytes, ...).
    virtual std::vector<std::string> Describe(const ChrState* player) const { (void)player; return {}; }
};
