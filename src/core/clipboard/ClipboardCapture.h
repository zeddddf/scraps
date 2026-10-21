#pragma once

#include <windows.h>

#include <string>
#include <string_view>

#include "common/Paths.h"
#include "common/Status.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::core::clipboard {

// Result of a size-bounded HGLOBAL text read. Exposed so tests can exercise the
// truncation behavior against hostile (non NUL-terminated) clipboard payloads.
struct ReadGlobalTextResult {
  common::Status status = common::Status::Ok();
  std::string text;
};

// Both readers never scan past GlobalSize(handle) and never require the source
// block to be NUL-terminated.
ReadGlobalTextResult ReadGlobalText(HGLOBAL handle);
ReadGlobalTextResult ReadAnsiGlobal(HGLOBAL handle);

common::Status CaptureClipboard(
    const common::AppPaths& paths,
    HWND source_window,
    store::CapturedClip* clip);

common::Status ComputeCanonicalImageContentHashFromBytes(
    std::string_view format_name,
    const std::vector<std::uint8_t>& image_bytes,
    std::string* content_hash,
    int* width,
    int* height);

common::Status NormalizePendingImage(
    const common::AppPaths& paths,
    const store::CapturedClip& pending_clip,
    std::string* content_hash,
    int* image_width,
    int* image_height);

}  // namespace clipd2d::core::clipboard
