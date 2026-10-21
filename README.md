# Scraps

**最快最轻的 Windows 原生剪贴板管理器**
*The fastest, lightest native clipboard manager for Windows.*

Scraps 是一个原生 Win32 剪贴板管理器：单进程常驻、D2D 直绘、无任何运行时依赖，
在十万级历史条目下依然保持瞬时响应。所有数据 100% 本地存储，不上传任何内容。

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

## 许可

本项目以 [GPL-3.0](LICENSE) 发布。

第三方组件（nlohmann/json、qrencode、SQLite 头、yoga）随附于
[third_party/](third_party/NOTICES.md)，按其自身许可分发。
