#pragma once

#include <optional>

#include "core/browser/BrowserCommand.h"
#include "core/browser/BrowserPresentation.h"
#include "core/browser/BrowserResult.h"
#include "core/browser/ClipQueryBuilder.h"

namespace clipd2d::core::browser {

class ClipBrowserService {
 public:
  BrowserState ApplyCommand(BrowserState state, const BrowserCommand& command) const;
  store::ClipQuery BuildQuery(const BrowserState& state) const;
  BrowserPresentation BuildPresentation(const BrowserState& state) const;
  BrowserResult BuildResult(const BrowserState& state) const;
  std::optional<BrowserCommand> BuildRemoveChipCommand(const BrowserState& state, const FilterChip& chip) const;
};

}  // namespace clipd2d::core::browser
