# clipboard/ —— 按能力分工，不按「文件大小」分工

这个目录处理剪贴板。历史上所有代码都在 `ClipboardCapture.cpp`（1671 行）里，
结果是：**改 DIB 像素归一化的人，和改编排顺序的人，动的是同一个文件**，
两次实发回归都是这么埋进去的。现在按能力切开：

| 文件 | 唯一职责 | 绝不做什么 |
|---|---|---|
| `ClipboardRead.*` | 从**已打开**的剪贴板读字节（UNICODETEXT / ANSI / HGLOBAL 二进制 / CF_HDROP） | 不判断格式是否"支持"、不解析语义、不写盘 |
| `ClipboardSession.*` | **剪贴板会话与全局内存原语**：`ClipboardScope`（唯一的 Open/Close 点，读写共用）+ GlobalAlloc/GlobalLock/SetClipboardData 组合 + CF_HDROP/PreferredDropEffect 写入 | 不决定「写什么格式、什么内容」 |
| `ClipboardDiagnostics.*` | 自检：此刻有哪些格式、owner 是谁、序列号多少 | 不写剪贴板、不改状态 |
| `ImageDib.*` | DIB/BITMAP 像素层：头部解析、颜色表、像素提取、像素哈希、**保留位 alpha 归一化** | 不碰剪贴板、不写文件 |
| `ImagePng.*` | 位图/DIB/PNG → PNG 文件（WIC 编码）+ 缩略图缩放 | 不决定路径、不写库 |
| `ImageIdentity.*` | 图片**内容身份**：所有来源统一解码成规范像素再哈希 | 不落盘、不比较条目 |
| `ImageWriteback.*` | **图片写回 payload 合成**：PNG→HBITMAP（WIC 解码）→ DIB/DIBV5/CF_BITMAP/PNG 四格式单会话写入；重活必须在打开剪贴板之前完成 | 不定位目标窗口、不发送按键 |
| `PasteTarget.*` | **粘贴目标定位与恢复**：root/focus 上下文、AttachThreadInput 前台恢复、焦点轮询细化 | 不发送按键、不碰剪贴板内容 |
| `PasteDispatch.*` | **粘贴按键派发**：Ctrl+V / Shift+Insert / 终端 Ctrl+Shift+V / Unicode 直输，Fast/Recovery 两级路由 | 不解码图片、不读写剪贴板格式 |
| `ProcessInfo.*` | HWND → 进程路径/进程名/窗口标题 | 不解析内容 |
| `ClipboardCapture.*` | 只剩**采集编排**：判定类型 → 抽取负载 → 组装待落盘任务；以及落盘编排 | 不内联像素算法、不内联编码细节 |
| `ClipboardFormat.*` | 注册格式 ID 的获取与缓存 | —— |
| `ClipboardWriter.*` | **写回编排**：决定条目以哪些格式写回（文本/HTML/RTF/文件标准格式 + 图片分发给 ImageWriteback） | 不读、不采集、不做按键派发 |

## 粘贴链路一层图（排查卡顿/跳顶时先看这里）

```
UI 线程: PasteById → ExecutePasteTransaction (AppController.Paste.cpp)
  ├─ SuppressClipboardMonitor + [baseline, final] sequence 区间武装   ← 跳顶防线①②
  ├─ WriteClipToClipboard (ClipboardWriter.cpp 编排)
  │    ├─ 图片 → ImageWriteback（WIC 解码 + 3 份像素拷贝，卡顿大头）
  │    └─ 文本/文件 → ClipboardSession 原语
  ├─ PerformPasteToTarget (PasteDispatch.cpp)
  │    ├─ RefinePasteTargetContext (PasteTarget)  ← 最多 8×10ms 轮询
  │    └─ Ctrl+V 派发（WaitForModifierRelease 最多 150ms + 前台轮询）
  └─ MarkPasted(promote_pasted_clip_on_reuse)                       ← 跳顶防线③
异步: 到达事件 → ConsumeClipboardTouchPolicy（5s identity_key 回声窗口）← 跳顶防线④
```

## 三条必须守住的红线（各对应一次实发回归）

1. **位置由「到达」决定。** 采集侧只负责给 `ReuseTouchPolicy`，不碰排序字段。
2. **DIB 的第 4 字节不是 alpha。** 任何新的解码/编码路径都必须过
   `NormalizeDibAlphaIfDegenerate`，否则会导出整张全透明的 PNG（"截图空白"）。
3. **身份只认规范像素。** 不允许直接哈希 PNG/DIB 编码字节 —— 写回剪贴板后
   编码会变，同一张图会被当成新条目（原仓库 4d9f22d 的教训）。

## 新增能力的落点

- 新格式（比如 WebP）→ 读：`ClipboardRead`；身份：`ImageIdentity` 里加一个分支；
  落盘：`ImagePng`。
- 新诊断字段 → `ClipboardDiagnostics`。
- 新的"决定要不要收这条"的逻辑 → `ClipboardCapture`（编排层），不要下沉到读取层。
