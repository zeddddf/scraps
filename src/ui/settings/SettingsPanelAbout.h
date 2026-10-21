#pragma once

#include "ui/settings/ISettingsPanel.h"
#include <memory>

namespace clipd2d::ui {

class FlexNode;

class SettingsPanelAbout : public ISettingsPanel {
public:
    void Layout(float /*width*/, float& /*current_y*/, const DraftState& /*draft*/, ISettingsHost* /*host*/) override {}
    float Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) override;

private:
    void BuildLayout(float width, float height, ISettingsHost* host);

    float last_width_ = 0.0f;
    float last_height_ = 0.0f;
    int last_scale_key_ = 0;
    float max_scroll_y_ = 0.0f;

    std::shared_ptr<FlexNode> root_;
    std::shared_ptr<FlexNode> title_node_;
    std::shared_ptr<FlexNode> card_;
    std::shared_ptr<FlexNode> row1_;
    std::shared_ptr<FlexNode> row1_title_;
    std::shared_ptr<FlexNode> row1_subtitle_;
    std::shared_ptr<FlexNode> row2_;
    std::shared_ptr<FlexNode> row2_toggle_;
};

}  // namespace clipd2d::ui
