# Contributing to Scraps

Thanks for your interest in contributing!

## Getting started

1. Fork the repo and create a branch from `main`.
2. Build: run `build_release.bat` (needs Visual Studio 2022 + CMake ≥ 3.28), or see the README for manual CMake commands.
3. Run the test suite: `build\Release\scraps_tests.exe` — all tests must pass before you submit.

## Ground rules

- **No ECS frameworks** (flecs / apex / …). This codebase is deliberately direct-rendering, non-ECS.
- **Layering is one-way**: `ui → core → common`, `ui → platform`. `core` must not include `ui/`.
- Side effects (clipboard writes, DB writes, IPC) live in `core/` and `ipc/` only.
- `WM_PAINT` only sets a dirty flag; never draw D2D inside it.
- UI changes must be verified with screenshots (text rows, image rows, file rows, fast scrolling). "It compiles" is not done.
- After bulk-editing any `.cpp/.h`, run `python .agents/scripts/check_line_integrity.py <file>`.

## Submitting

- Keep PRs focused; one logical change per PR.
- Describe what changed and why; include screenshots for UI changes.
- New tests go in `src/tests/` and must be registered in the root `CMakeLists.txt`.

## License

By contributing you agree that your contributions are licensed under the GPL-3.0 license of this repository.
