#include "core/clipboard/ClipboardFormat.h"

namespace clipd2d::core::clipboard {

UINT GetHtmlClipboardFormat() {
  static const UINT format = RegisterClipboardFormatA("HTML Format");
  return format;
}

UINT GetRtfClipboardFormat() {
  static const UINT format = RegisterClipboardFormatA("Rich Text Format");
  return format;
}

UINT GetPngClipboardFormat() {
  static const UINT format = RegisterClipboardFormatA("PNG");
  return format;
}

}  // namespace clipd2d::core::clipboard
