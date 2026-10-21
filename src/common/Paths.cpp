#include "common/Paths.h"

#include <windows.h>
#include <shlobj.h>

namespace clipd2d::common {

common::Status EnsureDirectory(const std::wstring& path) {
  if (path.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "目录路径不能为空。");
  }

  if (CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
    return common::Status::Ok();
  }

  return common::Status::Error(common::StatusCode::PlatformError, "创建目录失败。");
}

common::Status ResolveAppPaths(AppPaths* paths) {
  if (paths == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "输出路径对象不能为空。");
  }

  PWSTR local_app_data = nullptr;
  const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_app_data);
  if (FAILED(result) || local_app_data == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "获取 LocalAppData 失败。");
  }

  paths->root_dir = std::wstring(local_app_data) + L"\\Scraps";
  CoTaskMemFree(local_app_data);

  paths->storage_dir = paths->root_dir + L"\\storage";
  paths->image_dir = paths->storage_dir + L"\\images";
  paths->cache_dir = paths->root_dir + L"\\cache";
  paths->database_path = paths->root_dir + L"\\clipboard.db";

  common::Status status = EnsureDirectory(paths->root_dir);
  if (!status.ok()) {
    return status;
  }
  status = EnsureDirectory(paths->storage_dir);
  if (!status.ok()) {
    return status;
  }
  status = EnsureDirectory(paths->image_dir);
  if (!status.ok()) {
    return status;
  }
  status = EnsureDirectory(paths->cache_dir);
  return status;
}

}  // namespace clipd2d::common
