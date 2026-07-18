#pragma once

#include "AtmosphereModel.h"

#include <string>

namespace ffatmo {

struct CompanionStatus {
    bool pluginRunning = false;
    bool particleObjectLoaded = false;
    std::string aircraftPath;
    std::string profileName;
    EffectSettings settings {};
    AtmosphereInput input {};
    EffectState state {};
};

class CompanionBridge {
public:
    static bool loadStatus(const std::string& path, CompanionStatus& status,
                           std::string* error = nullptr);
    static bool saveStatus(const std::string& path, const CompanionStatus& status,
                           std::string* error = nullptr);
};

}  // namespace ffatmo
