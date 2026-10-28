# store/ —— 一个仓库类，按能力拆成多个实现单元

`ClipRepository` 是同一个类的声明（`ClipRepository.h`），实现按能力分文件。
这不是「拆类」，是「拆文件」：**改哪一类行为，就只有那一个文件该动**。

| 文件 | 唯一职责 |
|---|---|
| `ClipRepository.Schema.cpp` | 建表/迁移/打开/自愈/快速校验 |
| `ClipRepository.Insert.cpp` | **到达路径**：去重、身份解析、`ReuseTouchPolicy`（位置语义的唯一产地） |
| `ClipRepository.ClipMutations.cpp` | 条目级状态：置顶/收藏/分级/归组/粘贴计数/删除/清理历史/手动排序 |
| `ClipRepository.Tags.cpp` | 标签 CRUD 与条目打标 |
| `ClipRepository.Queue.cpp` | 粘贴队列 |
| `ClipRepository.Groups.cpp` | 分组 CRUD 与分组页签 |
| `ClipRepository.Settings.cpp` | 设置读写 |
| `ClipRepository.Query.cpp` | 只读查询（列表/详情/计数/搜索） |
| `ClipRepository.Identity.cpp` | 身份键构造与候选查找（去重的判据） |
| `ClipRepository.Assets.cpp` | 图片资产落库、备份轮转与恢复、OCR 结果写入 |
| `ClipRepository.Internal.h` | 私有原语（Statement/Bind/事务/错误包装），**不属于任何能力** |

## 红线（各自对应一次实发回归）

1. **排序键只有一个：`clips.updated_at`；它只在「到达」时由 `InsertClip` 写。**
   `Assets.cpp`（ingest 补全、失败标记、快照提升）与其它任何路径都不许写它，
   否则「粘贴后原地不动」会被几十毫秒后的异步收尾顶到首位。
2. **去重判据只来自 `Identity.cpp`。** 采集侧/落盘侧必须喂同一份规范像素，
   否则同一张图会在写回后被当成新条目。
3. **备份轮转与自愈替身必须原子。** 现在 `RestoreFromLatestBackup` 是直接覆盖，
   多实例并发时会互相看到「半个数据库」（`file is not a database`）——待修。
