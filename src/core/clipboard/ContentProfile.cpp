#include "core/clipboard/ContentProfile.h"

#include <algorithm>
#include <cctype>

namespace clipd2d::core::clipboard {

namespace {

std::string TrimAscii(std::string value) {
  const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };

  const auto begin = std::find_if(value.begin(), value.end(), not_space);
  const auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
  if (begin >= end) {
    return {};
  }

  return std::string(begin, end);
}

bool LooksLikeJson(std::string_view text) {
  const std::string trimmed = TrimAscii(std::string(text));
  if (trimmed.size() < 2) {
    return false;
  }

  const bool object_like = trimmed.front() == '{' && trimmed.back() == '}';
  const bool array_like = trimmed.front() == '[' && trimmed.back() == ']';
  return object_like || array_like;
}

bool LooksUrlEncoded(std::string_view text) {
  bool has_percent_escape = false;
  bool has_plus = false;

  for (size_t index = 0; index < text.size(); ++index) {
    const unsigned char current = static_cast<unsigned char>(text[index]);
    if (current == '+') {
      has_plus = true;
      continue;
    }

    if (current != '%' || index + 2 >= text.size()) {
      continue;
    }

    const auto is_hex = [](unsigned char ch) {
      return std::isdigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    };
    if (is_hex(static_cast<unsigned char>(text[index + 1])) &&
        is_hex(static_cast<unsigned char>(text[index + 2]))) {
      has_percent_escape = true;
      break;
    }
  }

  return has_percent_escape || has_plus;
}

bool CanProjectToText(ContentKind kind) {
  return kind == ContentKind::Text || kind == ContentKind::RichText || kind == ContentKind::Html;
}

}  // namespace

ContentProfile BuildContentProfile(
    ContentKind primary_kind,
    std::string plain_text,
    bool has_html_payload) {
  ContentProfile profile;
  profile.primary_kind = primary_kind;
  profile.plain_text = std::move(plain_text);
  profile.has_html_payload = has_html_payload;
  profile.can_project_to_text = CanProjectToText(primary_kind);
  profile.has_trim_candidate = TrimAscii(profile.plain_text) != profile.plain_text;
  profile.looks_url_encoded = LooksUrlEncoded(profile.plain_text);
  profile.looks_like_json = LooksLikeJson(profile.plain_text);
  if (profile.can_project_to_text) {
    profile.open_target = ResolveOpenTargetFromText(profile.plain_text);
  }
  return profile;
}

const char* ToString(ContentKind kind) {
  switch (kind) {
    case ContentKind::Text:
      return "text";
    case ContentKind::RichText:
      return "rich_text";
    case ContentKind::Html:
      return "html";
    case ContentKind::Image:
      return "image";
    case ContentKind::File:
      return "file";
  }

  return "unknown";
}

}  // namespace clipd2d::core::clipboard
