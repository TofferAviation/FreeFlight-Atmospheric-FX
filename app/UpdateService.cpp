#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "UpdateService.h"

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ffatmo::app {
namespace {

std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), count);
    result.resize(static_cast<std::size_t>(count - 1));
    return result;
}

bool getHttps(const std::string& url, std::string& body, std::string& error) {
    const std::wstring wide = widen(url);
    URL_COMPONENTSW parts{}; parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{}; wchar_t path[2048]{};
    parts.lpszHostName = host; parts.dwHostNameLength = 255;
    parts.lpszUrlPath = path; parts.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        error = "Invalid HTTPS update URL"; return false;
    }
    HINTERNET session = WinHttpOpen(L"FFAtmoUpdater/0.4", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { error = "WinHTTP session failed"; return false; }
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    bool ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0, statusSize = sizeof(status);
    if (ok) ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     nullptr, &status, &statusSize, nullptr) && status == 200;
    while (ok) {
        DWORD available = 0; if (!WinHttpQueryDataAvailable(request, &available)) { ok = false; break; }
        if (!available) break;
        const std::size_t offset = body.size(); body.resize(offset + available); DWORD read = 0;
        if (!WinHttpReadData(request, body.data() + offset, available, &read)) { ok = false; break; }
        body.resize(offset + read);
    }
    if (!ok) error = "Update server did not return a valid manifest";
    if (request) WinHttpCloseHandle(request); if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session); return ok;
}

std::string stringValue(const std::string& json, const std::string& key) {
    const std::string token = "\"" + key + "\""; std::size_t p = json.find(token);
    if (p == std::string::npos || (p = json.find(':', p + token.size())) == std::string::npos) return {};
    p = json.find('"', p + 1); if (p == std::string::npos) return {}; std::string out;
    for (++p; p < json.size(); ++p) { if (json[p] == '"') break; if (json[p] == '\\' && p + 1 < json.size()) ++p; out.push_back(json[p]); }
    return out;
}

std::uint64_t numberValue(const std::string& json, const std::string& key) {
    const std::string token = "\"" + key + "\""; std::size_t p = json.find(token);
    if (p == std::string::npos || (p = json.find(':', p + token.size())) == std::string::npos) return 0;
    while (++p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) {}
    std::uint64_t value = 0; while (p < json.size() && std::isdigit(static_cast<unsigned char>(json[p]))) value = value * 10 + static_cast<unsigned>(json[p++] - '0');
    return value;
}

std::vector<std::string> arrayValue(const std::string& json, const std::string& key) {
    std::vector<std::string> values; const std::string token = "\"" + key + "\"";
    std::size_t p = json.find(token); if (p == std::string::npos || (p = json.find('[', p)) == std::string::npos) return values;
    const std::size_t end = json.find(']', p); while (p < end) { p = json.find('"', p + 1); if (p == std::string::npos || p >= end) break; const std::size_t q = json.find('"', p + 1); if (q == std::string::npos || q > end) break; values.push_back(json.substr(p + 1, q - p - 1)); p = q; }
    return values;
}

std::vector<int> versionParts(const std::string& text) {
    std::vector<int> result; int value = 0; bool active = false;
    for (char c : text) { if (std::isdigit(static_cast<unsigned char>(c))) { value = value * 10 + c - '0'; active = true; } else if (active) { result.push_back(value); value = 0; active = false; } }
    if (active) result.push_back(value); return result;
}
bool newer(const std::string& candidate, const std::string& current) {
    auto a = versionParts(candidate), b = versionParts(current); a.resize(std::max(a.size(), b.size())); b.resize(a.size()); return a > b;
}
std::wstring quote(const std::wstring& value) { return L"\"" + value + L"\""; }

}  // namespace

UpdateService::~UpdateService() { if (worker_.joinable()) worker_.join(); }

void UpdateService::start(const std::string& channel) {
    if (wcsstr(GetCommandLineW(), L"--test-update-popup")) {
        showTestPreview();
        return;
    }
    if (worker_.joinable()) return;
    worker_ = std::thread([this, channel] { check(channel); });
}

void UpdateService::showTestPreview() {
    UpdateInfo preview;
    preview.checked = true;
    preview.available = true;
    preview.previewOnly = true;
    preview.version = "0.4.1-test";
    preview.channel = "stable";
    preview.sizeBytes = 134217728;
    preview.notes = {"Aircraft-specific contrail profiles", "Improved wake-vortex lifecycle", "Safer window resizing", "Verified GitHub update workflow"};
    std::lock_guard<std::mutex> lock(mutex_);
    info_ = std::move(preview);
}

UpdateInfo UpdateService::snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return info_; }
void UpdateService::dismissCurrent() { std::lock_guard<std::mutex> lock(mutex_); info_.available = false; }

void UpdateService::check(std::string channel) {
    std::string json, error; UpdateInfo next; next.checked = true;
    if (getHttps(kUpdateManifestUrl, json, error)) {
        next.version = stringValue(json, "version"); next.channel = stringValue(json, "channel");
        next.downloadUrl = stringValue(json, "download_url"); next.sha256 = stringValue(json, "sha256");
        next.sizeBytes = numberValue(json, "size_bytes"); next.notes = arrayValue(json, "notes");
        if (next.version.empty() || next.downloadUrl.empty() || next.sha256.size() != 64) next.error = "Update manifest is incomplete";
        else next.available = (next.channel == channel || channel == "beta") && newer(next.version, kAppVersion);
    } else next.error = error;
    std::lock_guard<std::mutex> lock(mutex_); info_ = std::move(next);
}

bool UpdateService::launchUpdater(const std::wstring& appDirectory, std::string* error) const {
    const UpdateInfo update = snapshot(); if (!update.available) { if (error) *error = "No update is ready"; return false; }
    if (update.previewOnly) { if (error) *error = "Test preview cannot install files"; return false; }
    const std::wstring helper = appDirectory + L"\\FFAtmoUpdater.exe";
    const std::wstring arguments = quote(widen(update.downloadUrl)) + L" " + quote(widen(update.sha256)) + L" " + quote(appDirectory) + L" " + std::to_wstring(GetCurrentProcessId());
    HINSTANCE result = ShellExecuteW(nullptr, L"open", helper.c_str(), arguments.c_str(), appDirectory.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) { if (error) *error = "Could not start FFAtmoUpdater.exe"; return false; }
    return true;
}

}  // namespace ffatmo::app
