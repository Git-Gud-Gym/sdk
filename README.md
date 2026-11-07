# Git Gud Gym SDK

Everything a [Git Gud Gym](https://git-gud-gym.github.io/) game module is built from:

- **The trainer core** (`core/`): immortality, death and defeat counting, fall respawns, boss
  tracking, attempt stats and CSV logging, hotkeys, and the link to the launcher. Every module
  compiles it in.
- **The `GameAdapter` interface** (`core/game_adapter.h`): the only part that differs per game.
  You implement it, and the core does the rest.
- **The launcher ↔ module contract** (`include/`): the shared-memory layout (`shared_state.h`),
  file locations (`paths.h`) and version resources. The [launcher](https://github.com/Git-Gud-Gym/launcher)
  builds against these too.
- **`ggg_add_module()`**: one CMake call that turns your adapter into `gitgudgym-<id>.dll`.

## Using it

In your module's `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(ggg_sdk
    GIT_REPOSITORY https://github.com/Git-Gud-Gym/sdk.git
    GIT_TAG v0.1.0)
FetchContent_MakeAvailable(ggg_sdk)

ggg_add_module(ID mygame MANIFEST module.json INI gitgudgym-mygame.ini RC src/module.rc
               SOURCES src/dllmain.cpp src/mygame_game.cpp)
```

To work on the SDK and a module together, build the module with
`-DFETCHCONTENT_SOURCE_DIR_GGG_SDK=path\to\sdk`.

**[Adding a game: the full guide →](docs/ADDING_A_GAME.md)**. The
[Elden Ring module](https://github.com/Git-Gud-Gym/module-eldenring) is the reference implementation.

## Versioning

The SDK follows semver. A **minor** release may add to the `GameAdapter` interface (with default
implementations). A **major** release changes the shared-memory contract, which also bumps the
mapping name in `shared_state.h` (`GitGudGym.State.v1` → `.v2`), so an old launcher and a new module
never misread each other.

## Building

`build.bat` compiles the core against a stub adapter (`check/`), which is what CI runs.
Requires Visual Studio 2022 (or Build Tools) with the C++ workload.

## License

[MIT](LICENSE)
