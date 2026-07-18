#pragma once

#include "CompanionBridge.h"

#include <memory>
#include <string>

namespace ffatmo::app {

class TelemetryProvider {
public:
    virtual ~TelemetryProvider() = default;
    virtual const char* name() const = 0;
    virtual bool available() const = 0;
    virtual bool read(CompanionStatus& status, std::string* error = nullptr) = 0;
};

class FileBridgeTelemetry final : public TelemetryProvider {
public:
    explicit FileBridgeTelemetry(std::string statusPath);
    const char* name() const override { return "FFAtmo plugin bridge"; }
    bool available() const override;
    bool read(CompanionStatus& status, std::string* error = nullptr) override;

private:
    std::string statusPath_;
};

// Reserved integration point for an optional XUIPC-backed provider. The base
// product never requires XUIPC; a future adapter can implement this interface
// and be selected when its runtime is detected.
std::unique_ptr<TelemetryProvider> createXUIPCProviderIfAvailable();

}  // namespace ffatmo::app
