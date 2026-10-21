# Architecture

当前实现按下面的边界拆分：

- `src/app`
  - 进程装配、单实例、托盘、开机启动、窗口生命周期
- `src/core/store`
  - `SQLite`、`FTS5`、历史数据、设置、分组
- `src/core/clipboard`
  - 剪贴板采集、格式识别、图片落盘、写回与粘贴
- `src/core/commands`
  - `AppController`、命令分发、跨 UI/CLI 共享的业务入口
- `src/core/actions`
  - 内容类型到动态操作栏的解析
- `src/core/paste`
  - 文本变形引擎
- `src/ipc`
  - `Named Pipe` 协议和服务端/客户端
- `src/platform/win32`
  - 隐藏消息窗口、热键、托盘图标
- `src/ui`
  - 主窗口、设置窗口、虚拟列表、预览、D2D 外壳
- `src/cli`
  - 命令行入口

关键原则：

- UI 不直接访问数据库和剪贴板实现
- 业务入口统一收敛到 `AppController`
- `CLI` 通过 `Named Pipe` 调用主进程命令服务
- GUI 主进程保留 `Named Pipe`，作为后续扩展接口宿主
- 变形粘贴和复制路径通过跨进程抑制状态避免自监听回写
- 列表查询、来源图标和图片预览尽量移出 UI 线程

当前还没做完的部分：

- 主界面完整改成 `D2D/DWrite` 自绘
- 设置页完整改成自绘布局
- 更细粒度的后台分页、缩略图缓存和资源池
- 更完整的视觉系统与动效
