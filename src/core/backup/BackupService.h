#pragma once

#include <string>

#include "common/Paths.h"
#include "common/Status.h"

namespace clipd2d::core::backup {

// Container format (.scraps):
//   magic  "SCRAPSBK1"            8 bytes
//   salt   (PBKDF2, random)      16 bytes
//   nonce  (AES-GCM, random)      12 bytes
//   ciphertext  AES-256-GCM over the entry stream
//   tag                          16 bytes
//
// Entry stream (plaintext):
//   [u32 path_len][utf8 path][u64 data_len][data] ... terminated by path_len == 0
//
// Export checkpoints the WAL first so clipboard.db is self-contained.
// Import requires the running Scraps instance to be exited (database lock).

common::Status ExportBackup(
    const common::AppPaths& paths,
    const std::wstring& out_file,
    const std::string& password);

common::Status ImportBackup(
    const common::AppPaths& paths,
    const std::wstring& in_file,
    const std::string& password);

}  // namespace clipd2d::core::backup
