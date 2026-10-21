#pragma once

#include "core/browser/BrowserPresentation.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::core::browser {

struct BrowserResult {
  store::ClipQuery query;
  BrowserPresentation presentation;
};

}  // namespace clipd2d::core::browser
