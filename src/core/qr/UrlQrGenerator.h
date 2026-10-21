#pragma once

#include <string>
#include <string_view>

#include "common/Paths.h"
#include "common/Status.h"

namespace clipd2d::core::qr {

common::Status GenerateUrlQrPng(
    const common::AppPaths& paths,
    std::wstring_view url,
    std::wstring* output_path);

}  // namespace clipd2d::core::qr
