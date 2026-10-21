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
    DWORD expected_sequence,
    DWORD actual_sequence) {
  return reason.has_value() &&
      *reason == store::ClipArrivalReason::InternalStickerWriteback &&
      expected_sequence != 0 &&
      actual_sequence == expected_sequence;
}

inline store::ReuseTouchPolicy ResolveClipboardTouchPolicy(
    std::optional<store::ClipArrivalReason> reason,
    DWORD expected_sequence,
    DWORD actual_sequence,
    bool promote_internal_paste_on_reuse,
    bool* should_clear_state) {
  if (should_clear_state != nullptr) {
    *should_clear_state = false;
  }
  if (!reason.has_value() || expected_sequence == 0) {
    if (should_clear_state != nullptr) {
      *should_clear_state = true;
    }
    return store::ReuseTouchPolicy::PromoteOnReuse;
  }
  if (actual_sequence != expected_sequence) {
    if (actual_sequence > expected_sequence && should_clear_state != nullptr) {
      *should_clear_state = true;
    }
    return store::ReuseTouchPolicy::PromoteOnReuse;
  }
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
    case store::ClipArrivalReason::ExternalCapture:
    default:
      return store::ReuseTouchPolicy::PromoteOnReuse;
  }
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
