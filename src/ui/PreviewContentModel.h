#pragma once

// 预览窗可测核心：文件清单解析/统计、缩放钳制、HTML 包壳文档生成。
// 无窗口依赖（仅 std），供 PreviewWindowTests 直接覆盖。

#include <cstdint>
#include <string>
#include <vector>

namespace clipd2d::ui {

struct PreviewFileEntry {
  std::wstring name;
  std::wstring directory;
  std::int64_t size_bytes = 0;
  bool exists = false;
};

struct PreviewFileList {
  std::vector<PreviewFileEntry> entries;
  // plain_text 中出现过但磁盘上不存在的行数（显示"缺失 N 项"用）。
  int missing_count = 0;
  std::int64_t total_bytes = 0;
};

// 解析 Files 类型剪贴条目的 plain_text（每行一个路径，容忍空行/首尾空白）。
// 对存在文件取文件大小；不存在的行保留条目并计入 missing_count。
PreviewFileList BuildPreviewFileList(const std::string& paths_utf8);

// 人类可读大小：< 1024 → "N B"；此后 KB/MB/GB 保留 1 位小数。
std::wstring FormatPreviewFileSize(std::int64_t bytes);

// 图片缩放因子钳制：范围 [1.0, 5.0]，吸附到 0.1 步进。
float ClampPreviewZoom(float zoom);

// 从原始 CF_HTML（"Version:0.9 / StartHTML:... / EndHTML:..." 头 + 正文）提取
// 展示用正文片段：优先 <!--StartFragment-->~<!--EndFragment--> 标记（ASCII
// 大小写不敏感），无标记时按头内 StartHTML/EndHTML 字节偏移截取，均不可用则
// 原样返回。存储层必须保留原始 CF_HTML（回写剪贴板时接收方依赖头），本函数
// 只在展示层调用。
std::string ExtractHtmlFragment(const std::string& raw_cf_html);

enum class PreviewDocStatus {
  kOk,
  kTooLarge,  // UTF-8 片段超限，调用方应回退纯文本视图
};

// 把剪贴的 HTML 片段包进主题化文档壳：
// 背景随深浅色、图片限宽、表格加边框。片段 UTF-8 > kMaxHtmlFragmentBytes → kTooLarge。
PreviewDocStatus BuildPreviewHtmlDoc(
    const std::string& html_fragment_utf8,
    bool dark_theme,
    std::wstring* out_doc);

}  // namespace clipd2d::ui
