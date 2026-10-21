#pragma once

#include <array>

#include <windows.h>

namespace clipd2d::core::policies {

// Responsibility: Provide the canonical clipboard open retry schedule for capture and writer paths.
// Inputs: None.
// Outputs: Shared retry delay sequence used by clipboard read/write code.
// Forbidden dependencies: Database state, UI concerns, or per-call mutable globals.

const std::array<DWORD, 8>& ClipboardOpenRetryDelaysMs();

}  // namespace clipd2d::core::policies
