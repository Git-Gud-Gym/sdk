#include "game_adapter.h"

#include <cmath>

float Distance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float Distance(const ChrState& a, const ChrState& b) {
    return a.hasPos && b.hasPos ? Distance(a.pos, b.pos) : INFINITY;
}

bool GameAdapter::IsLoaded(uintptr_t chr) const {
    for (uintptr_t c : Characters()) {
        if (c == chr) return true;
    }
    return false;
}
