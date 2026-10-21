#pragma once

#include <windows.h>
#include <aclapi.h>
#include <accctrl.h>

#include <vector>

// 较老的 SDK 头文件可能缺少该定义（Vista+ 支持）。
#ifndef PIPE_REJECT_REMOTE_CLIENTS
#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008
#endif

namespace clipd2d::ipc {

// DACL 授权范围。
enum class DaclScope {
  kUserOnly,               // 仅当前用户（令牌文件用）
  kUserWithSystemAdmins,   // 当前用户 + 本地 SYSTEM/Administrators（管道用）
};

// RAII 安全描述符：当前用户 SID（GetTokenInformation TokenUser）+
// 可选本地 SYSTEM/Administrators 的 DACL（SetEntriesInAcl 标准做法），
// 供 CreateNamedPipeW / CreateFileW 的 SECURITY_ATTRIBUTES 使用。
//
// 用法（两处管道创建共用）：
//   ScopedSecurityAttributes security(DaclScope::kUserWithSystemAdmins);
//   HANDLE pipe = CreateNamedPipeW(..., PIPE_REJECT_REMOTE_CLIENTS, ...,
//                                   security.get());
// 构造失败时 get() 返回 nullptr，调用方退回默认 DACL 并记录日志。
class ScopedSecurityAttributes {
 public:
  explicit ScopedSecurityAttributes(DaclScope scope) {
    Build(scope);
  }
  ~ScopedSecurityAttributes() {
    Cleanup();
  }

  ScopedSecurityAttributes(const ScopedSecurityAttributes&) = delete;
  ScopedSecurityAttributes& operator=(const ScopedSecurityAttributes&) = delete;

  [[nodiscard]] bool ok() const {
    return ready_;
  }

  SECURITY_ATTRIBUTES* get() {
    return ready_ ? &attributes_ : nullptr;
  }

 private:
  void Build(DaclScope scope) {
    attributes_.nLength = sizeof(SECURITY_ATTRIBUTES);
    attributes_.lpSecurityDescriptor = nullptr;
    attributes_.bInheritHandle = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token_)) {
      token_ = nullptr;
      return;
    }

    DWORD needed = 0;
    GetTokenInformation(token_, TokenUser, nullptr, 0, &needed);
    if (needed == 0) {
      return;
    }
    token_user_buffer_.resize(needed);
    if (!GetTokenInformation(
            token_, TokenUser, token_user_buffer_.data(), needed, &needed)) {
      return;
    }
    auto* token_user = reinterpret_cast<TOKEN_USER*>(token_user_buffer_.data());
    PSID user_sid = token_user->User.Sid;
    if (!IsValidSid(user_sid)) {
      return;
    }

    EXPLICIT_ACCESS_W entries[3]{};
    DWORD entry_count = 0;

    auto add_entry = [&entries, &entry_count](PSID sid, DWORD access, TRUSTEE_TYPE type) {
      EXPLICIT_ACCESS_W& entry = entries[entry_count];
      entry.grfAccessPermissions = access;
      entry.grfAccessMode = SET_ACCESS;
      entry.grfInheritance = NO_INHERITANCE;
      entry.Trustee.pMultipleTrustee = nullptr;
      entry.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
      entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
      entry.Trustee.TrusteeType = type;
      entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
      ++entry_count;
    };

    add_entry(user_sid, GENERIC_ALL, TRUSTEE_IS_USER);

    if (scope == DaclScope::kUserWithSystemAdmins) {
      SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
      if (AllocateAndInitializeSid(
              &nt_authority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0,
              &system_sid_)) {
        add_entry(system_sid_, GENERIC_ALL, TRUSTEE_IS_WELL_KNOWN_GROUP);
      } else {
        system_sid_ = nullptr;
      }
      if (AllocateAndInitializeSid(
              &nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
              DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins_sid_)) {
        add_entry(admins_sid_, GENERIC_ALL, TRUSTEE_IS_WELL_KNOWN_GROUP);
      } else {
        admins_sid_ = nullptr;
      }
    }

    if (entry_count == 0) {
      return;
    }
    if (SetEntriesInAclW(entry_count, entries, nullptr, &acl_) != ERROR_SUCCESS) {
      acl_ = nullptr;
      return;
    }
    if (acl_ == nullptr) {
      return;
    }

    if (!InitializeSecurityDescriptor(&security_descriptor_, SECURITY_DESCRIPTOR_REVISION)) {
      return;
    }
    if (!SetSecurityDescriptorDacl(&security_descriptor_, TRUE, acl_, FALSE)) {
      return;
    }

    attributes_.lpSecurityDescriptor = &security_descriptor_;
    ready_ = true;
  }

  void Cleanup() {
    if (acl_ != nullptr) {
      LocalFree(acl_);
      acl_ = nullptr;
    }
    if (system_sid_ != nullptr) {
      FreeSid(system_sid_);
      system_sid_ = nullptr;
    }
    if (admins_sid_ != nullptr) {
      FreeSid(admins_sid_);
      admins_sid_ = nullptr;
    }
    if (token_ != nullptr) {
      CloseHandle(token_);
      token_ = nullptr;
    }
    token_user_buffer_.clear();
    attributes_.lpSecurityDescriptor = nullptr;
    ready_ = false;
  }

  SECURITY_ATTRIBUTES attributes_{};
  SECURITY_DESCRIPTOR security_descriptor_{};
  PACL acl_ = nullptr;
  PSID system_sid_ = nullptr;
  PSID admins_sid_ = nullptr;
  HANDLE token_ = nullptr;
  std::vector<unsigned char> token_user_buffer_;
  bool ready_ = false;
};

}  // namespace clipd2d::ipc
