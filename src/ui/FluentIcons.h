#pragma once

#include <d2d1.h>

namespace clipd2d::ui {

enum class FluentIcon {
  Search = 0,
  PanelPin,
  PanelPinned,
  Settings,
  More,
  List,
  StarOutline,
  StarFilled,
  Text,
  Code,
  Image,
  Folder,
  Group,
  Rename,
  Delete,
  Clipboard,
  Palette,
  Keyboard,
  Filter,
  Options,
  Info,
};

bool DrawFluentIcon(
    ID2D1Factory* factory,
    ID2D1RenderTarget* target,
    FluentIcon icon,
    const D2D1_RECT_F& rect,
    ID2D1Brush* brush,
    float weight_boost_dip = 0.0f);

}  // namespace clipd2d::ui
