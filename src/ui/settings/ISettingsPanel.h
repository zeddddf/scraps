#pragma once

#include <windows.h>
#include <d2d1.h>

#include "ui/settings/SettingsTypes.h"

namespace clipd2d::ui {

class ISettingsHost;

class ISettingsPanel {
public:
    virtual ~ISettingsPanel() = default;

    virtual void Layout(float width, float& current_y, const DraftState& draft, ISettingsHost* host) = 0;
    virtual float Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) = 0;
};

}  // namespace clipd2d::ui
