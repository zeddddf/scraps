# ui

负责窗口和可视交互，不直接读写数据库。

## 状态所有权（批次 A/B 后）

- `HistoryViewState.h`：历史列表数据态的唯一权威所有者（分页缓存、位图缓存、浏览态、选择态、签发型代次）。MainWindow 持有 `state_`，`HistoryDataController` / `HistoryAssetLoader` 经 `history_state()` 访问，**不经 friend**。
- 异步任务：`common::AsyncRunner`（禁止窗口自建线程池）；任务一律按值捕获 + PostMessage 回投。
- 代次：`common::GenerationToken`（单调自签发）；水位/镜像型代次保留 atomic，见 HistoryViewState.h 注释。
- 架构不变式自查：`python .agents/scripts/check_invariants.py`。
