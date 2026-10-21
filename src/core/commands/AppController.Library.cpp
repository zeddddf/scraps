#include "core/commands/AppController.Internal.h"

namespace clipd2d::core::commands {

common::Status AppController::CountClips(const store::ClipQuery& query, std::int64_t* count) {
  std::scoped_lock lock(mutex_);
  return repository_.CountClips(query, count);
}

common::Status AppController::ListClips(
    const store::ClipQuery& query,
    int offset,
    int limit,
    std::vector<store::ClipSummary>* clips) {
  std::scoped_lock lock(mutex_);
  return repository_.ListClips(query, offset, limit, clips);
}

common::Status AppController::GetClipDetailsById(std::int64_t id, store::ClipRecord* clip) {
  std::scoped_lock lock(mutex_);
  return repository_.GetClipDetailsById(id, clip);
}

common::Status AppController::GetClipById(std::int64_t id, store::ClipRecord* clip) {
  std::scoped_lock lock(mutex_);
  return repository_.GetClipById(id, clip);
}

common::Status AppController::GetLatestClip(store::ClipRecord* clip) {
  std::scoped_lock lock(mutex_);
  return repository_.GetLatestClip(clip);
}

common::Status AppController::LoadBootstrapState(store::BootstrapState* state) {
  std::scoped_lock lock(mutex_);
  return repository_.LoadBootstrapState(state);
}

common::Status AppController::HasAnyClips(bool* has_any_clips) {
  std::scoped_lock lock(mutex_);
  return repository_.HasAnyClips(has_any_clips);
}

common::Status AppController::HasAnySettingsData(bool* has_any_settings) {
  std::scoped_lock lock(mutex_);
  return repository_.HasAnySettingsData(has_any_settings);
}

common::Status AppController::TogglePinned(std::int64_t id, bool pinned) {
  std::scoped_lock lock(mutex_);
  return repository_.SetPinned(id, pinned);
}

common::Status AppController::ToggleFavorite(std::int64_t id, bool favorite) {
  std::scoped_lock lock(mutex_);
  return repository_.SetFavorite(id, favorite);
}

common::Status AppController::SetStorageTier(std::int64_t id, store::StorageTier tier) {
  std::scoped_lock lock(mutex_);
  return repository_.SetStorageTier(id, tier);
}

common::Status AppController::DeleteClip(std::int64_t id) {
  std::scoped_lock lock(mutex_);
  return repository_.DeleteClip(id);
}

common::Status AppController::EnsureTag(
    const std::wstring& name,
    const std::wstring& color,
    std::int64_t* tag_id) {
  std::scoped_lock lock(mutex_);
  return repository_.EnsureTag(name, color, tag_id);
}

common::Status AppController::AddTag(std::int64_t id, std::int64_t tag_id) {
  std::scoped_lock lock(mutex_);
  return repository_.AddTag(id, tag_id);
}

common::Status AppController::RemoveTag(std::int64_t id, std::int64_t tag_id) {
  std::scoped_lock lock(mutex_);
  return repository_.RemoveTag(id, tag_id);
}

common::Status AppController::RenameTag(std::int64_t tag_id, const std::wstring& name, const std::wstring& color) {
  std::scoped_lock lock(mutex_);
  return repository_.RenameTag(tag_id, name, color);
}

common::Status AppController::DeleteTag(std::int64_t tag_id) {
  std::scoped_lock lock(mutex_);
  return repository_.DeleteTag(tag_id);
}

common::Status AppController::ListTags(std::vector<store::TagRecord>* tags) {
  std::scoped_lock lock(mutex_);
  return repository_.ListTags(tags);
}

common::Status AppController::ListClipTags(std::int64_t id, std::vector<store::TagRecord>* tags) {
  std::scoped_lock lock(mutex_);
  return repository_.ListClipTags(id, tags);
}

common::Status AppController::AddToQueue(std::int64_t id) {
  std::scoped_lock lock(mutex_);
  return repository_.AddToQueue(id, settings_.queue_auto_pop);
}

common::Status AppController::RemoveFromQueue(std::int64_t id) {
  std::scoped_lock lock(mutex_);
  return repository_.RemoveFromQueue(id);
}

common::Status AppController::ListQueue(std::vector<store::QueueEntry>* entries) {
  std::scoped_lock lock(mutex_);
  return repository_.ListQueue(entries);
}

common::Status AppController::ReorderQueue(std::int64_t id, int new_sort_order) {
  std::scoped_lock lock(mutex_);
  return repository_.ReorderQueue(id, new_sort_order);
}

common::Status AppController::ReorderFavorites(std::int64_t id, int new_sort_order) {
  std::scoped_lock lock(mutex_);
  return repository_.ReorderFavorites(id, new_sort_order);
}

common::Status AppController::ReorderPinned(std::int64_t id, int new_sort_order) {
  std::scoped_lock lock(mutex_);
  return repository_.ReorderPinned(id, new_sort_order);
}

common::Status AppController::ReorderTagItems(std::int64_t tag_id, std::int64_t id, int new_sort_order) {
  std::scoped_lock lock(mutex_);
  return repository_.ReorderTagItems(tag_id, id, new_sort_order);
}

common::Status AppController::MoveQueueToFront(std::int64_t id) {
  std::scoped_lock lock(mutex_);
  common::Status status = repository_.AddToQueue(id, settings_.queue_auto_pop);
  if (!status.ok()) {
    return status;
  }
  return repository_.MoveQueueToFront(id, settings_.queue_auto_pop);
}

common::Status AppController::ExportClipToFile(
    std::int64_t id,
    const std::wstring& destination_path,
    std::wstring* exported_path) {
  std::scoped_lock lock(mutex_);
  return repository_.ExportClipToFile(id, destination_path, exported_path);
}

common::Status AppController::GenerateQrCodeForUrl(std::wstring_view url, std::wstring* output_path) {
  std::scoped_lock lock(mutex_);
  return core::qr::GenerateUrlQrPng(paths_, url, output_path);
}

common::Status AppController::EnsureGroup(const std::wstring& name, std::int64_t* group_id) {
  std::scoped_lock lock(mutex_);
  return repository_.EnsureGroup(name, group_id);
}

common::Status AppController::RenameGroup(std::int64_t group_id, const std::wstring& name) {
  std::scoped_lock lock(mutex_);
  return repository_.RenameGroup(group_id, name);
}

common::Status AppController::DeleteGroup(std::int64_t group_id) {
  std::scoped_lock lock(mutex_);
  return repository_.DeleteGroup(group_id);
}

common::Status AppController::ReorderGroup(std::int64_t group_id, int new_sort_order) {
  std::scoped_lock lock(mutex_);
  return repository_.ReorderGroup(group_id, new_sort_order);
}

common::Status AppController::SetGroupTabPinned(std::int64_t group_id, bool pinned, int tab_sort_order) {
  std::scoped_lock lock(mutex_);
  return repository_.SetGroupTabPinned(group_id, pinned, tab_sort_order);
}

common::Status AppController::ReorderGroupTab(std::int64_t group_id, int new_tab_sort_order) {
  std::scoped_lock lock(mutex_);
  return repository_.ReorderGroupTab(group_id, new_tab_sort_order);
}

common::Status AppController::SetGroup(std::int64_t id, std::int64_t group_id) {
  std::scoped_lock lock(mutex_);
  return repository_.SetGroup(id, group_id);
}

common::Status AppController::MoveClipsToGroup(const std::vector<std::int64_t>& ids, std::int64_t group_id) {
  std::scoped_lock lock(mutex_);
  for (const auto id : ids) {
    common::Status status = repository_.SetGroup(id, group_id);
    if (!status.ok()) {
      return status;
    }
  }
  return common::Status::Ok();
}

common::Status AppController::MoveClipsToLibraryRoot(const std::vector<std::int64_t>& ids) {
  std::scoped_lock lock(mutex_);
  for (const auto id : ids) {
    common::Status status = repository_.SetStorageTier(id, store::StorageTier::Library);
    if (!status.ok()) {
      return status;
    }
    status = repository_.SetGroup(id, 0);
    if (!status.ok()) {
      return status;
    }
  }
  return common::Status::Ok();
}

common::Status AppController::MoveClipsToHistory(const std::vector<std::int64_t>& ids) {
  std::scoped_lock lock(mutex_);
  for (const auto id : ids) {
    common::Status status = repository_.SetStorageTier(id, store::StorageTier::History);
    if (!status.ok()) {
      return status;
    }
  }
  return common::Status::Ok();
}

common::Status AppController::ListGroups(std::vector<store::GroupRecord>* groups) {
  std::scoped_lock lock(mutex_);
  return repository_.ListGroups(groups);
}

common::Status AppController::LoadSettings(store::AppSettings* settings) {
  std::scoped_lock lock(mutex_);
  if (settings == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "输出设置不能为空。");
  }
  *settings = settings_;
  return common::Status::Ok();
}

common::Status AppController::SaveSettings(const store::AppSettings& settings) {
  std::scoped_lock lock(mutex_);
  settings_ = settings;
  return settings_repository_.Save(settings_);
}

store::AppSettings AppController::current_settings() const {
  const auto wait_started_at = GetTickCount64();
  std::scoped_lock lock(mutex_);
  const auto wait_elapsed_ms = GetTickCount64() - wait_started_at;
  if (wait_elapsed_ms >= 16) {
    common::WriteLog(
        common::LogLevel::Warning,
        "controller.lock",
        "current_settings waited_ms=" + std::to_string(wait_elapsed_ms));
  }
  return settings_;
}

common::Status AppController::RunStartupMaintenance() {
  common::Status status = common::Status::Ok();
  const auto wait_started_at = GetTickCount64();
  {
    std::scoped_lock lock(mutex_);
    const auto wait_elapsed_ms = GetTickCount64() - wait_started_at;
    if (wait_elapsed_ms >= 16) {
      common::WriteLog(
          common::LogLevel::Warning,
          "controller.lock",
          "startup_maintenance waited_ms=" + std::to_string(wait_elapsed_ms));
    }
    if (shutting_down_) {
      return common::Status::Ok();
    }
    const auto clear_started_at = GetTickCount64();
    status = repository_.ClearHistory(settings_.history_limit, settings_.history_retention_days);
    const auto clear_elapsed_ms = GetTickCount64() - clear_started_at;
    common::WriteLog(
        common::LogLevel::Info,
        "controller.maintenance",
        "clear_history elapsed_ms=" + std::to_string(clear_elapsed_ms) +
            " ok=" + std::string(status.ok() ? "true" : "false"));
  }
  if (status.ok()) {
    NotifyRefreshFromWorker(MakePreserveViewportRefreshRequest());
  }
  return status;
}


common::Status AppController::ClearHistory() {
  std::scoped_lock lock(mutex_);
  return repository_.ClearHistoryArea();
}


std::vector<actions::ActionDescriptor> AppController::ResolveActions(const store::ClipRecord& clip) const {
  const clipboard::ContentProfile profile = clipboard::BuildContentProfile(
      ToContentKind(clip.summary.content_type),
      clip.plain_text,
      !clip.html_text.empty());
  std::vector<actions::ActionDescriptor> actions = action_resolver_.Resolve(profile);

  auto add_action = [&actions](std::string_view id, std::string_view label, const wchar_t* icon_token, std::string_view placement, bool is_primary, int priority) {
    actions.push_back(actions::ActionDescriptor{
        .id = std::string(id),
        .label = std::string(label),
        .icon_token = icon_token,
        .placement = std::string(placement),
        .is_primary = is_primary,
        .priority = priority,
    });
  };

  switch (clip.summary.content_type) {
    case store::ClipContentType::Image:
      add_action("open-default", "\u6253\u5f00\u56fe\u7247", L"\xE8A7", "drawer", false, 88);
      add_action("open-folder", "\u6253\u5f00\u56fe\u7247\u76ee\u5f55", L"\xE838", "drawer", false, 80);
      break;
    case store::ClipContentType::Files:
      if (!clip.plain_text.empty()) {
        add_action("paste-path", "\u7c98\u8d34\u8def\u5f84", L"\xE8C8", "quick", false, 94);
      }
      if (clip.summary.file_count == 1 && !FirstFilePathFromClip(clip).empty()) {
        add_action("open-default", "\u6253\u5f00\u6587\u4ef6", L"\xE8A7", "drawer", false, 88);
        add_action("copy-path", "\u590d\u5236\u8def\u5f84", L"\xE8C8", "quick", false, 90);
      }
      if (!FirstFilePathFromClip(clip).empty()) {
        add_action("open-folder", "\u6253\u5f00\u6240\u5728\u76ee\u5f55", L"\xE838", "drawer", false, 80);
      }
      break;
    case store::ClipContentType::Text:
    case store::ClipContentType::Html:
    case store::ClipContentType::Rtf:
    default:
      break;
  }

  return actions;
}

std::string AppController::BuildSearchJson(const std::vector<store::ClipSummary>& clips) const {
  std::string json = "[";
  for (size_t index = 0; index < clips.size(); ++index) {
    if (index > 0) {
      json.push_back(',');
    }
    json += "{";
    json += "\"id\":" + std::to_string(clips[index].id);
    json += ",\"title\":\"" + common::JsonEscape(clips[index].title) + "\"";
    json += ",\"snippet\":\"" + common::JsonEscape(clips[index].snippet) + "\"";
    json += ",\"contentType\":\"" + std::string(store::ToSqlContentType(clips[index].content_type)) + "\"";
    json += ",\"isPinned\":" + std::string(clips[index].is_pinned ? "true" : "false");
    json += ",\"isFavorite\":" + std::string(clips[index].is_favorite ? "true" : "false");
    json += ",\"groupId\":" + std::to_string(clips[index].group_id);
    json += ",\"sourceApp\":\"" + common::JsonEscape(common::WideToUtf8(clips[index].source_app_name).utf8) + "\"";
    json += ",\"updatedAt\":" + std::to_string(clips[index].updated_at);
    json += ",\"lastPastedAt\":" + std::to_string(clips[index].last_pasted_at);
    json += ",\"storageTier\":\"" + std::string(store::ToSettingsValue(clips[index].storage_tier)) + "\"";
    json += ",\"pasteCount\":" + std::to_string(clips[index].paste_count);
    json += ",\"isFrequent\":" + std::string(clips[index].is_frequent ? "true" : "false");
    json += ",\"tags\":[";
    for (size_t tag_index = 0; tag_index < clips[index].tag_names.size(); ++tag_index) {
      if (tag_index > 0) {
        json.push_back(',');
      }
      json += "{";
      json += "\"id\":" + std::to_string(clips[index].tag_ids[tag_index]);
      json += ",\"name\":\"" + common::JsonEscape(common::WideToUtf8(clips[index].tag_names[tag_index]).utf8) + "\"";
      json += ",\"color\":\"" + common::JsonEscape(common::WideToUtf8(clips[index].tag_colors[tag_index]).utf8) + "\"";
      json += "}";
    }
    json += "]";
    if (clips[index].image_width > 0 && clips[index].image_height > 0) {
      json += ",\"imageWidth\":" + std::to_string(clips[index].image_width);
      json += ",\"imageHeight\":" + std::to_string(clips[index].image_height);
    }
    json += "}";
  }
  json.push_back(']');
  return json;
}

}  // namespace clipd2d::core::commands
