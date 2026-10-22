#pragma once

#include <windows.h>
#include <d2d1.h>

#include <string_view>
#include <utility>
#include <vector>

#include "ui/settings/SettingsRow.h"
#include "ui/settings/SettingsTypes.h"

namespace clipd2d::ui {

class ISettingsHost;

class ISettingsPanel {
public:
    virtual ~ISettingsPanel() = default;

    virtual void Layout(float width, float& current_y, const DraftState& draft, ISettingsHost* host) = 0;
    virtual float Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) = 0;

    // 行声明（标题/副题/控件值）的唯一来源：Render 用它绘制，搜索索引也用它，
    // 不产生绘制副作用。布局未构建时节点指针为空，文本仍然有效。
    virtual std::vector<SettingRow> BuildRows(const DraftState& draft, ISettingsHost* host) const = 0;

    // 最近一次 Render 实际绘制（已按搜索词过滤）的行。搜索跳转据此定位行位置，
    // 因此宿主必须无条件调用，未绘制（空态）时也要传入空集。
    void SetDrawnRows(std::vector<SettingRow> rows) { drawn_rows_ = std::move(rows); }
    const std::vector<SettingRow>& GetDrawnRows() const { return drawn_rows_; }

    // 按行标题取行顶部相对面板顶部的 Y 偏移；标题不存在或行节点缺失返回 -1。
    float FindRowTopByTitle(std::wstring_view title) const {
        if (title.empty()) {
            return -1.0f;
        }
        for (const auto& row : drawn_rows_) {
            if (row.row == nullptr || row.title_text == nullptr) {
                continue;
            }
            if (title != std::wstring_view(row.title_text)) {
                continue;
            }
            return static_cast<float>(row.row->GetAbsoluteRect(0.0f, 0.0f).top);
        }
        return -1.0f;
    }

private:
    std::vector<SettingRow> drawn_rows_;
};

}  // namespace clipd2d::ui
