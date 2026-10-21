#include "common/ClipboardSuppression.h"

#include <windows.h>

namespace clipd2d::common {

namespace {

constexpr wchar_t kSharedMappingName[] = L"Local\\Scraps.SuppressUntil";

struct SharedSuppressState {
  volatile LONG64 suppress_until_ms = 0;
};

class SharedStateHolder {
 public:
  SharedStateHolder() {
    mapping_ = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(SharedSuppressState),
        kSharedMappingName);
    if (mapping_ == nullptr) {
      return;
    }

    view_ = static_cast<SharedSuppressState*>(MapViewOfFile(
        mapping_,
        FILE_MAP_READ | FILE_MAP_WRITE,
        0,
        0,
        sizeof(SharedSuppressState)));
  }

  ~SharedStateHolder() {
    if (view_ != nullptr) {
      UnmapViewOfFile(view_);
      view_ = nullptr;
    }
    if (mapping_ != nullptr) {
      CloseHandle(mapping_);
      mapping_ = nullptr;
    }
  }

  SharedSuppressState* view() const {
    return view_;
  }

 private:
  HANDLE mapping_ = nullptr;
  SharedSuppressState* view_ = nullptr;
};

SharedSuppressState* GetSharedState() {
  static SharedStateHolder holder;
  return holder.view();
}

}  // namespace

std::int64_t GetGlobalClipboardSuppressUntil() {
  SharedSuppressState* state = GetSharedState();
  if (state == nullptr) {
    return 0;
  }

  return static_cast<std::int64_t>(
      InterlockedCompareExchange64(&state->suppress_until_ms, 0, 0));
}

void SetGlobalClipboardSuppressUntil(std::int64_t epoch_ms) {
  SharedSuppressState* state = GetSharedState();
  if (state == nullptr) {
    return;
  }

  InterlockedExchange64(&state->suppress_until_ms, static_cast<LONG64>(epoch_ms));
}

}  // namespace clipd2d::common
