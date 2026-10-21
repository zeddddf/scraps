#pragma once

// P3-16 IRowRenderer 注册表：列表行内容渲染按 ClipContentType 分发到
// 文本/图片/文件三个实现。行框架（背景/分隔线/骨架屏/头部/操作按钮）仍由
// MainWindow::RenderListRow 绘制；本头只负责 content_rect 内的类型化内容。
// 行高/度量仍归 RowHeightModel（虚拟列表高度索引依赖其一致性，不在本层）。
//
// 用法：RenderListRow 组装 RowContentContext → RowContentRendererFor(type)
//      返回对应单态渲染器 → Render(ctx)。

#include <d2d1_1.h>
#include <dwrite.h>

#include <functional>
#include <string>
#include <vector>

#include "core/store/ClipTypes.h"
#include "ui/ClipImagePreview.h"
#include "ui/ClipVisualAssets.h"
#include "ui/ClipVisualFileContent.h"
#include "ui/D2dShell.h"
#include "ui/MainWindowInternal.h"

namespace clipd2d::ui {

enum class RowBitmapKind {
  Thumbnail,     // 缩略图（图片/文件卡）
  FileItemIcon,  // 文件项 shell 图标（多文件/无预览路径）
};

struct RowBitmapRef {
  ID2D1Bitmap* bitmap = nullptr;
  UINT width = 0;
  UINT height = 0;
};

// 缓存解析 + 未命中时触发异步加载（由 MainWindow 提供）。
using RowBitmapResolver =
    std::function<RowBitmapRef(RowBitmapKind, const core::store::ClipSummary&)>;

struct RowContentContext {
  ID2D1RenderTarget* target = nullptr;
  RECT content_rect{};
  const core::store::ClipSummary* summary = nullptr;
  D2dShell* shell = nullptr;
  TextLayoutCache* text_layout_cache = nullptr;
  const std::vector<std::wstring>* search_tokens = nullptr;
  RowBitmapResolver resolve_bitmap;
};

class IRowContentRenderer {
 public:
  virtual ~IRowContentRenderer() = default;
  virtual void Render(const RowContentContext& ctx) = 0;
};

// ── 文本（及 Html/Rtf 等纯文本形态）────────────────────────────────────
class TextRowRenderer final : public IRowContentRenderer {
 public:
  void Render(const RowContentContext& ctx) override {
    const std::wstring content_text = RowContentText(*ctx.summary);
    const bool has_tokens = ctx.search_tokens != nullptr && !ctx.search_tokens->empty();
    if (has_tokens) {
      DrawHighlightedText(
          ctx.shell->dwrite_factory(),
          ctx.target,
          content_text,
          ctx.shell->small_format(),
          ctx.shell->text_brush(),
          ctx.shell->accent_soft_brush(),
          ctx.content_rect,
          false,
          *ctx.search_tokens);
    } else {
      DrawCachedText(
          ctx.text_layout_cache,
          ctx.shell->dwrite_factory(),
          ctx.target,
          *ctx.summary,
          CachedTextRole::Content,
          content_text,
          ctx.shell->small_format(),
          ctx.shell->text_brush(),
          ctx.content_rect,
          false);
    }
  }
};

// ── 图片 ──────────────────────────────────────────────────────────────
class ImageRowRenderer final : public IRowContentRenderer {
 public:
  void Render(const RowContentContext& ctx) override {
    RowBitmapRef ref;
    if (ctx.resolve_bitmap) {
      ref = ctx.resolve_bitmap(RowBitmapKind::Thumbnail, *ctx.summary);
    }
    image_preview::Draw(
        ctx.target,
        *ctx.shell,
        ref.bitmap,
        ref.width,
        ref.height,
        ctx.content_rect);
  }
};

// ── 文件 ──────────────────────────────────────────────────────────────
class FileRowRenderer final : public IRowContentRenderer {
 public:
  void Render(const RowContentContext& ctx) override {
    const core::store::ClipSummary& summary = *ctx.summary;
    const std::wstring first_path = FirstFilePathFromSummary(summary);
    const std::wstring display_name =
        summary.file_count > 1 ? SummaryTitle(summary) : FileDisplayName(first_path);
    const std::wstring path_line = FilePathLine(summary, first_path);
    const std::wstring trailing_text = FileTrailingText(summary, first_path);
    const bool supports_visual_thumbnail = SupportsSystemThumbnailPreviewPath(first_path);
    const std::wstring secondary_text = !path_line.empty() ? path_line : trailing_text;

    if (supports_visual_thumbnail) {
      RowBitmapRef ref;
      if (ctx.resolve_bitmap) {
        ref = ctx.resolve_bitmap(RowBitmapKind::Thumbnail, summary);
      }
      file_content_preview::Draw(
          ctx.target,
          *ctx.shell,
          ref.bitmap,
          ref.width,
          ref.height,
          ctx.content_rect,
          display_name,
          secondary_text,
          summary.file_count,
          file_content_preview::FileVisualMode::ThumbnailCard);
    } else {
      RowBitmapRef ref;
      if (ctx.resolve_bitmap) {
        ref = ctx.resolve_bitmap(RowBitmapKind::FileItemIcon, summary);
      }
      file_content_preview::Draw(
          ctx.target,
          *ctx.shell,
          ref.bitmap,
          ref.width,
          ref.height,
          ctx.content_rect,
          display_name,
          secondary_text,
          summary.file_count,
          summary.file_count <= 1
              ? file_content_preview::FileVisualMode::SingleFileIcon
              : file_content_preview::FileVisualMode::IconOnly);
    }
  }
};

// 注册表：按内容类型取渲染器单态（无每帧分配/无虚表查表开销以外的代价）。
inline IRowContentRenderer& RowContentRendererFor(core::store::ClipContentType type) {
  static TextRowRenderer text_renderer;
  static ImageRowRenderer image_renderer;
  static FileRowRenderer file_renderer;
  switch (type) {
    case core::store::ClipContentType::Image:
      return image_renderer;
    case core::store::ClipContentType::Files:
      return file_renderer;
    default:
      return text_renderer;
  }
}

}  // namespace clipd2d::ui
