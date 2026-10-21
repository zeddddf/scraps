#pragma once

#include <string>

#include "common/Status.h"

namespace clipd2d::common {

struct AppPaths {
  std::wstring root_dir;
  std::wstring storage_dir;
  std::wstring image_dir;
  std::wstring cache_dir;
  std::wstring database_path;
};

common::Status ResolveAppPaths(AppPaths* paths);
common::Status EnsureDirectory(const std::wstring& path);

}  // namespace clipd2d::common
