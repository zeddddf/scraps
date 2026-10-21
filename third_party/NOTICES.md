# Third-party components

本目录下的第三方代码按其自身许可随本项目（GPL-3.0）分发。各组件来源与许可如下。

## nlohmann/json

- 文件：`nlohmann/json.hpp`
- 来源：https://github.com/nlohmann/json （单头文件发行版）
- 许可：MIT License
- 用途：IPC 协议与截图子进程的 JSON 序列化

## qrcodegen

- 目录：`qrcodegen/`
- 来源：QR Code generator library (C) —— Copyright (c) Project Nayuki
  https://www.nayuki.io/page/qr-code-generator-library
- 许可：MIT License（声明于 `qrcodegen.h` 源文件头注释）
- 用途：URL 分享的二维码生成（`src/core/qr/UrlQrGenerator.cpp`）

## sqlite

- 文件：`sqlite/sqlite3.h`、`sqlite/sqlite3ext.h`
- 来源：https://www.sqlite.org （官方 amalgamation 头文件）
- 许可：Public Domain（SQLite Is In The Public Domain）
- 说明：SQLite 运行时使用 Windows 系统库 `winsqlite3.dll`，本仓库不内嵌 SQLite 实现。

## yoga

- 目录：`yoga/`
- 来源：https://github.com/facebook/yoga
- 许可：MIT License（见 `yoga/LICENSE`）
- 用途：UI 布局计算（FlexNode）
