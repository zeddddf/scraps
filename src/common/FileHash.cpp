#include "common/FileHash.h"

#include <windows.h>
#include <wincrypt.h>

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
  HCRYPTPROV provider = 0;
  if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
    return {};
  }

  HCRYPTHASH hash = 0;
  if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
    CryptReleaseContext(provider, 0);
    return {};
  }

  const bool hash_ok = bytes.empty() ||
      CryptHashData(
          hash,
          reinterpret_cast<const BYTE*>(bytes.data()),
          static_cast<DWORD>(bytes.size()),
          0);
  if (!hash_ok) {
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    return {};
  }

  std::array<BYTE, 32> digest{};
  DWORD digest_size = static_cast<DWORD>(digest.size());
  if (!CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &digest_size, 0)) {
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    return {};
  }

  CryptDestroyHash(hash);
  CryptReleaseContext(provider, 0);
  return HexBytes(digest.data(), digest_size);
}

}  // namespace clipd2d::common
