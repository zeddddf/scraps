#include "app/UpdateChecker.h"

#include <winhttp.h>

#include <chrono>
#include <cstdint>
#include <fstream>

#include <nlohmann/json.hpp>

#include "common/Log.h"

#pragma comment(lib, "winhttp.lib")

namespace clipd2d::app {

namespace {

constexpr wchar_t kThrottleFileName[] = L"\\update_check.txt";
constexpr std::int64_t kThrottleSeconds = 20 * 60 * 60;  // ~daily
constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kApiPath[] = L"/repos/zeddddf/scraps/releases/latest";

std::int64_t NowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool ShouldSkipCheck(const std::wstring& cache_dir) {
  std::ifstream file(cache_dir + kThrottleFileName);
  if (!file) {
    return false;
  }
  std::int64_t last = 0;
  if (!(file >> last)) {
    return false;
  }
  return NowSeconds() - last < kThrottleSeconds;
}

void MarkChecked(const std::wstring& cache_dir) {
  std::ofstream file(cache_dir + kThrottleFileName, std::ios::trunc);
  if (file) {
    file << NowSeconds();
  }
}

// Compares "vMAJOR.MINOR.PATCH" style tags numerically. Returns >0 if lhs is newer.
int CompareVersions(std::wstring lhs, std::wstring rhs) {
  auto parse = [](std::wstring& s) {
    if (!s.empty() && (s[0] == L'v' || s[0] == L'V')) {
      s.erase(0, 1);
    }
    std::vector<int> parts(3, 0);
    int index = 0;
    size_t pos = 0;
    while (index < 3 && pos <= s.size()) {
      size_t next = s.find(L'.', pos);
      std::wstring token = s.substr(pos, next == std::wstring::npos ? std::wstring::npos : next - pos);
      parts[index++] = _wtoi(token.c_str());
      if (next == std::wstring::npos) {
        break;
      }
      pos = next + 1;
    }
    return parts;
  };
  const std::vector<int> a = parse(lhs);
  const std::vector<int> b = parse(rhs);
  for (int i = 0; i < 3; ++i) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  return 0;
}

bool HttpGetLatestRelease(std::wstring* body) {
  bool ok = false;
  HINTERNET session = WinHttpOpen(
      L"Scraps-UpdateCheck/1.0",
      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS,
      0);
  if (session == nullptr) {
    return false;
  }
  WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);

  HINTERNET connect = WinHttpConnect(session, kApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET request = connect != nullptr ? WinHttpOpenRequest(
                                               connect,
                                               L"GET",
                                               kApiPath,
                                               nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE)
                                         : nullptr;
  if (request != nullptr) {
    const wchar_t* headers =
        L"Accept: application/vnd.github+json\r\nUser-Agent: Scraps-UpdateCheck\r\n";
    if (WinHttpSendRequest(request, headers, static_cast<DWORD>(wcslen(headers)),
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
      DWORD status = 0;
      DWORD size = sizeof(status);
      if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                              WINHTTP_NO_HEADER_INDEX) &&
          status == 200) {
        ok = true;
        for (;;) {
          DWORD available = 0;
          if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
            break;
          }
          std::string chunk(available, '\0');
          DWORD read = 0;
          if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) {
            break;
          }
          body->append(chunk.begin(), chunk.begin() + read);
        }
      }
    }
    WinHttpCloseHandle(request);
  }
  if (connect != nullptr) {
    WinHttpCloseHandle(connect);
  }
  WinHttpCloseHandle(session);
  return ok;
}

}  // namespace

UpdateChecker::~UpdateChecker() {
  Shutdown();
}

void UpdateChecker::StartAsync(const std::wstring& cache_dir,
                               std::wstring current_version,
                               UpdateCallback on_update) {
  if (worker_.joinable()) {
    return;
  }
  worker_ = std::thread(
      [this, cache_dir, current_version, on_update]() {
        Worker(cache_dir, current_version, std::move(on_update));
      });
}

void UpdateChecker::Shutdown() {
  stopping_ = true;
  if (worker_.joinable()) {
    worker_.join();
  }
}

void UpdateChecker::Worker(std::wstring cache_dir,
                           std::wstring current_version,
                           UpdateCallback on_update) {
  if (ShouldSkipCheck(cache_dir)) {
    return;
  }
  std::wstring body;
  bool fetched = HttpGetLatestRelease(&body);
  MarkChecked(cache_dir);
  if (stopping_ || !fetched) {
    return;
  }

  try {
    const auto payload = nlohmann::json::parse(body);
    if (!payload.contains("tag_name") || !payload["tag_name"].is_string()) {
      return;
    }
    const std::string tag = payload["tag_name"].get<std::string>();
    const std::wstring new_version(tag.begin(), tag.end());
    std::wstring release_url = kApiPath;  // fallback (unused in practice)
    if (payload.contains("html_url") && payload["html_url"].is_string()) {
      const std::string url = payload["html_url"].get<std::string>();
      release_url.assign(url.begin(), url.end());
    } else {
      release_url = L"https://github.com/zeddddf/scraps/releases/latest";
    }
    if (stopping_ || CompareVersions(new_version, current_version) <= 0) {
      return;
    }
    common::WriteLog(common::LogLevel::Info, "update.check",
                     "New version available: " + tag);
    if (on_update) {
      on_update(new_version, release_url);
    }
  } catch (const std::exception&) {
    // Malformed payload: silently ignore, retried on the next daily cycle.
  }
}

}  // namespace clipd2d::app
