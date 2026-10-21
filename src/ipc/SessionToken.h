#pragma once

#include <windows.h>
#include <wincrypt.h>

#include <random>
#include <string>

#include "common/Paths.h"
#include "common/Status.h"
#include "ipc/PipeSecurity.h"

namespace clipd2d::ipc {

// 会话令牌文件名（位于 %LOCALAPPDATA%\Scraps\ 下，仅当前用户可读写）。
inline constexpr wchar_t kSessionTokenFileName[] = L"ipc_session.token";

// 生成 32 字节随机数的 64 位十六进制会话令牌。
// 首选 CryptGenRandom（advapi32，CSPRNG）；失败时降级 std::random_device。
inline std::string GenerateSessionToken() {
  unsigned char bytes[32]{};
  bool filled = false;

  HCRYPTPROV provider = 0;
  if (CryptAcquireContextW(
          &provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
    filled = CryptGenRandom(provider, static_cast<DWORD>(sizeof(bytes)), bytes) != FALSE;
    CryptReleaseContext(provider, 0);
  }
  if (!filled) {
    std::random_device rd;
    for (unsigned char& byte : bytes) {
      byte = static_cast<unsigned char>(rd());
    }
  }

  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string token;
  token.reserve(sizeof(bytes) * 2);
  for (unsigned char byte : bytes) {
    token.push_back(kHexDigits[byte >> 4]);
    token.push_back(kHexDigits[byte & 0x0F]);
  }
  return token;
}

// 写入令牌文件，DACL 仅授予当前用户。失败时删除残留文件并返回错误。
inline common::Status WriteSessionTokenFile(const std::string& token) {
  common::AppPaths paths;
  const common::Status paths_status = common::ResolveAppPaths(&paths);
  if (!paths_status.ok()) {
    return paths_status;
  }

  ScopedSecurityAttributes security(DaclScope::kUserOnly);
  if (!security.ok()) {
    return common::Status::Error(
        common::StatusCode::PlatformError,
        "构建令牌文件安全描述符失败。");
  }

  const std::wstring path = paths.root_dir + L"\\" + kSessionTokenFileName;
  HANDLE file = CreateFileW(
      path.c_str(),
      GENERIC_WRITE,
      0,
      security.get(),
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return common::Status::Error(
        common::StatusCode::PlatformError,
        "创建令牌文件失败（错误码 " + std::to_string(GetLastError()) + "）。");
  }

  DWORD bytes_written = 0;
  const BOOL write_ok =
      WriteFile(file, token.data(), static_cast<DWORD>(token.size()), &bytes_written, nullptr);
  CloseHandle(file);

  if (!write_ok || bytes_written != token.size()) {
    DeleteFileW(path.c_str());
    return common::Status::Error(
        common::StatusCode::PlatformError,
        "写入令牌文件失败。");
  }
  return common::Status::Ok();
}

// 读取令牌文件。文件不存在或读取失败时返回 false（调用方按无令牌处理）。
inline bool LoadSessionTokenFile(std::string* token) {
  if (token == nullptr) {
    return false;
  }
  token->clear();

  common::AppPaths paths;
  if (!common::ResolveAppPaths(&paths).ok()) {
    return false;
  }

  const std::wstring path = paths.root_dir + L"\\" + kSessionTokenFileName;
  HANDLE file = CreateFileW(
      path.c_str(),
      GENERIC_READ,
      FILE_SHARE_READ,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }

  char buffer[256]{};
  DWORD bytes_read = 0;
  const BOOL read_ok = ReadFile(file, buffer, sizeof(buffer) - 1, &bytes_read, nullptr);
  CloseHandle(file);
  if (!read_ok || bytes_read == 0) {
    return false;
  }

  token->assign(buffer, bytes_read);
  return true;
}

// 删除令牌文件（服务端停止时调用，幂等）。
inline void DeleteSessionTokenFile() {
  common::AppPaths paths;
  if (!common::ResolveAppPaths(&paths).ok()) {
    return;
  }
  const std::wstring path = paths.root_dir + L"\\" + kSessionTokenFileName;
  DeleteFileW(path.c_str());
}

}  // namespace clipd2d::ipc
