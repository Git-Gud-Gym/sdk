# Adding a game

Each game is its own repository, `module-<id>`, built on this SDK. The SDK does the heavy lifting:
the trainer loop, stats, falls, boss tracking, the link to the launcher, hotkeys and the overlay.
Your job is to teach it how to read and write the game's memory.

The [Elden Ring module](https://github.com/Git-Gud-Gym/module-eldenring) is the reference
implementation: start by copying it.

## 1. Research the game

You'll need, for the game version you're targeting:

| What | Why |
|---|---|
| A pattern to find the **player** character | Player HP and position |
| A way to list **loaded characters** | Finding the boss |
| **HP / max HP** offsets on a character | Deaths, defeats, damage stats |
| **Position** (x, y, z with y up) | Boss distance, fall detection, respawning |
| A **"can't die"** mechanism (a per-character flag, or a debug flag) | Keeping HP at 1 instead of 0 |

Cheat Engine tables and the game's modding community are the usual starting points. Prefer
**byte-pattern scans** over hardcoded addresses so the module survives small patches. Put tunable
offsets in the module's `.ini`, so users can fix a broken offset without a rebuild.

## 2. Create the module

Copy `module-eldenring` into a new folder and rename things:

```
module-<id>/
  CMakeLists.txt         FetchContent the SDK + ggg_add_module(ID <id> ...)
  module.json            identity + what the launcher needs to know about the game
  gitgudgym-<id>.ini     default offsets and tuning ([general], [boss], [offsets])
  src/<id>_game.h/.cpp   class <Id>Game : public GameAdapter
  src/dllmain.cpp        creates the adapter and calls RunTrainer(self, game, L"<id>")
  src/module.rc          version resource (change the two #defines)
  CHANGELOG.md           "## vX.Y.Z" sections become release notes
```

`<id>` is lowercase letters and digits only, e.g. `sekiro` or `darksouls3`.

### module.json

```json
{
  "schema": 1,
  "id": "sekiro",
  "name": "Sekiro: Shadows Die Twice",
  "version": "0.1.0",
  "tagline": "One line shown on the library card.",
  "dll": "gitgudgym-sekiro.dll",
  "files": ["gitgudgym-sekiro.dll", "gitgudgym-sekiro.ini", "module.json"],
  "features": ["Shown on the website's games page"],
  "game": {
    "exe": "sekiro.exe",
    "steamAppId": 814380,
    "steamFolder": "Sekiro",
    "exeSubdir": "",
    "antiCheatProcesses": [],
    "versions": [{ "exe": "1.6.0.0", "patch": "1.06", "tested": true }]
  }
}
```

- `steamFolder` / `exeSubdir`: where the exe sits inside `steamapps\common\`. Used for auto-detect.
- `antiCheatProcesses`: the launcher refuses to attach while any of these run. Required for every
  game that has online anti-cheat.
- `versions`: exe file versions you've verified. Others show as "untested" in the launcher.

### The adapter

Implement every pure virtual in [`core/game_adapter.h`](../core/game_adapter.h):
- **Reading memory:** use `mem::Read` / `mem::ReadPtr` / `mem::SafeCopy` from `core/memory.h`.
  They never crash the game on a bad pointer.
- **Rejecting garbage:** `ReadChr` must reject values that don't look like a real character (HP out
  of range, null modules). Callers write to whatever you return.
- **Caching pointers:** store adapter-private pointers in `ChrState::internal[]` so writes don't
  walk the pointer chain again.
- **The F8 probe:** `Describe()` adds lines to its output. Include whatever helps someone debug
  offsets.

## 3. Test it with the launcher

1. Run `build.bat`. The module lands in `bin\<id>\`.
2. Point the launcher at it: set the environment variable `GGG_DEV_MODULES` to your module's `bin`
   folder (several folders can be separated with `;`), then start Git Gud Gym. The game shows up in
   the sidebar with a **dev** badge.
3. Start the game **offline**, open its page and let it attach.
4. Press <kbd>F8</kbd> and check `gitgudgym.log` (*Log* button): the player HP should match the
   game, the position should change as you move, and characters should be listed.
5. Die to an enemy, take a boss to zero, and fall off a ledge. Check the counters and the CSV.

## 4. Ship it

1. Open a [game request](https://github.com/Git-Gud-Gym/registry/issues/new?template=game_request.yml)
   (or comment on the existing one) saying you're building it. A maintainer creates
   `Git-Gud-Gym/module-<id>`, or transfers your repo into the org. Only org repos can appear in the
   library, because the launcher runs as admin.
2. Tag `v0.1.0` with a matching `## v0.1.0` changelog section. The release workflow builds and
   publishes it.
3. Add the module to the [registry](https://github.com/Git-Gud-Gym/registry)'s `modules.json` in a PR.
   Once it's merged, the game appears in everyone's library.
