#pragma once

// 行文案与文件路径文案。原先埋在 MainWindowInternal.h 的匿名命名空间里，
// 13 个 TU 各持一份副本；这里改为具名实体 + 单一定义，RowRenderer 等组件
// 不再需要拖入整个 MainWindow 类定义。

#include <cstdint>
#include <string>
#include <string_view>

#include "core/store/ClipTypes.h"

namespace clipd2d::ui {

std::wstring ExtractFirstLine(std::string_view text);

std::wstring ToWide(std::string_view text);

std::wstring FormatByteSize(std::uintmax_t bytes);

std::wstring FileDisplayName(const std::wstring& path);

std::wstring FileTrailingText(const core::store::ClipSummary& summary, const std::wstring& first_path);

std::wstring FilePathLine(const core::store::ClipSummary& summary, const std::wstring& first_path);

std::wstring SummaryTitle(const core::store::ClipSummary& summary);

std::wstring RowContentText(const core::store::ClipSummary& summary);

std::wstring FirstFilePathFromSummary(const core::store::ClipSummary& summary);

bool SupportsSystemThumbnailPreviewPath(const std::wstring& path);

bool IsImageFilePath(const std::wstring& path);

bool IsVideoFilePath(const std::wstring& path);

std::wstring LowercaseWide(std::wstring text);

}  // namespace clipd2d::ui
