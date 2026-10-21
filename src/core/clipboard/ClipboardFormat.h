#pragma once

#include <windows.h>

namespace clipd2d::core::clipboard {

UINT GetHtmlClipboardFormat();
UINT GetRtfClipboardFormat();
UINT GetPngClipboardFormat();

}  // namespace clipd2d::core::clipboard
