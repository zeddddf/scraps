#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "common/Status.h"
#include "core/store/ClipTypes.h"
#include "ui/FluentIcons.h"
#include "ui/ClipVisualAssets.h" // For ShellIconPurpose

namespace clipd2d::ui {

enum class ToolbarCommand {
  PanelLock = 0,
  Settings = 1,
  More = 2
};

struct PanelLayout {
  RECT client{};
  RECT surface{};
  RECT header{};
  RECT search_box{};
  RECT toolbar[3]{};
  RECT tabs[7]{};
  RECT nav_actions[2]{};
  RECT chips_bar{};
  RECT list{};
  RECT list_top_reveal{};
  RECT back_to_top{};
  RECT bottom_bar{};
  RECT status_left{};
  RECT status_right{};
  RECT scrollbar_track{};
};

} // namespace clipd2d::ui
