# Scraps

<p align="center">
  <img src="assets/brand/hero.png" alt="Scraps — 最快最轻的 Windows 原生剪贴板管理器（剪贴板历史 / 队列粘贴 / CLI）" width="880">
</p>

<p align="center">
  <a href="https://github.com/zeddddf/scraps/actions/workflows/ci.yml"><img src="https://github.com/zeddddf/scraps/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/zeddddf/scraps/releases/latest"><img src="https://img.shields.io/github/v/release/zeddddf/scraps?label=release" alt="Release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/zeddddf/scraps" alt="License"></a>
</p>

**最快最轻的 Windows 原生剪贴板管理器**
*The fastest, lightest native clipboard manager for Windows.*

[English](README.md) | 简体中文

Scraps 是一个原生 Win32 剪贴板管理器：单进程常驻、D2D 直绘、无任何运行时依赖，
在十万级历史条目下依然保持瞬时响应。所有数据 100% 本地存储，不上传任何内容。

## 下载

也可以用 [winget](https://github.com/microsoft/winget-cli) 一行安装：

```bat
winget install zeddddf.Scraps
```

或从 [Releases](https://github.com/zeddddf/scraps/releases/latest) 下载：

| 版本 | 说明 |
|---|---|
| [安装版](https://github.com/zeddddf/scraps/releases/latest) `Scraps-x.y.z-setup.exe` | 向导安装、开始菜单/桌面快捷方式、可卸载 |
| [便携版](https://github.com/zeddddf/scraps/releases/latest) `Scraps-x.y.z-portable.zip` | 解压即用，免安装 |

历史记录与设置存储于 `%LOCALAPPDATA%\Scraps`，安装版与便携版通用，卸载不会删除用户数据。


## 特性

- 剪贴板监听与历史记录，采集文本 / HTML / RTF / 图片 / 文件列表
- `SQLite + FTS5` 全文存储，异步分页查询，搜索结果高亮
- 收藏、置顶、分组、标签系统与标签过滤
- `history / library` 分层存储、时间范围过滤与时间分组
- 队列与顺序粘贴
- 常用统计视图
- 文件/图片条目外部拖拽导出
- 全局唤出热键与设置持久化
- Named Pipe 本地 IPC 与 CLI（`scraps-cli`），可脚本化
- 截图子进程（WGC/DXGI/GDI 多后端）

## 横向对比

| | Scraps | 系统内置（Win+V） | Ditto |
|---|---|---|---|
| 历史容量 | 十万级条目，history/library 分层 | 小容量固定缓冲 | 大 |
| 全文搜索 | SQLite FTS5，高亮，十万级瞬时 | 简单过滤 | 有 |
| 标签 / 分组 / 收藏 | 有 | 仅置顶 | 有 |
| 队列 / 顺序粘贴 | 有 | 无 | 有 |
| CLI 与脚本化 | 有（`scraps-cli`、Named Pipe IPC） | 无 | 有限 |
| 云 / 手机同步 | 无（设计取向：100% 本地） | 有（微软账户） | 仅局域网 |
| 界面 | 原生 Direct2D 直绘，Win11 风格，60 FPS 虚拟化列表 | 系统样式 | 经典样式 |
| 运行时依赖 | 无 | 内置 | 无 |
| 许可 | GPL-3.0 开源 | Windows 组件 | GPL |

## 性能

虚拟列表内核采用 Fenwick 树索引 + 稳定槽位池，可见范围计算与行高查询均为
O(1)/O(log N)：

| 场景 | 每帧耗时 |
|---|---|
| 10K 条目，变高行 | 1.7~4.4 μs |
| 100K 条目，变高行 | 1.7~2.5 μs |
| 可见范围二分（变高，100K） | 25.5 ns / 次 |
| 可见范围计算（定高） | 4.5 ns / 次 |

10K → 100K 几乎无衰减；60FPS 帧预算（16.7 ms）余量约四个数量级。

## 构建

需要 Visual Studio 2022（x64）与 CMake ≥ 3.28。

```bat
build_release.bat
```

或手动：

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物位于 `build/Release/`：

- `Scraps.exe` — 主程序（托盘常驻，单实例）
- `scraps-cli.exe` — 命令行工具（`--help` 查看用法）
- `scraps_tests.exe` — 测试套件

## 文档

- [架构总览](docs/ARCHITECTURE.md)
- [命令参考](docs/COMMANDS.md)
- [存储 Schema](docs/SCHEMA.md)
- [性能说明](docs/PERF.md)

## 常见问题

**Scraps 和 Ditto / PowerToys 剪贴板有什么区别？**
Scraps 是纯原生 Win32 + Direct2D 直绘实现，没有运行时依赖、没有 Electron；虚拟化列表基于 Fenwick 索引支持可变行高，十万级条目下滚动依然瞬时。相比 PowerToys 剪贴板提供更完整的历史管理能力（标签 / 分组 / 队列 / 库分层）与 CLI 脚本化接口。

**我的数据存在哪里？会被上传吗？**
全部存储在本地 `%LOCALAPPDATA%\Scraps`（SQLite + FTS5）。无任何网络上传、无遥测、无账号，可完全离线使用。

**支持哪些系统？**
Windows 10 / 11（x64）。

**历史记录会无限膨胀吗？**
`history / library` 分层存储 + 清理策略，常用内容进收藏与标签体系，其余按策略清理。

## 许可

本项目以 [GPL-3.0](LICENSE) 发布。

第三方组件（nlohmann/json、qrencode、SQLite 头、yoga）随附于
[third_party/](third_party/NOTICES.md)，按其自身许可分发。
