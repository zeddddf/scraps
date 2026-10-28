#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

#include "common/Paths.h"
#include "common/Status.h"
#include "core/store/ClipTypes.h"

// 本模块按能力拆成 6 个实现单元（都在 clipd2d::core::clipboard 里）：
//   ClipboardRead       读已打开剪贴板的字节（文本/二进制/HDROP）+ ClipboardScope
//   ClipboardDiagnostics 自检：此刻剪贴板上有什么、归谁所有
//   ImageDib            DIB/BITMAP 像素层（头部解析、像素哈希、保留位归一化）
//   ImagePng            位图/DIB/PNG 编码落盘
//   ImageIdentity       图片内容身份（规范像素哈希）
//   ProcessInfo         进程/窗口元数据
// 本文件只保留「采集编排」的对外契约；其余能力请直接 include 对应头文件。
#include "core/clipboard/ClipboardDiagnostics.h"
#include "core/clipboard/ClipboardRead.h"
#include "core/clipboard/ImageDib.h"
#include "core/clipboard/ImageIdentity.h"
#include "core/clipboard/ImagePng.h"
#include "core/clipboard/ProcessInfo.h"

namespace clipd2d::core::clipboard {

// 采集编排：打开剪贴板 → 判定内容类型 → 抽取负载 → 算出待落盘任务的元数据。
// 不写盘、不写库、不懂「条目」。
common::Status CaptureClipboard(
    const common::AppPaths& paths,
    HWND source_window,
    store::CapturedClip* clip);

// 落盘编排：把 CapturedClip 里暂存的负载真正编码成 PNG + 缩略图。
common::Status NormalizePendingImage(
    const common::AppPaths& paths,
    const store::CapturedClip& pending_clip,
    std::string* content_hash,
    int* image_width,
    int* image_height);

}  // namespace clipd2d::core::clipboard
