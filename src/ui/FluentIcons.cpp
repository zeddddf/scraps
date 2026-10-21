#include "ui/FluentIcons.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <wrl/client.h>

namespace clipd2d::ui {

namespace {

using Microsoft::WRL::ComPtr;

struct IconAssetEntry {
  FluentIcon icon;
  const wchar_t* file_name;
};

struct IconCacheEntry {
  bool attempted = false;
  ID2D1Factory* factory_owner = nullptr;
  float view_box_width = 20.0f;
  float view_box_height = 20.0f;
  D2D1_RECT_F geometry_bounds = D2D1::RectF(0.0f, 0.0f, 20.0f, 20.0f);
  ComPtr<ID2D1PathGeometry> geometry;
};

constexpr std::array<IconAssetEntry, 21> kIconAssets = {{
    {FluentIcon::Search, L"search.svg"},
    {FluentIcon::PanelPin, L"pin.svg"},
    {FluentIcon::PanelPinned, L"pin_filled.svg"},
    {FluentIcon::Settings, L"settings.svg"},
    {FluentIcon::More, L"more_horizontal.svg"},
    {FluentIcon::List, L"apps_list.svg"},
    {FluentIcon::StarOutline, L"star.svg"},
    {FluentIcon::StarFilled, L"star_filled.svg"},
    {FluentIcon::Text, L"document_text.svg"},
    {FluentIcon::Code, L"code.svg"},
    {FluentIcon::Image, L"image.svg"},
    {FluentIcon::Folder, L"folder.svg"},
    {FluentIcon::Group, L"group.svg"},
    {FluentIcon::Rename, L"rename.svg"},
    {FluentIcon::Delete, L"delete.svg"},
    {FluentIcon::Clipboard, L"clipboard_text.svg"},
    {FluentIcon::Palette, L"color.svg"},
    {FluentIcon::Keyboard, L"keyboard.svg"},
    {FluentIcon::Filter, L"filter.svg"},
    {FluentIcon::Options, L"options.svg"},
    {FluentIcon::Info, L"info.svg"},
}};

std::array<IconCacheEntry, kIconAssets.size()> g_icon_cache;

std::filesystem::path IconAssetRoot() {
  wchar_t module_path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  return std::filesystem::path(module_path).remove_filename() / L"assets" / L"fluent";
}

size_t IconIndex(FluentIcon icon) {
  return static_cast<size_t>(icon);
}

bool ReadTextFile(const std::filesystem::path& path, std::string* contents) {
  if (contents == nullptr) {
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  contents->assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  return true;
}

bool ExtractAttribute(std::string_view svg, std::string_view name, std::string* value) {
  if (value == nullptr) {
    return false;
  }
  const std::string key = std::string(name) + "=\"";
  const size_t key_pos = svg.find(key);
  if (key_pos == std::string_view::npos) {
    return false;
  }
  const size_t value_start = key_pos + key.size();
  const size_t value_end = svg.find('"', value_start);
  if (value_end == std::string_view::npos) {
    return false;
  }
  value->assign(svg.substr(value_start, value_end - value_start));
  return true;
}

bool ParseViewBox(std::string_view view_box, float* width, float* height) {
  if (width == nullptr || height == nullptr) {
    return false;
  }

  std::istringstream stream{std::string(view_box)};
  float min_x = 0.0f;
  float min_y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  if (!(stream >> min_x >> min_y >> w >> h)) {
    return false;
  }
  if (w <= 0.0f || h <= 0.0f) {
    return false;
  }
  *width = w;
  *height = h;
  return true;
}

class SvgPathParser {
 public:
  SvgPathParser(std::string_view path_data, ID2D1GeometrySink* sink)
      : path_data_(path_data), sink_(sink) {}

  bool Parse() {
    if (sink_ == nullptr) {
      return false;
    }

    sink_->SetFillMode(D2D1_FILL_MODE_WINDING);
    char command = '\0';

    while (true) {
      SkipSeparators();
      if (AtEnd()) {
        break;
      }

      if (std::isalpha(static_cast<unsigned char>(CurrentChar())) != 0) {
        command = CurrentChar();
        ++position_;
      } else if (command == '\0') {
        return false;
      }

      switch (command) {
        case 'M':
          if (!ParseMove(false)) {
            return false;
          }
          command = 'L';
          break;
        case 'm':
          if (!ParseMove(true)) {
            return false;
          }
          command = 'l';
          break;
        case 'L':
          if (!ParseLines(false)) {
            return false;
          }
          break;
        case 'l':
          if (!ParseLines(true)) {
            return false;
          }
          break;
        case 'H':
          if (!ParseHorizontal(false)) {
            return false;
          }
          break;
        case 'h':
          if (!ParseHorizontal(true)) {
            return false;
          }
          break;
        case 'V':
          if (!ParseVertical(false)) {
            return false;
          }
          break;
        case 'v':
          if (!ParseVertical(true)) {
            return false;
          }
          break;
        case 'C':
          if (!ParseCubic(false)) {
            return false;
          }
          break;
        case 'c':
          if (!ParseCubic(true)) {
            return false;
          }
          break;
        case 'Z':
        case 'z':
          if (figure_open_) {
            sink_->EndFigure(D2D1_FIGURE_END_CLOSED);
            figure_open_ = false;
            current_point_ = figure_start_;
          }
          break;
        default:
          return false;
      }
    }

    if (figure_open_) {
      sink_->EndFigure(D2D1_FIGURE_END_OPEN);
      figure_open_ = false;
    }
    return SUCCEEDED(sink_->Close());
  }

 private:
  bool ParseMove(bool relative) {
    float x = 0.0f;
    float y = 0.0f;
    if (!ParseCoordinatePair(&x, &y)) {
      return false;
    }

    if (figure_open_) {
      sink_->EndFigure(D2D1_FIGURE_END_OPEN);
    }
    if (relative) {
      x += current_point_.x;
      y += current_point_.y;
    }
    current_point_ = D2D1::Point2F(x, y);
    figure_start_ = current_point_;
    sink_->BeginFigure(current_point_, D2D1_FIGURE_BEGIN_FILLED);
    figure_open_ = true;

    while (HasNumber()) {
      if (!ParseCoordinatePair(&x, &y)) {
        return false;
      }
      if (relative) {
        x += current_point_.x;
        y += current_point_.y;
      }
      current_point_ = D2D1::Point2F(x, y);
      sink_->AddLine(current_point_);
    }
    return true;
  }

  bool ParseLines(bool relative) {
    if (!figure_open_) {
      return false;
    }
    while (HasNumber()) {
      float x = 0.0f;
      float y = 0.0f;
      if (!ParseCoordinatePair(&x, &y)) {
        return false;
      }
      if (relative) {
        x += current_point_.x;
        y += current_point_.y;
      }
      current_point_ = D2D1::Point2F(x, y);
      sink_->AddLine(current_point_);
    }
    return true;
  }

  bool ParseHorizontal(bool relative) {
    if (!figure_open_) {
      return false;
    }
    while (HasNumber()) {
      float x = 0.0f;
      if (!ParseNumber(&x)) {
        return false;
      }
      if (relative) {
        x += current_point_.x;
      }
      current_point_.x = x;
      sink_->AddLine(current_point_);
    }
    return true;
  }

  bool ParseVertical(bool relative) {
    if (!figure_open_) {
      return false;
    }
    while (HasNumber()) {
      float y = 0.0f;
      if (!ParseNumber(&y)) {
        return false;
      }
      if (relative) {
        y += current_point_.y;
      }
      current_point_.y = y;
      sink_->AddLine(current_point_);
    }
    return true;
  }

  bool ParseCubic(bool relative) {
    if (!figure_open_) {
      return false;
    }
    while (HasNumber()) {
      float values[6]{};
      for (float& value : values) {
        if (!ParseNumber(&value)) {
          return false;
        }
      }
      D2D1_POINT_2F p1 = D2D1::Point2F(values[0], values[1]);
      D2D1_POINT_2F p2 = D2D1::Point2F(values[2], values[3]);
      D2D1_POINT_2F p3 = D2D1::Point2F(values[4], values[5]);
      if (relative) {
        p1.x += current_point_.x;
        p1.y += current_point_.y;
        p2.x += current_point_.x;
        p2.y += current_point_.y;
        p3.x += current_point_.x;
        p3.y += current_point_.y;
      }
      sink_->AddBezier(D2D1::BezierSegment(p1, p2, p3));
      current_point_ = p3;
    }
    return true;
  }

  bool ParseCoordinatePair(float* x, float* y) {
    return ParseNumber(x) && ParseNumber(y);
  }

  bool ParseNumber(float* value) {
    if (value == nullptr) {
      return false;
    }
    SkipSeparators();
    if (AtEnd()) {
      return false;
    }
    const char* start = path_data_.data() + position_;
    char* end = nullptr;
    *value = std::strtof(start, &end);
    if (end == start) {
      return false;
    }
    position_ = static_cast<size_t>(end - path_data_.data());
    SkipSeparators();
    return true;
  }

  bool HasNumber() {
    SkipSeparators();
    if (AtEnd()) {
      return false;
    }
    const char ch = CurrentChar();
    return std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '-' || ch == '+' || ch == '.';
  }

  void SkipSeparators() {
    while (!AtEnd()) {
      const char ch = CurrentChar();
      if (ch == ',' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
        ++position_;
      } else {
        break;
      }
    }
  }

  bool AtEnd() const {
    return position_ >= path_data_.size();
  }

  char CurrentChar() const {
    return path_data_[position_];
  }

  std::string_view path_data_;
  ID2D1GeometrySink* sink_ = nullptr;
  size_t position_ = 0;
  bool figure_open_ = false;
  D2D1_POINT_2F current_point_ = D2D1::Point2F(0.0f, 0.0f);
  D2D1_POINT_2F figure_start_ = D2D1::Point2F(0.0f, 0.0f);
};

bool EnsureGeometryLoaded(ID2D1Factory* factory, FluentIcon icon, IconCacheEntry* entry) {
  if (factory == nullptr || entry == nullptr) {
    return false;
  }
  if (entry->geometry != nullptr && entry->factory_owner == factory) {
    return true;
  }
  if (entry->factory_owner != nullptr && entry->factory_owner != factory) {
    entry->attempted = false;
    entry->factory_owner = nullptr;
    entry->geometry.Reset();
  }
  if (entry->attempted) {
    return entry->geometry != nullptr;
  }
  entry->attempted = true;

  const auto& asset = kIconAssets[IconIndex(icon)];
  std::string svg;
  if (!ReadTextFile(IconAssetRoot() / asset.file_name, &svg)) {
    return false;
  }

  std::string view_box_text;
  std::string path_text;
  if (!ExtractAttribute(svg, "viewBox", &view_box_text) || !ExtractAttribute(svg, "d", &path_text)) {
    return false;
  }
  if (!ParseViewBox(view_box_text, &entry->view_box_width, &entry->view_box_height)) {
    return false;
  }

  if (FAILED(factory->CreatePathGeometry(entry->geometry.ReleaseAndGetAddressOf()))) {
    return false;
  }
  entry->factory_owner = factory;

  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(entry->geometry->Open(sink.GetAddressOf()))) {
    entry->factory_owner = nullptr;
    entry->geometry.Reset();
    return false;
  }

  SvgPathParser parser(path_text, sink.Get());
  if (!parser.Parse()) {
    entry->factory_owner = nullptr;
    entry->geometry.Reset();
    return false;
  }
  if (FAILED(entry->geometry->GetBounds(nullptr, &entry->geometry_bounds))) {
    entry->geometry_bounds = D2D1::RectF(0.0f, 0.0f, entry->view_box_width, entry->view_box_height);
  }
  return true;
}

}  // namespace

bool DrawFluentIcon(
    ID2D1Factory* factory,
    ID2D1RenderTarget* target,
    FluentIcon icon,
    const D2D1_RECT_F& rect,
    ID2D1Brush* brush,
    float weight_boost_dip) {
  if (factory == nullptr || target == nullptr || brush == nullptr) {
    return false;
  }

  IconCacheEntry& entry = g_icon_cache[IconIndex(icon)];
  if (!EnsureGeometryLoaded(factory, icon, &entry) || entry.geometry == nullptr) {
    return false;
  }

  const float rect_width = rect.right - rect.left;
  const float rect_height = rect.bottom - rect.top;
  if (rect_width <= 0.0f || rect_height <= 0.0f) {
    return false;
  }

  const float bounds_width = std::max(0.001f, entry.geometry_bounds.right - entry.geometry_bounds.left);
  const float bounds_height = std::max(0.001f, entry.geometry_bounds.bottom - entry.geometry_bounds.top);
  const float scale = std::min(rect_width / bounds_width, rect_height / bounds_height);
  const float tx = rect.left + (rect_width - bounds_width * scale) * 0.5f - entry.geometry_bounds.left * scale;
  const float ty = rect.top + (rect_height - bounds_height * scale) * 0.5f - entry.geometry_bounds.top * scale;

  D2D1_MATRIX_3X2_F old_transform{};
  target->GetTransform(&old_transform);
  const D2D1_MATRIX_3X2_F local_transform =
      D2D1::Matrix3x2F(scale, 0.0f, 0.0f, scale, tx, ty);
  target->SetTransform(local_transform * old_transform);
  target->FillGeometry(entry.geometry.Get(), brush);
  if (weight_boost_dip > 0.01f) {
    target->DrawGeometry(entry.geometry.Get(), brush, weight_boost_dip / std::max(scale, 0.001f));
  }
  target->SetTransform(old_transform);
  return true;
}

}  // namespace clipd2d::ui
