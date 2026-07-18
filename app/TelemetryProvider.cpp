#include "TelemetryProvider.h"

#include <filesystem>

namespace ffatmo::app {

FileBridgeTelemetry::FileBridgeTelemetry(std::string statusPath)
    : statusPath_(std::move(statusPath)) {}

bool FileBridgeTelemetry::available() const {
    std::error_code error;
    return std::filesystem::exists(statusPath_, error);
}

bool FileBridgeTelemetry::read(CompanionStatus& status, std::string* error) {
    return CompanionBridge::loadStatus(statusPath_, status, error);
}

std::unique_ptr<TelemetryProvider> createXUIPCProviderIfAvailable() {
    return nullptr;
}

}  // namespace ffatmo::app
