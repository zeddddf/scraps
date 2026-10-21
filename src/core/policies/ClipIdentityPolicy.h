#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/store/ClipTypes.h"

namespace clipd2d::core::policies {

// Responsibility: Compute clipboard dedupe identities and file snapshot keys for repository use.
// Inputs: CapturedClip payloads and filesystem-backed image-file paths.
// Outputs: Stable identity metadata and optional file snapshot descriptors.
// Forbidden dependencies: UI state, clipboard monitoring loops, or direct database writes.

struct FileSnapshotIdentityData {
  std::wstring normalized_path;
  std::uint64_t file_size = 0;
  std::uint64_t last_write_time_utc = 0;
};

std::string NormalizeLineEndings(std::string_view text);
std::string NormalizeTextIdentityField(std::string_view text);
std::string NormalizeStructuredTextIdentityField(std::string_view text);
std::wstring NormalizeWindowsPathForIdentity(std::wstring_view path);
std::optional<FileSnapshotIdentityData> ReadFileSnapshotIdentityData(std::wstring_view path);
std::string BuildTextPayloadSetIdentityKey(const store::CapturedClip& clip);
std::string BuildImageFileSnapshotIdentityKey(const FileSnapshotIdentityData& snapshot);
store::ClipIdentityKind ResolveIdentityKind(const store::CapturedClip& clip);
std::string ResolveIdentityKey(const store::CapturedClip& clip, store::ClipIdentityKind kind);
store::ClipIdentityKind ParseSqlIdentityKind(std::string_view value);
const char* ToSqlIdentityKind(store::ClipIdentityKind kind);

}  // namespace clipd2d::core::policies
