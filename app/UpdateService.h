#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ffatmo::app {

inline constexpr const char* kAppVersion = "0.4.0";
inline constexpr const char* kUpdateManifestUrl =
    "https://raw.githubusercontent.com/TofferAviation/FreeFlight-Atmospheric-FX/main/update/latest.json";

struct UpdateInfo {
    bool checked = false;
    bool available = false;
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
    UpdateInfo snapshot() const;
    void dismissCurrent();
    bool launchUpdater(const std::wstring& appDirectory, std::string* error = nullptr) const;

private:
    void check(std::string channel);
    mutable std::mutex mutex_;
    UpdateInfo info_;
    std::thread worker_;
};

}  // namespace ffatmo::app
