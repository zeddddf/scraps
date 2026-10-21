#include "core/backup/BackupService.h"

#include <windows.h>
#include <bcrypt.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "common/Log.h"
#include "common/Utf8.h"

#pragma comment(lib, "bcrypt.lib")

namespace clipd2d::core::backup {

namespace {

namespace fs = std::filesystem;

constexpr char kMagic[8] = {'S', 'C', 'R', 'A', 'P', 'S', 'B', 'K'};
constexpr size_t kSaltSize = 16;
constexpr size_t kNonceSize = 12;
constexpr size_t kTagSize = 16;
constexpr size_t kKeySize = 32;
constexpr ULONG kPbkdf2Iterations = 150000;

void AppendU32(std::string* out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out->push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
  }
}

void AppendU64(std::string* out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out->push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
  }
}

bool ReadU32(const std::string& in, size_t* offset, std::uint32_t* value) {
  if (in.size() < *offset + 4) {
    return false;
  }
  std::uint32_t result = 0;
  for (int i = 0; i < 4; ++i) {
    result |= static_cast<std::uint8_t>(in[*offset + i]) << (i * 8);
  }
  *offset += 4;
  *value = result;
  return true;
}

bool ReadU64(const std::string& in, size_t* offset, std::uint64_t* value) {
  if (in.size() < *offset + 8) {
    return false;
  }
  std::uint64_t result = 0;
  for (int i = 0; i < 8; ++i) {
    result |= static_cast<std::uint8_t>(in[*offset + i]) << (i * 8);
  }
  *offset += 8;
  *value = result;
  return true;
}

bool ReadFileBytes(const fs::path& path, std::string* out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  if (size < 0) {
    return false;
  }
  file.seekg(0, std::ios::beg);
  out->resize(static_cast<size_t>(size));
  if (size > 0) {
    file.read(out->data(), size);
    if (!file) {
      return false;
    }
  }
  return true;
}

bool WriteFileBytes(const fs::path& path, const char* data, size_t size) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return false;
  }
  if (size > 0) {
    file.write(data, static_cast<std::streamsize>(size));
  }
  return static_cast<bool>(file);
}

// Read-only integrity preflight. Opens with SQLITE_OPEN_READONLY so this can
// never write to the database: no WAL checkpoint, no journal recovery, no
// lock contention with a running app. (An external TRUNCATE checkpoint from
// the CLI once raced the running app and corrupted its WAL view -- the
// 2026-10-21 self-healing incident. Never checkpoint from outside the app.)
// A failed check logs a warning and lets the export continue: an imperfect
// backup still beats no backup.
void QuickCheckDatabase(const fs::path& db_path) {
  const common::Utf8Conversion db_utf8 = common::WideToUtf8(db_path.wstring());
  if (!db_utf8.status.ok()) {
    common::WriteLog(common::LogLevel::Warning, "backup.export",
                     "quick_check skipped, path conversion failed");
    return;
  }
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(db_utf8.utf8.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    const char* message = (db != nullptr) ? sqlite3_errmsg(db) : "cannot open database";
    common::WriteLog(common::LogLevel::Warning, "backup.export",
                     std::string("quick_check skipped, open failed: ") + message);
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return;
  }
  sqlite3_busy_timeout(db, 2000);
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "PRAGMA quick_check;", -1, &stmt, nullptr) != SQLITE_OK) {
    common::WriteLog(common::LogLevel::Warning, "backup.export",
                     std::string("quick_check prepare failed: ") + sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* result = sqlite3_column_text(stmt, 0);
    const std::string verdict = result ? reinterpret_cast<const char*>(result) : "";
    if (verdict != "ok") {
      common::WriteLog(common::LogLevel::Warning, "backup.export",
                       "quick_check reported: " + verdict);
    }
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

class ScopedBcryptAlgorithm {
 public:
  explicit ScopedBcryptAlgorithm(LPCWSTR algorithm, ULONG flags = 0) {
    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg_, algorithm, nullptr, flags);
    ok_ = BCRYPT_SUCCESS(status);
  }
  ~ScopedBcryptAlgorithm() {
    if (alg_ != nullptr) {
      BCryptCloseAlgorithmProvider(alg_, 0);
    }
  }
  BCRYPT_ALG_HANDLE handle() const { return alg_; }
  bool ok() const { return ok_; }

 private:
  BCRYPT_ALG_HANDLE alg_ = nullptr;
  bool ok_ = false;
};

// AES-256-GCM helpers. The AES provider is opened per call for simplicity.
bool EncryptPayload(const std::string& password,
                    const unsigned char* salt,
                    const unsigned char* nonce,
                    const std::string& plaintext,
                    std::string* ciphertext,
                    unsigned char* tag,
                    std::string* error_detail) {
  const auto fail = [error_detail](const char* what, NTSTATUS status) {
    char detail[96];
    snprintf(detail, sizeof(detail), "%s failed ntstatus=0x%08lx", what,
             static_cast<unsigned long>(status));
    if (error_detail != nullptr) {
      *error_detail = detail;
    }
    common::WriteLog(common::LogLevel::Error, "backup.encrypt", detail);
    return false;
  };
  // PBKDF2 is HMAC-based: the SHA256 handle must be opened with
  // BCRYPT_ALG_HANDLE_HMAC_FLAG or BCryptDeriveKeyPBKDF2 rejects it
  // with STATUS_INVALID_HANDLE (0xc0000008).
  ScopedBcryptAlgorithm sha256(L"SHA256", BCRYPT_ALG_HANDLE_HMAC_FLAG);
  ScopedBcryptAlgorithm aes(L"AES");
  if (!sha256.ok() || !aes.ok()) {
    return fail("BCryptOpenAlgorithmProvider", 0xC0000001L);
  }
  NTSTATUS status = BCryptSetProperty(
      aes.handle(), BCRYPT_CHAINING_MODE,
      reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
      sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
  if (!BCRYPT_SUCCESS(status)) {
    return fail("BCryptSetProperty(GCM)", status);
  }
  unsigned char derived[kKeySize]{};
  status = BCryptDeriveKeyPBKDF2(
      sha256.handle(),
      reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
      static_cast<ULONG>(password.size()),
      const_cast<PUCHAR>(salt), kSaltSize, kPbkdf2Iterations,
      derived, sizeof(derived), 0);
  if (!BCRYPT_SUCCESS(status)) {
    return fail("BCryptDeriveKeyPBKDF2", status);
  }
  BCRYPT_KEY_HANDLE key = nullptr;
  status = BCryptGenerateSymmetricKey(aes.handle(), &key, nullptr, 0, derived, sizeof(derived), 0);
  if (!BCRYPT_SUCCESS(status)) {
    return fail("BCryptGenerateSymmetricKey", status);
  }

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
  BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = const_cast<PUCHAR>(nonce);
  info.cbNonce = kNonceSize;
  info.pbTag = tag;
  info.cbTag = kTagSize;

  ciphertext->resize(plaintext.size());
  ULONG result_size = 0;
  status = BCryptEncrypt(
      key,
      reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.data())),
      static_cast<ULONG>(plaintext.size()),
      &info,
      nullptr, 0,
      reinterpret_cast<PUCHAR>(ciphertext->data()),
      static_cast<ULONG>(ciphertext->size()),
      &result_size,
      0);
  BCryptDestroyKey(key);
  if (!BCRYPT_SUCCESS(status)) {
    return fail("BCryptEncrypt", status);
  }
  ciphertext->resize(result_size);
  return true;
}

bool DecryptPayload(const std::string& password,
                    const unsigned char* salt,
                    const unsigned char* nonce,
                    const char* ciphertext,
                    size_t ciphertext_size,
                    const unsigned char* tag,
                    std::string* plaintext) {
  // PBKDF2 is HMAC-based: see EncryptPayload for the flag rationale.
  ScopedBcryptAlgorithm sha256(L"SHA256", BCRYPT_ALG_HANDLE_HMAC_FLAG);
  ScopedBcryptAlgorithm aes(L"AES");
  if (!sha256.ok() || !aes.ok()) {
    return false;
  }
  NTSTATUS status = BCryptSetProperty(
      aes.handle(), BCRYPT_CHAINING_MODE,
      reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
      sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
  if (!BCRYPT_SUCCESS(status)) {
    return false;
  }
  unsigned char derived[kKeySize]{};
  status = BCryptDeriveKeyPBKDF2(
      sha256.handle(),
      reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
      static_cast<ULONG>(password.size()),
      const_cast<PUCHAR>(salt), kSaltSize, kPbkdf2Iterations,
      derived, sizeof(derived), 0);
  if (!BCRYPT_SUCCESS(status)) {
    return false;
  }
  BCRYPT_KEY_HANDLE key = nullptr;
  status = BCryptGenerateSymmetricKey(aes.handle(), &key, nullptr, 0, derived, sizeof(derived), 0);
  if (!BCRYPT_SUCCESS(status)) {
    return false;
  }

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
  BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = const_cast<PUCHAR>(nonce);
  info.cbNonce = kNonceSize;
  info.pbTag = const_cast<PUCHAR>(tag);
  info.cbTag = kTagSize;

  plaintext->resize(ciphertext_size);
  ULONG result_size = 0;
  status = BCryptDecrypt(
      key,
      reinterpret_cast<PUCHAR>(const_cast<char*>(ciphertext)),
      static_cast<ULONG>(ciphertext_size),
      &info,
      nullptr, 0,
      reinterpret_cast<PUCHAR>(plaintext->data()),
      static_cast<ULONG>(plaintext->size()),
      &result_size,
      0);
  BCryptDestroyKey(key);
  plaintext->resize(BCRYPT_SUCCESS(status) ? result_size : 0);
  return BCRYPT_SUCCESS(status);
}

void CollectEntries(const common::AppPaths& paths, std::string* stream) {
  auto add_file = [&stream](const fs::path& full, const std::string& rel) {
    std::string data;
    if (!ReadFileBytes(full, &data)) {
      return;
    }
    AppendU32(stream, static_cast<std::uint32_t>(rel.size()));
    stream->append(rel);
    AppendU64(stream, static_cast<std::uint64_t>(data.size()));
    stream->append(data);
  };

  const fs::path root(paths.root_dir);
  add_file(root / L"clipboard.db", "clipboard.db");
  add_file(root / L"clipboard.db-wal", "clipboard.db-wal");

  std::error_code ec;
  const fs::path storage = fs::absolute(paths.storage_dir, ec);
  if (fs::exists(storage, ec)) {
    for (auto& entry : fs::recursive_directory_iterator(storage, fs::directory_options::skip_permission_denied, ec)) {
      if (ec || !entry.is_regular_file(ec)) {
        continue;
      }
      std::wstring rel_wide = fs::relative(entry.path(), storage, ec).wstring();
      // Build "storage/<rel>" with narrow path (filenames are content hashes: ASCII-safe).
      std::string rel_path = "storage/";
      for (wchar_t c : rel_wide) {
        rel_path.push_back(static_cast<char>(c));
      }
      add_file(entry.path(), rel_path);
    }
  }
  AppendU32(stream, 0);  // terminator
}

}  // namespace

common::Status ExportBackup(
    const common::AppPaths& paths,
    const std::wstring& out_file,
    const std::string& password) {
  if (password.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Backup password cannot be empty.");
  }
  if (out_file.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Backup output path cannot be empty.");
  }

  QuickCheckDatabase(paths.database_path);

  std::string plaintext;
  CollectEntries(paths, &plaintext);

  unsigned char salt[kSaltSize]{};
  unsigned char nonce[kNonceSize]{};
  std::mt19937_64 rng(static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  for (auto& b : salt) b = static_cast<unsigned char>(rng());
  for (auto& b : nonce) b = static_cast<unsigned char>(rng());

  std::string ciphertext;
  unsigned char tag[kTagSize]{};
  std::string encrypt_detail;
  if (!EncryptPayload(password, salt, nonce, plaintext, &ciphertext, tag, &encrypt_detail)) {
    return common::Status::Error(common::StatusCode::PlatformError,
                                 "Backup encryption failed. " + encrypt_detail);
  }

  std::string blob;
  blob.append(kMagic, sizeof(kMagic));
  blob.append(reinterpret_cast<const char*>(salt), kSaltSize);
  blob.append(reinterpret_cast<const char*>(nonce), kNonceSize);
  blob.append(ciphertext);
  blob.append(reinterpret_cast<const char*>(tag), kTagSize);

  std::error_code ec;
  fs::create_directories(fs::path(out_file).parent_path(), ec);
  std::ofstream out(out_file, std::ios::binary | std::ios::trunc);
  if (!out) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Cannot open backup output file.");
  }
  out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
  if (!out) {
    return common::Status::Error(common::StatusCode::PlatformError, "Write backup failed.");
  }
  common::WriteLog(common::LogLevel::Info, "backup.export", "Backup written: " + std::to_string(blob.size()) + " bytes");
  return common::Status::Ok();
}

common::Status ImportBackup(
    const common::AppPaths& paths,
    const std::wstring& in_file,
    const std::string& password) {
  if (password.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Backup password cannot be empty.");
  }
  std::string blob;
  if (!ReadFileBytes(in_file, &blob) ||
      blob.size() < sizeof(kMagic) + kSaltSize + kNonceSize + kTagSize) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid backup file.");
  }
  if (memcmp(blob.data(), kMagic, sizeof(kMagic)) != 0) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Not a Scraps backup file.");
  }

  const unsigned char* salt = reinterpret_cast<const unsigned char*>(blob.data()) + sizeof(kMagic);
  const unsigned char* nonce = salt + kSaltSize;
  const char* ciphertext = blob.data() + sizeof(kMagic) + kSaltSize + kNonceSize;
  const size_t ciphertext_size = blob.size() - sizeof(kMagic) - kSaltSize - kNonceSize - kTagSize;
  const unsigned char* tag = reinterpret_cast<const unsigned char*>(ciphertext) + ciphertext_size;

  std::string plaintext;
  if (!DecryptPayload(password, salt, nonce, ciphertext, ciphertext_size, tag, &plaintext)) {
    return common::Status::Error(common::StatusCode::InvalidArgument,
                                 "Decrypt failed: wrong password or corrupted file.");
  }

  // Require the database entry before touching anything on disk.
  std::string db_entry;
  size_t offset = 0;
  bool has_db = false;
  while (true) {
    std::uint32_t path_len = 0;
    if (!ReadU32(plaintext, &offset, &path_len)) {
      return common::Status::Error(common::StatusCode::InvalidArgument, "Corrupt backup stream.");
    }
    if (path_len == 0) {
      break;
    }
    if (plaintext.size() < offset + path_len) {
      return common::Status::Error(common::StatusCode::InvalidArgument, "Corrupt backup stream.");
    }
    std::string rel = plaintext.substr(offset, path_len);
    offset += path_len;
    std::uint64_t data_len = 0;
    if (!ReadU64(plaintext, &offset, &data_len) || plaintext.size() < offset + data_len) {
      return common::Status::Error(common::StatusCode::InvalidArgument, "Corrupt backup stream.");
    }
    if (rel == "clipboard.db") {
      db_entry = plaintext.substr(offset, data_len);
      has_db = true;
    }
    offset += data_len;
  }
  if (!has_db) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Backup does not contain clipboard.db.");
  }

  // Snapshot current state so import is revertible.
  const fs::path root(paths.root_dir);
  std::error_code ec;
  const std::wstring stamp = std::to_wstring(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
  fs::copy_file(paths.database_path, root / (L"clipboard.db.pre-import-" + stamp), ec);

  // Remove stale WAL/SHM before overwriting clipboard.db: a leftover WAL from
  // the previous database must never be applied to the restored file (this
  // hazard is the same family as the 2026-10-21 self-healing incident). If
  // removal fails the app is likely running and holding the files -- refuse
  // instead of producing a corrupted hybrid state.
  for (const wchar_t* side : {L"clipboard.db-wal", L"clipboard.db-shm"}) {
    std::error_code rm_ec;
    fs::remove(root / side, rm_ec);
    if (rm_ec) {
      return common::Status::Error(
          common::StatusCode::PlatformError,
          "Cannot remove WAL/SHM sidecar file -- is Scraps running? Close it before importing.");
    }
  }

  if (fs::exists(paths.storage_dir, ec)) {
    fs::rename(paths.storage_dir, root / (L"storage.pre-import-" + stamp), ec);
  }

  // Second pass: write everything to disk.
  offset = 0;
  int written = 0;
  while (true) {
    std::uint32_t path_len = 0;
    if (!ReadU32(plaintext, &offset, &path_len) || path_len == 0) {
      break;
    }
    if (plaintext.size() < offset + path_len) {
      return common::Status::Error(common::StatusCode::PlatformError, "Corrupt backup stream during write.");
    }
    std::string rel = plaintext.substr(offset, path_len);
    offset += path_len;
    std::uint64_t data_len = 0;
    if (!ReadU64(plaintext, &offset, &data_len) || plaintext.size() < offset + data_len) {
      return common::Status::Error(common::StatusCode::PlatformError, "Corrupt backup stream during write.");
    }
    const std::wstring rel_wide(rel.begin(), rel.end());
    if (!WriteFileBytes(root / rel_wide, plaintext.data() + offset, data_len)) {
      return common::Status::Error(common::StatusCode::PlatformError, "Write entry failed: " + rel);
    }
    offset += data_len;
    ++written;
  }

  common::WriteLog(common::LogLevel::Info, "backup.import",
                   "Imported entries: " + std::to_string(written));
  return common::Status::Ok();
}

}  // namespace clipd2d::core::backup
