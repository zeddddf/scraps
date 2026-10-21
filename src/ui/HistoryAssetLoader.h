#pragma once

#include "core/store/ClipTypes.h"

namespace clipd2d::ui {

class MainWindow;

// Responsibility: Own MainWindow's async icon/thumbnail/preview loading entrypoints and bitmap cache invalidation.
// Inputs: MainWindow state, asset keys, and bitmap-ready payload messages.
// Outputs: Updated bitmap caches and dirty-row notifications for the view.
// Forbidden dependencies: Query composition, repository SQL, or direct input handling.

class HistoryAssetLoader {
 public:
  explicit HistoryAssetLoader(MainWindow& owner);

  void EnsureBitmapCachesValid();
  void RequestSourceIconAsync(const core::store::ClipSummary& summary);
  void RequestFileItemIconAsync(const core::store::ClipSummary& summary);
  void RequestThumbnailAsync(const core::store::ClipSummary& summary);
  void RequestPreviewAsync(const core::store::ClipRecord& clip);
  void HandleIconReady(LPARAM l_param);
  void HandlePreviewReady(LPARAM l_param);
  void HandleThumbnailReady(LPARAM l_param);

 private:
  MainWindow& owner_;
};

}  // namespace clipd2d::ui
