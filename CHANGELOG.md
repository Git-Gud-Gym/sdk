# Changelog

## v0.1.0 - 2026-11-07

First standalone release, extracted from the original Git Gud Gym repository.

- **Trainer core:** player and boss immortality with death and defeat counting, fall respawns onto
  the last solid ground, automatic boss tracking, per-attempt stats to CSV, hotkeys F5 to F10.
- **`GameAdapter` interface** for game-specific memory access.
- **Shared-memory contract `GitGudGym.State.v1`:** settings, commands, status and an event ring.
- **`ggg_add_module()`** CMake helper. Module versions come from `module.json`.
