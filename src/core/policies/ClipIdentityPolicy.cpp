#include "core/policies/ClipIdentityPolicy.h"

#include <windows.h>

#include <filesystem>

#include "common/FileHash.h"
#include "common/StringUtil.h"
#include "common/Utf8.h"

namespace clipd2d::core::policies {

std::string NormalizeLineEndings(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (ch == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') {
        ++i;
      }
      normalized.push_back('\n');
    } else {
      normalized.push_back(ch);
    }
  }
  return normalized;
}

std::string NormalizeTextIdentityField(std::string_view text) {
  return common::TrimAscii(NormalizeLineEndings(text));
}

std::string NormalizeStructuredTextIdentityField(std::string_view text) {
  return NormalizeLineEndings(text);
}

std::wstring NormalizeWindowsPathForIdentity(std::wstring_view path) {
  if (path.empty()) {
    return {};
  }

  std::error_code ec;
  std::filesystem::path normalized = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
  if (ec) {
    ec.clear();
    normalized = std::filesystem::absolute(std::filesystem::path(path), ec);
    if (ec) {
      normalized = std::filesystem::path(path);
    }
    normalized = normalized.lexically_normal();
  }
  normalized.make_preferred();
  return common::ToLowerWide(normalized.wstring());
}

std::optional<FileSnapshotIdentityData> ReadFileSnapshotIdentityData(std::wstring_view path) {
  const std::wstring normalized_path = NormalizeWindowsPathForIdentity(path);
  if (normalized_path.empty()) {
    return std::nullopt;
  }

  WIN32_FILE_ATTRIBUTE_DATA attributes{};
  if (!GetFileAttributesExW(normalized_path.c_str(), GetFileExInfoStandard, &attributes)) {
    return std::nullopt;
  }
  if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return std::nullopt;
  }

  ULARGE_INTEGER size{};
  size.LowPart = attributes.nFileSizeLow;
  size.HighPart = attributes.nFileSizeHigh;

  ULARGE_INTEGER last_write{};
  last_write.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
  last_write.HighPart = attributes.ftLastWriteTime.dwHighDateTime;

  FileSnapshotIdentityData snapshot;
  snapshot.normalized_path = normalized_path;
  snapshot.file_size = size.QuadPart;
  snapshot.last_write_time_utc = last_write.QuadPart;
  return snapshot;
}

std::string BuildTextPayloadSetIdentityKey(const store::CapturedClip& clip) {
  std::string payload;
  payload.reserve(clip.plain_text.size() + clip.html_text.size() + clip.rtf_text.size() + 32);
  payload.append(store::ToSqlContentType(clip.content_type));
  payload.push_back('\0');
  payload.append(NormalizeTextIdentityField(clip.plain_text));
  payload.push_back('\0');
  payload.append(NormalizeStructuredTextIdentityField(clip.html_text));
  payload.push_back('\0');
  payload.append(NormalizeStructuredTextIdentityField(clip.rtf_text));
  return common::ContentIdentityHex(payload);
}

std::string BuildImageFileSnapshotIdentityKey(const FileSnapshotIdentityData& snapshot) {
  std::string payload;
  payload.reserve(snapshot.normalized_path.size() * 2 + 64);
  payload.append(common::WideToUtf8(snapshot.normalized_path).utf8);
  payload.push_back('\0');
  payload.append(std::to_string(snapshot.file_size));
  payload.push_back('\0');
  payload.append(std::to_string(snapshot.last_write_time_utc));
  return common::ContentIdentityHex(payload);
}

store::ClipIdentityKind ResolveIdentityKind(const store::CapturedClip& clip) {
  switch (clip.content_type) {
    case store::ClipContentType::Text:
    case store::ClipContentType::Html:
    case store::ClipContentType::Rtf:
      return store::ClipIdentityKind::TextPayloadSet;
    case store::ClipContentType::Files: {
      const std::wstring wide = common::Utf8ToWide(clip.plain_text).wide;
      const auto parts = common::SplitWide(wide, L'\n');
      std::vector<std::wstring> paths;
      for (const auto& part : parts) {
        const std::wstring trimmed = common::TrimWide(part);
        if (!trimmed.empty()) {
          paths.push_back(trimmed);
        }
      }
      if (paths.size() == 1) {
        const std::wstring lowered = common::ToLowerWide(paths.front());
        if (lowered.ends_with(L".png") || lowered.ends_with(L".jpg") || lowered.ends_with(L".jpeg") ||
            lowered.ends_with(L".bmp") || lowered.ends_with(L".gif") || lowered.ends_with(L".webp") ||
            lowered.ends_with(L".tif") || lowered.ends_with(L".tiff") || lowered.ends_with(L".ico") ||
            lowered.ends_with(L".heic")) {
          if (ReadFileSnapshotIdentityData(paths.front()).has_value()) {
            return store::ClipIdentityKind::ImageFileSnapshot;
          }
        }
      }
      return store::ClipIdentityKind::LegacyPayload;
    }
    case store::ClipContentType::Image:
      return store::ClipIdentityKind::ImageContentHash;
  }
  return store::ClipIdentityKind::LegacyPayload;
}

std::string ResolveIdentityKey(const store::CapturedClip& clip, store::ClipIdentityKind kind) {
  switch (kind) {
    case store::ClipIdentityKind::TextPayloadSet:
      return BuildTextPayloadSetIdentityKey(clip);
    case store::ClipIdentityKind::ImageFileSnapshot: {
      const std::wstring wide = common::Utf8ToWide(clip.plain_text).wide;
      for (const auto& part : common::SplitWide(wide, L'\n')) {
        const std::wstring trimmed = common::TrimWide(part);
        if (trimmed.empty()) {
          continue;
        }
        const auto snapshot = ReadFileSnapshotIdentityData(trimmed);
        if (snapshot.has_value()) {
          return BuildImageFileSnapshotIdentityKey(*snapshot);
        }
        break;
      }
      return clip.content_hash;
    }
    case store::ClipIdentityKind::ImageContentHash:
    case store::ClipIdentityKind::LegacyPayload:
      return clip.content_hash;
    case store::ClipIdentityKind::TextTitlePlain:
    case store::ClipIdentityKind::ImageFilePath:
    default:
      return clip.content_hash;
  }
}

const char* ToSqlIdentityKind(store::ClipIdentityKind kind) {
  switch (kind) {
    case store::ClipIdentityKind::TextTitlePlain:
      return "text_title_plain";
    case store::ClipIdentityKind::TextPayloadSet:
      return "text_payload_set";
    case store::ClipIdentityKind::ImageFilePath:
      return "image_file_path";
    case store::ClipIdentityKind::ImageFileSnapshot:
      return "image_file_snapshot";
    case store::ClipIdentityKind::ImageContentHash:
      return "image_content_hash";
    case store::ClipIdentityKind::LegacyPayload:
    default:
      return "legacy_payload";
  }
}

store::ClipIdentityKind ParseSqlIdentityKind(std::string_view value) {
  if (value == "text_title_plain") {
    return store::ClipIdentityKind::TextTitlePlain;
  }
  if (value == "text_payload_set") {
    return store::ClipIdentityKind::TextPayloadSet;
  }
  if (value == "image_file_path") {
    return store::ClipIdentityKind::ImageFilePath;
  }
  if (value == "image_file_snapshot") {
    return store::ClipIdentityKind::ImageFileSnapshot;
  }
  if (value == "image_content_hash" || value == "image_metadata") {
    return store::ClipIdentityKind::ImageContentHash;
  }
  return store::ClipIdentityKind::LegacyPayload;
}

}  // namespace clipd2d::core::policies
