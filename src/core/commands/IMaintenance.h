#pragma once

// IMaintenance —— 维护与杂项服务面。
// 覆盖「后台维护 + 周边能力」这一用例：启动维护、导出文件、二维码生成、图片 OCR。
// 不在本接口：Initialize/Shutdown/OnClipboardChanged/SetRefreshDispatcher
// （进程装配面，由 app 层直接持有 AppController 使用，UI 不该触碰）。

#include <cstdint>
#include <string>
#include <string_view>

#include "common/Status.h"

namespace clipd2d::core::commands {

class IMaintenance {
 public:
  virtual ~IMaintenance() = default;

  virtual common::Status RunStartupMaintenance() = 0;
  virtual common::Status ExportClipToFile(std::int64_t id, const std::wstring& destination_path, std::wstring* exported_path) = 0;
  virtual common::Status GenerateQrCodeForUrl(std::wstring_view url, std::wstring* output_path) = 0;
  virtual common::Status RequestImageOcr(std::int64_t id, bool force_rerun) = 0;
  virtual common::Status GetImageOcrProviderStatus(std::wstring* message) const = 0;
};

}  // namespace clipd2d::core::commands
