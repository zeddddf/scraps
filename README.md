# Scraps

<p align="center">
  <img src="assets/brand/hero.png" alt="Scraps — the fastest, lightest native clipboard manager for Windows · clipboard history, paste queue, CLI" width="880">
</p>

<p align="center">
  <a href="https://github.com/zeddddf/scraps/actions/workflows/ci.yml"><img src="https://github.com/zeddddf/scraps/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/zeddddf/scraps/releases/latest"><img src="https://img.shields.io/github/v/release/zeddddf/scraps?label=release" alt="Release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/zeddddf/scraps" alt="License"></a>
</p>

**The fastest, lightest native clipboard manager for Windows.**
*A free, open-source alternative to Ditto and the built-in Windows clipboard history (Win+V) — Win32 + Direct2D, zero runtime dependencies, 100% local.*

English | [简体中文](README.zh-CN.md)

Scraps is a native Win32 clipboard manager: single resident process, Direct2D direct rendering, zero runtime dependencies — instant response even with 100k+ history entries. All data stays 100% local. No accounts, no telemetry, no cloud.

## Install

One line with [winget](https://github.com/microsoft/winget-cli):

```bat
winget install zeddddf.Scraps
```

Or grab a binary from [Releases](https://github.com/zeddddf/scraps/releases/latest):

| Edition | File | Notes |
|---|---|---|
| Installer | `Scraps-x.y.z-setup.exe` | Guided setup, start menu / desktop shortcuts, uninstaller included |
| Portable | `Scraps-x.y.z-portable.zip` | Extract and run, no installation |

History and settings are stored in `%LOCALAPPDATA%\Scraps`. Installer and portable editions are interchangeable; uninstalling never deletes your data.

## Features

- Clipboard monitoring and history for text / HTML / RTF / images / file lists
- Full-text search backed by `SQLite + FTS5`, async paging, result highlighting
- Favorites, pins, groups, tag system and tag filtering
- `history / library` layered storage, time-range filtering and time grouping
- Paste queue and sequential pasting
- Usage statistics view
- Drag-and-drop export of file / image entries
- Global hotkey and persistent settings
- Named Pipe local IPC and CLI (`scraps-cli`) for scripting
- Screenshot subprocess (WGC / DXGI / GDI backends)

## How it compares

| | Scraps | Windows built-in (Win+V) | Ditto |
|---|---|---|---|
| History capacity | 100k+ entries, layered history / library | Small fixed buffer | Large |
| Full-text search | SQLite FTS5, highlighted, instant at 100k+ | Basic filter | Yes |
| Tags / groups / favorites | Yes | Pinned only | Yes |
| Paste queue / sequential paste | Yes | No | Yes |
| CLI & scripting | Yes (`scraps-cli`, Named Pipe IPC) | No | Limited |
| Cloud / phone sync | No — 100% local by design | Yes (Microsoft account) | LAN only |
| UI | Native Direct2D direct rendering, Win11 style, virtualized list at 60 FPS | System UI | Classic UI |
| Runtime dependencies | None | Built-in | None |
| License | GPL-3.0 open source | Part of Windows | GPL |

## Performance

The virtualized list uses a Fenwick-tree index plus a stable slot pool; visible-range lookup and row-height queries are O(1)/O(log N):

| Scenario | Per-frame cost |
|---|---|
| 10K entries, variable row heights | 1.7~4.4 μs |
| 100K entries, variable row heights | 1.7~2.5 μs |
| Visible-range binary search (variable, 100K) | 25.5 ns / call |
| Visible-range lookup (fixed height) | 4.5 ns / call |

10K → 100K shows almost no degradation; headroom against the 60 FPS frame budget (16.7 ms) is roughly four orders of magnitude.

## FAQ

**How is Scraps different from Ditto or the PowerToys clipboard?**
Scraps is pure native Win32 + Direct2D — no runtime dependencies, no Electron. The virtualized list is built on a Fenwick index with variable row heights, staying instant at 100k entries. Compared to the PowerToys clipboard it offers far richer history management (tags / groups / paste queue / library layers) plus a scriptable CLI.

**Where is my data stored? Is it uploaded anywhere?**
Everything lives in `%LOCALAPPDATA%\Scraps` (SQLite + FTS5). No uploads, no telemetry, no accounts — Scraps works fully offline.

**Which Windows versions are supported?**
Windows 10 / 11 (x64).

## Build

Requires Visual Studio 2022 (x64) and CMake ≥ 3.28.

```bat
build_release.bat
```

Or manually:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Artifacts land in `build/Release/`:

- `Scraps.exe` — main program (tray resident, single instance)
- `scraps-cli.exe` — command line tool (`--help` for usage)
- `scraps_tests.exe` — test suite

## Docs

- [Architecture](docs/ARCHITECTURE.md)
- [Commands](docs/COMMANDS.md)
- [Storage schema](docs/SCHEMA.md)
- [Performance notes](docs/PERF.md)

## License

Released under the [GPL-3.0](LICENSE).

Third-party components (nlohmann/json, qrcodegen, SQLite headers, yoga) ship under
[third_party/](third_party/NOTICES.md) with their own licenses.
