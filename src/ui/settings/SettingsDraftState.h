#pragma once

// SettingsDraftState —— 设置草稿域的纯逻辑核（业务核抽取计划 §2）。
// 负责：草稿加载/回建、数值与热键校验、分节脏标记、分节重置。
// 不负责：任何 HWND / D2D / textbox / 编辑会话（EndEditing 等留在 SettingsWindow 壳）。
// 依赖：core/store::AppSettings 值契约 + platform::win32 热键解析（纯解析，无副作用）。

#include <string>

#include "core/store/ClipTypes.h"
#include "ui/settings/SettingsTypes.h"

namespace clipd2d::ui {

class SettingsDraftState {
 public:
  // settings → 草稿（含 quick_paste/search_focus 空值缺省回填）。
  void LoadFrom(const core::store::AppSettings& settings);
  // 草稿 → settings；数字解析失败字段回退 base（语义与原 BuildSettingsFromDraft 一致）。
  core::store::AppSettings BuildSettings(const core::store::AppSettings& base) const;

  // 校验全部草稿规则（数值区间/必填/热键格式）。false 时写 error_message。
  bool Validate(std::wstring* error_message) const;

  // 分节脏检查（导航脏点与保存按钮态的共同依据）。cur 由参数注入，保持纯函数。
  bool IsSectionDirty(Section section, const core::store::AppSettings& next,
                      const core::store::AppSettings& cur) const;
  // 位序 = Section 枚举序（General=bit0 … About=bit6）。
  unsigned ComputeDirtyMask(const core::store::AppSettings& cur) const;
  bool IsDirty(const core::store::AppSettings& cur) const;

  // 只重置本节字段到 AppSettings 默认值，不碰其它节。
  void ResetSectionToDefaults(Section section);

  const DraftState& draft() const { return draft_; }
  DraftState& draft() { return draft_; }

 private:
  DraftState draft_{};
};

}  // namespace clipd2d::ui
