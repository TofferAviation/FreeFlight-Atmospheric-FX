#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ffatmo::app {

#ifndef FFATMO_APP_VERSION
#define FFATMO_APP_VERSION "0.4.0"
#endif
inline constexpr const char* kAppVersion = FFATMO_APP_VERSION;
inline constexpr const char* kUpdateManifestUrl =
    "https://raw.githubusercontent.com/TofferAviation/FreeFlight-Atmospheric-FX/main/update/latest.json";
inline constexpr const char* kTestUpdateManifestUrl =
    "https://raw.githubusercontent.com/TofferAviation/FreeFlight-Atmospheric-FX/refs/heads/agent/updater-foundation/update/test.json";

struct UpdateInfo {
    bool checked = false;
    bool available = false;
    bool previewOnly = false;
    std::string version;
    std::string channel = "stable";
    std::string downloadUrl;
    std::string sha256;
    std::uint64_t sizeBytes = 0;
    std::vector<std::string> notes;
    std::string error;
};

class UpdateService {
public:
    UpdateService() = default;
    ~UpdateService();
    UpdateService(const UpdateService&) = delete;
    UpdateService& operator=(const UpdateService&) = delete;

    void start(const std::string& channel = "stable");
    void showTestPreview();
    UpdateInfo snapshot() const;
    void dismissCurrent();
    bool launchUpdater(const std::wstring& appDirectory, std::string* error = nullptr) const;

private:
    void check(std::string channel, std::string manifestUrl);
    mutable std::mutex mutex_;
    UpdateInfo info_;
    std::thread worker_;
};

}  // namespace ffatmo::app
