#pragma once
#include <windows.h>

class GameAdapter;

// Runs the trainer until the user unloads it (F10 or the app). Called on the module's own thread.
// moduleId names the per-game data folder (stats, log), e.g. L"eldenring".
void RunTrainer(HMODULE self, GameAdapter& game, const wchar_t* moduleId);
