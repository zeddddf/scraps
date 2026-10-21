#pragma once

#include <string>
#include <string_view>

namespace clipd2d::common {

// FNV-1a 64 位（16 hex）。非抗碰撞，仅保留给资源缓存文件命名等兼容路径；
// 内容身份键请用 ContentIdentityHex。
std::string HashBytes(std::string_view bytes);

// SHA-256（64 hex）。BCrypt（CNG）实现。
std::string Sha256Hex(std::string_view bytes);

// 内容身份键：SHA-256 截断前 16 字节（32 hex）。抗碰撞，用于
// content_hash 与 identity_key。旧 FNV 身份键由查重管线的 legacy
// 回退路径匹配后自动迁移（FindLegacyTextCandidate 等）。
std::string ContentIdentityHex(std::string_view bytes);

}  // namespace clipd2d::common
