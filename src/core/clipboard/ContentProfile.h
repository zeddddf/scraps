#pragma once

#include <string>

#include "core/clipboard/OpenTargetResolver.h"

namespace clipd2d::core::clipboard {

enum class ContentKind {
  Text,
  RichText,
  Html,
  Image,
  File,
};

struct ContentProfile {
  ContentKind primary_kind = ContentKind::Text;
  std::string plain_text;
  bool has_html_payload = false;
  bool can_project_to_text = true;
  bool has_trim_candidate = false;
  bool looks_url_encoded = false;
  bool looks_like_json = false;
  OpenTarget open_target;
};

ContentProfile BuildContentProfile(
    ContentKind primary_kind,
    std::string plain_text,
    bool has_html_payload);

const char* ToString(ContentKind kind);

}  // namespace clipd2d::core::clipboard
