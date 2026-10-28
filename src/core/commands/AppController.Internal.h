#pragma once

// Responsibility: Shared AppController-only helpers and constants for clipboard, ingest, paste, and library implementation files.
// Inputs: AppController state, ClipRecord/CapturedClip values, and platform clipboard signals.
// Outputs: Internal-only helper results used by AppController implementation units.
// Forbidden dependencies: UI rendering concerns, repository SQL details, or direct window lifecycle management.

#include "core/commands/AppController.h"

#include <windows.h>

#include <array>
#include <filesystem>
#include <optional>

#include "common/ClipboardSuppression.h"
#include "common/Log.h"
#include "common/Stopwatch.h"
#include "common/StringUtil.h"
#include "common/Utf8.h"
#include "core/clipboard/ClipboardCapture.h"
#include "core/clipboard/ClipboardWriter.h"
#include "core/policies/ClipboardRetryPolicy.h"

namespace clipd2d::core::commands {

namespace {

const auto& kClipboardRetryDelaysMs = policies::ClipboardOpenRetryDelaysMs();
constexpr std::array<DWORD, 3> kDeferredClipboardRetryDelaysMs{150, 350, 700};

std::int64_t NowEpochMs() {
  FILETIME file_time{};
  GetSystemTimeAsFileTime(&file_time);
  ULARGE_INTEGER ticks{};
  ticks.LowPart = file_time.dwLowDateTime;
  ticks.HighPart = file_time.dwHighDateTime;
  constexpr std::uint64_t kEpochDiff = 116444736000000000ull;
  return static_cast<std::int64_t>((ticks.QuadPart - kEpochDiff) / 10000ull);
}

clipboard::ContentKind ToContentKind(store::ClipContentType content_type) {
  switch (content_type) {
    case store::ClipContentType::Text:
      return clipboard::ContentKind::Text;
    case store::ClipContentType::Html:
      return clipboard::ContentKind::Html;
    case store::ClipContentType::Rtf:
      return clipboard::ContentKind::RichText;
    case store::ClipContentType::Image:
      return clipboard::ContentKind::Image;
    case store::ClipContentType::Files:
      return clipboard::ContentKind::File;
  }
  return clipboard::ContentKind::Text;
}

std::string BuildPasteTextFallback(const store::ClipRecord& clip) {
  if (!clip.plain_text.empty()) {
    return clip.plain_text;
  }

  if (clip.summary.content_type == store::ClipContentType::Image) {
    if (!clip.secondary_path.empty()) {
      return common::WideToUtf8(clip.secondary_path).utf8;
    }
    if (!clip.storage_path.empty()) {
      return common::WideToUtf8(clip.storage_path).utf8;
    }
  }

  return {};
}

bool NeedsPendingImageIngest(const store::CapturedClip& clip) {
  return clip.content_type == store::ClipContentType::Image &&
      clip.ingest_state == "pending" &&
      (!clip.ingest_format.empty() || !clip.storage_path.empty());
}

bool IsTransientClipboardImageFailureMessage(std::string_view message) {
  return message.find("Image formats were advertised but no payload was ready.") != std::string_view::npos ||
      message.find("CF_BITMAP handle is not ready.") != std::string_view::npos ||
      message.find("Read bitmap info failed.") != std::string_view::npos ||
      message.find("Extract bitmap pixels failed.") != std::string_view::npos ||
      message.find("Open image stream failed.") != std::string_view::npos ||
      message.find("Decode image stream failed.") != std::string_view::npos ||
      message.find("Copy image pixels failed.") != std::string_view::npos;
}

bool ShouldRetryClipboardCapture(const common::Status& status) {
  return status.code == common::StatusCode::Unavailable ||
      ((status.code == common::StatusCode::PlatformError ||
        status.code == common::StatusCode::InvalidArgument ||
        status.code == common::StatusCode::NotFound) &&
          IsTransientClipboardImageFailureMessage(status.message));
}

std::wstring FirstFilePathFromClip(const store::ClipRecord& clip) {
  if (clip.summary.content_type != store::ClipContentType::Files) {
    return {};
  }
  if (!clip.storage_path.empty()) {
    return common::TrimWide(clip.storage_path);
  }
  if (!clip.secondary_path.empty()) {
    return common::TrimWide(clip.secondary_path);
  }
  return {};
}

bool IsImageStickerCandidate(const store::ClipRecord& clip) {
  if (clip.summary.content_type == store::ClipContentType::Image) {
    return true;
  }
  if (clip.summary.content_type != store::ClipContentType::Files || clip.summary.file_count != 1) {
    return false;
  }
  const std::wstring path = FirstFilePathFromClip(clip);
  const std::wstring extension = common::ToLowerWide(std::filesystem::path(path).extension().wstring());
  return extension == L".png" || extension == L".jpg" || extension == L".jpeg" ||
         extension == L".bmp" || extension == L".gif" || extension == L".webp" ||
         extension == L".tif" || extension == L".tiff" || extension == L".ico" ||
         extension == L".heic";
}

std::string BuildClipDebugSummary(const store::CapturedClip& clip) {
  std::string summary = "type=" + std::string(store::ToSqlContentType(clip.content_type));
  summary += " title=" + common::JsonEscape(clip.title);
  // 来源应用：图片类问题（1x1 垃圾条目、写回被当成外部复制）都需要知道"这是谁写的"，
  // 否则只能靠猜。
  if (!clip.source_app.process_name.empty()) {
    summary += " source=" + common::JsonEscape(common::WideToUtf8(clip.source_app.process_name).utf8);
  }
  if (!clip.plain_text.empty()) {
    const std::string snippet = clip.plain_text.substr(0, std::min<size_t>(clip.plain_text.size(), 32));
    summary += " text=" + common::JsonEscape(snippet);
  }
  if (clip.content_type == store::ClipContentType::Files) {
    const size_t file_count = std::count(clip.plain_text.begin(), clip.plain_text.end(), '\n') + (clip.plain_text.empty() ? 0 : 1);
    summary += " files=" + std::to_string(file_count);
  }
  if (clip.content_type == store::ClipContentType::Image) {
    summary += " image=" + std::to_string(clip.image_width) + "x" + std::to_string(clip.image_height);
  }
  if (!clip.content_hash.empty()) {
    summary += " hash=" + clip.content_hash.substr(0, std::min<size_t>(clip.content_hash.size(), 12));
  }
  return summary;
}

}  // namespace

namespace detail {

// OTP 形态：恰好 6 位纯数字（主流 TOTP / 短信验证码）。更宽的匹配会误杀
// 年份、版本号、短 ID 等普通文本。
inline bool IsLikelyOtpCode(std::string_view text) {
  if (text.size() != 6) {
    return false;
  }
  for (const char ch : text) {
    if (ch < '0' || ch > '9') {
      return false;
    }
  }
  return true;
}

inline std::optional<DWORD> DeferredClipboardRetryDelayMs(int deferred_attempt) {
  if (deferred_attempt < 0 || deferred_attempt >= static_cast<int>(kDeferredClipboardRetryDelaysMs.size())) {
    return std::nullopt;
  }
  return kDeferredClipboardRetryDelaysMs[static_cast<size_t>(deferred_attempt)];
}

inline bool ShouldSkipClipboardCapture(
    std::optional<store::ClipArrivalReason> reason,
    bool internal_writeback) {
  return reason.has_value() &&
      *reason == store::ClipArrivalReason::InternalStickerWriteback &&
      internal_writeback;
}

// 触摸策略判定（批次：剪贴板回声修复 3028baa 后重做）。
// internal_writeback 由入队时按 [baseline, final] 区间判定并随事件携带——
// 写回会话内 EmptyClipboard 与 CF_DIB 派生格式合成都会推进 sequence（单次写回可 +4），
// 任何「事件 sequence == armed 最终值」的相等判定都会被第一个事件击穿。
inline store::ReuseTouchPolicy ResolveClipboardTouchPolicy(
    std::optional<store::ClipArrivalReason> reason,
    bool internal_writeback,
    bool promote_internal_paste_on_reuse,
    bool* should_clear_state) {
  if (should_clear_state != nullptr) {
    *should_clear_state = false;
  }
  if (internal_writeback && reason.has_value()) {
    if (should_clear_state != nullptr) {
      *should_clear_state = true;
    }
    switch (*reason) {
      case store::ClipArrivalReason::InternalStickerWriteback:
        return store::ReuseTouchPolicy::KeepPositionOnReuse;
      case store::ClipArrivalReason::InternalClipboardWriteback:
        return promote_internal_paste_on_reuse
            ? store::ReuseTouchPolicy::PromoteOnReuse
            : store::ReuseTouchPolicy::KeepPositionOnReuse;
    }
  }
  if (should_clear_state != nullptr) {
    *should_clear_state = true;
  }
  return store::ReuseTouchPolicy::PromoteOnReuse;
}

// 内容身份判定：这次捕获是不是「刚刚那次内部写回」的回声。
// 为什么要它：写回判定原本只看 sequence 窗口 [baseline, final]，但图片写回会写
// PNG+DIB+DIBV5，目标应用首次打开剪贴板时 Windows 惰性合成派生格式，每个合成
// 推进一个 sequence。实测一次写回推进 7 个，事件到达时 sequence 已越过 final
// （日志 arrival-sequence expired expected=23013 actual=23018），写回被当成外部
// 复制 → PromoteOnReuse → 跳顶。图片身份按归一化像素计算、跨格式稳定，用它兜底。
inline bool MatchesInternalWritebackIdentity(
    std::string_view clip_identity_key,
    std::string_view last_writeback_identity_key,
    std::uint64_t age_ms,
    std::uint64_t window_ms) {
  if (last_writeback_identity_key.empty() || clip_identity_key.empty()) {
    return false;
  }
  return clip_identity_key == last_writeback_identity_key && age_ms < window_ms;
}

inline RefreshRequest BuildClipboardRefreshRequest(
    store::ReuseTouchPolicy touch_policy,
    std::optional<std::int64_t> clip_id) {
  if (touch_policy == store::ReuseTouchPolicy::PromoteOnReuse) {
    return MakeJumpToTopRefreshRequest(clip_id, RefreshSelectionPolicy::SelectTargetClip);
  }
  return MakePreserveViewportRefreshRequest(clip_id, RefreshSelectionPolicy::PreserveCurrent);
}

}  // namespace detail

}  // namespace clipd2d::core::commands
