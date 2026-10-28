#include "ui/settings/SettingsSearchController.h"

#include <array>
#include <utility>

#include "common/StringUtil.h"

namespace clipd2d::ui {
namespace {

// 节名兜底关键词表：与历史 UpdateSearchSelection 逐词一致（语义原文迁移）。
constexpr std::array<Section, 7> kSections = {
    Section::General,
    Section::Appearance,
    Section::Hotkey,
    Section::Clipboard,
    Section::Filter,
    Section::Data,
    Section::About};
constexpr std::array<const wchar_t*, 7> kKeywords = {
    L"常规 历史 开机",
    L"外观 密度 图标",
    L"快捷键 热键 搜索",
    L"剪贴板 监听 位置 粘贴",
    L"过滤 黑名单",
    L"数据 清空 目录 大小",
    L"关于 版本 渲染"};

}  // namespace

SettingsSearchController::RebuildOutcome SettingsSearchController::Rebuild(
    std::wstring_view query,
    const std::vector<std::pair<Section, std::vector<SettingRow>>>& section_rows) {
  hits_.clear();
  hover_index_ = -1;
  window_offset_ = 0;
  query_ = common::TrimWide(query);
  if (query_.empty()) {
    return {};
  }

  for (const auto& [section, rows] : section_rows) {
    for (const SettingRow& row : rows) {
      if (row.title_text == nullptr || row.title_text[0] == L'\0') {
        continue;
      }
      if (!SettingRowMatches(row, query_)) {
        continue;
      }
      SettingsSearchHit hit;
      hit.section = section;
      hit.title = row.title_text;
      hit.subtitle = row.subtitle_text != nullptr ? row.subtitle_text : L"";
      hit.hit_kind = row.hit_kind;
      hit.field_id = row.field_id;
      hit.select_field = row.select_field;
      hit.hit_option = row.hit_option;
      hits_.push_back(std::move(hit));
    }
  }

  if (hits_.empty()) {
    // 没有行级命中时退回章节关键词匹配，「外观」「快捷键」这类节名仍然可用。
    return {false, MatchSection(query_)};
  }
  hover_index_ = 0;
  return {true, std::nullopt};
}

std::optional<Section> SettingsSearchController::MatchSection(std::wstring_view query) {
  const std::wstring trimmed = common::TrimWide(query);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  for (size_t index = 0; index < kSections.size(); ++index) {
    if (settings_row_detail::ContainsInsensitive(SectionTitle(kSections[index]), trimmed) ||
        settings_row_detail::ContainsInsensitive(kKeywords[index], trimmed)) {
      return kSections[index];
    }
  }
  return std::nullopt;
}

void SettingsSearchController::MoveHover(int delta) {
  if (hits_.empty()) {
    return;
  }
  const int total = static_cast<int>(hits_.size());
  int next = hover_index_ < 0 ? 0 : hover_index_ + delta;
  if (next < 0) {
    next = total - 1;
  }
  if (next >= total) {
    next = 0;
  }
  hover_index_ = next;
  if (next < window_offset_) {
    window_offset_ = next;
  }
  if (next >= window_offset_ + kMaxVisibleHits) {
    window_offset_ = next - kMaxVisibleHits + 1;
  }
}

void SettingsSearchController::SetHoverIndex(int index) {
  if (index < 0 || index >= static_cast<int>(hits_.size())) {
    return;
  }
  hover_index_ = index;
}

SettingsSearchController::Jump SettingsSearchController::Preview(int index) const {
  if (index < 0 || index >= static_cast<int>(hits_.size())) {
    return {};
  }
  const SettingsSearchHit& hit = hits_[static_cast<size_t>(index)];
  return Jump{hit.section, hit.title, true};
}

SettingsSearchController::Jump SettingsSearchController::Activate(int index) {
  if (index < 0 || index >= static_cast<int>(hits_.size())) {
    return {};
  }
  const SettingsSearchHit hit = hits_[static_cast<size_t>(index)];
  // 跳转即消费查询词：清空查询与结果，否则目标节会被继续过滤成「只剩一行」甚至空白。
  Close();
  return Jump{hit.section, hit.title, true};
}

void SettingsSearchController::Close() {
  hits_.clear();
  query_.clear();
  hover_index_ = -1;
  window_offset_ = 0;
}

int SettingsSearchController::HitAtPoint(const std::vector<RECT>& hit_rects, POINT pt) const {
  for (size_t slot = 0; slot < hit_rects.size(); ++slot) {
    RECT rect = hit_rects[slot];
    if (PtInRect(&rect, pt)) {
      return window_offset_ + static_cast<int>(slot);
    }
  }
  return -1;
}

}  // namespace clipd2d::ui
