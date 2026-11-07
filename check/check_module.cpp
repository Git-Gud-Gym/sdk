// A do-nothing adapter: proves the SDK compiles and links as a module. Never shipped.
#include <windows.h>

#include "game_adapter.h"
#include "trainer.h"

namespace {

class NoGame : public GameAdapter {
public:
    bool Init(std::string& err) override {
        err = "SDK check module";
        return false;
    }
    uintptr_t Player() const override { return 0; }
    std::vector<uintptr_t> Characters() const override { return {}; }
    std::optional<ChrState> ReadChr(uintptr_t) const override { return std::nullopt; }
    bool SetHp(const ChrState&, int) const override { return false; }
    bool SetPosition(const ChrState&, const Vec3&) const override { return false; }
    bool SetNoDead(const ChrState&, bool, bool) const override { return false; }
};

}  // namespace

extern "C" __declspec(dllexport) void GggSdkCheck(HMODULE self) {
    NoGame game;
    RunTrainer(self, game, L"sdkcheck");
}
