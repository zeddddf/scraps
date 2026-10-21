#include "common/FileHash.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace clipd2d::common {

namespace {

std::string HexBytes(const BYTE* bytes, DWORD size) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (DWORD index = 0; index < size; ++index) {
    output << std::setw(2) << static_cast<unsigned int>(bytes[index]);
  }
  return output.str();
}

}  // namespace

std::string HashBytes(std::string_view bytes) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const unsigned char ch : bytes) {
    hash ^= ch;
    hash *= 1099511628211ull;
  }

  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

std::string Sha256Hex(std::string_view bytes) {
  // BCrypt（CNG）单次摘要：替代已弃用的 CAPI（CryptAcquireContext 系列），
  // 无手工 RAII 清理分支。
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  const NTSTATUS open_status =
      BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
  if (open_status < 0) {
    return {};
  }

  std::array<BYTE, 32> digest{};
  const NTSTATUS hash_status = BCryptHash(
      algorithm,
      nullptr,
      0,
      reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
      static_cast<ULONG>(bytes.size()),
      digest.data(),
      static_cast<ULONG>(digest.size()));
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (hash_status < 0) {
    return {};
  }
  return HexBytes(digest.data(), static_cast<DWORD>(digest.size()));
}

std::string ContentIdentityHex(std::string_view bytes) {
  const std::string full = Sha256Hex(bytes);
  if (full.size() < 32) {
    return {};
  }
  return full.substr(0, 32);
}

}  // namespace clipd2d::common
