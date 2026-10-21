#pragma once

#include <memory>
#include <string>

#include "common/Status.h"

namespace clipd2d::core::ocr {

struct ImageOcrRequest {
  std::wstring image_path;
  std::wstring language_hint;
};

struct ImageOcrResult {
  std::string utf8_text;
  std::string engine_name;
  std::string language_used;
};

class ImageOcrProvider {
 public:
  virtual ~ImageOcrProvider() = default;

  virtual common::Status Recognize(const ImageOcrRequest& request, ImageOcrResult* result) = 0;
};

std::unique_ptr<ImageOcrProvider> CreateWindowsOcrProvider();

}  // namespace clipd2d::core::ocr
