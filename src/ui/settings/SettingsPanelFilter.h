#pragma once

#include "ui/settings/ISettingsPanel.h"
#include <memory>

namespace clipd2d::ui {

class FlexNode;

class SettingsPanelFilter : public ISettingsPanel {
public:
    void Layout(float /*width*/, float& /*current_y*/, const DraftState& /*draft*/, ISettingsHost* /*host*/) override {}
    float Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) override;
    std::vector<SettingRow> BuildRows(const DraftState& draft, ISettingsHost* host) const override;

private:
    void BuildLayout(float width, float height, ISettingsHost* host);

    float last_width_ = 0.0f;
    float last_height_ = 0.0f;
    int last_scale_key_ = 0;
    float max_scroll_y_ = 0.0f;

    std::shared_ptr<FlexNode> root_;
    std::shared_ptr<FlexNode> c1_title_;
    std::shared_ptr<FlexNode> c1_;
    std::shared_ptr<FlexNode> r1_;
    std::shared_ptr<FlexNode> r1_txt_;
    std::shared_ptr<FlexNode> r1_t_;
    std::shared_ptr<FlexNode> r1_s_;
    std::shared_ptr<FlexNode> r1_c_;
    std::shared_ptr<FlexNode> r2_;
    std::shared_ptr<FlexNode> r2_txt_;
    std::shared_ptr<FlexNode> r2_t_;
    std::shared_ptr<FlexNode> r2_s_;
    std::shared_ptr<FlexNode> r2_c_;
    std::shared_ptr<FlexNode> r3_;
    std::shared_ptr<FlexNode> r3_txt_;
    std::shared_ptr<FlexNode> r3_t_;
    std::shared_ptr<FlexNode> r3_s_;
    std::shared_ptr<FlexNode> r3_c_;
    std::shared_ptr<FlexNode> c2_title_;
    std::shared_ptr<FlexNode> c2_;
    std::shared_ptr<FlexNode> r4_;
    std::shared_ptr<FlexNode> r4_txt_;
    std::shared_ptr<FlexNode> r4_t_;
    std::shared_ptr<FlexNode> r4_s_;
    std::shared_ptr<FlexNode> r4_c_input_;
    std::shared_ptr<FlexNode> r4_c_toggle_;
    std::shared_ptr<FlexNode> r5_;
    std::shared_ptr<FlexNode> r5_txt_;
    std::shared_ptr<FlexNode> r5_t_;
    std::shared_ptr<FlexNode> r5_s_;
    std::shared_ptr<FlexNode> r5_c_input_;
    std::shared_ptr<FlexNode> r5_c_toggle_;
    std::shared_ptr<FlexNode> c3_title_;
    std::shared_ptr<FlexNode> c3_;
    std::shared_ptr<FlexNode> r6_;
    std::shared_ptr<FlexNode> r7_;
    std::shared_ptr<FlexNode> r7_t_;
    std::shared_ptr<FlexNode> r7_s_;
    std::shared_ptr<FlexNode> r7_c_;
};

}  // namespace clipd2d::ui
