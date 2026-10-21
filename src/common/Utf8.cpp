#include "common/Utf8.h"

#include <windows.h>

namespace clipd2d::common {

Utf8Conversion Utf8ToWide(std::string_view utf8_text) {
  Utf8Conversion result;

  if (utf8_text.empty()) {
    result.status = Status::Ok();
    return result;
  }

  const int required = MultiByteToWideChar(
      CP_UTF8,
      MB_ERR_INVALID_CHARS,
      utf8_text.data(),
      static_cast<int>(utf8_text.size()),
      nullptr,
      0);
  if (required <= 0) {
    result.status = Status::Error(StatusCode::InvalidArgument, "UTF-8 转 UTF-16 失败。");
    return result;
  }

  result.wide.resize(static_cast<size_t>(required));
  const int written = MultiByteToWideChar(
      CP_UTF8,
      MB_ERR_INVALID_CHARS,
      utf8_text.data(),
      static_cast<int>(utf8_text.size()),
      result.wide.data(),
      required);
  if (written != required) {
    result.status = Status::Error(StatusCode::InvalidArgument, "UTF-8 转 UTF-16 长度不匹配。");
    result.wide.clear();
    return result;
  }

  result.status = Status::Ok();
  return result;
}

Utf8Conversion WideToUtf8(std::wstring_view wide_text) {
  Utf8Conversion result;

  if (wide_text.empty()) {
    result.status = Status::Ok();
    return result;
  }

  const int required = WideCharToMultiByte(
      CP_UTF8,
      0,
      wide_text.data(),
      static_cast<int>(wide_text.size()),
      nullptr,
      0,
      nullptr,
      nullptr);
  if (required <= 0) {
    result.status = Status::Error(StatusCode::InvalidArgument, "UTF-16 转 UTF-8 失败。");
    return result;
  }

  result.utf8.resize(static_cast<size_t>(required));
  const int written = WideCharToMultiByte(
      CP_UTF8,
      0,
      wide_text.data(),
      static_cast<int>(wide_text.size()),
      result.utf8.data(),
      required,
      nullptr,
      nullptr);
  if (written != required) {
    result.status = Status::Error(StatusCode::InvalidArgument, "UTF-16 转 UTF-8 长度不匹配。");
    result.utf8.clear();
    return result;
  }

  result.status = Status::Ok();
  return result;
}

}  // namespace clipd2d::common
