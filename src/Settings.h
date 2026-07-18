#pragma once

#include "AtmosphereModel.h"

#include <string>

namespace ffatmo {

class SettingsStore {
public:
    static bool load(const std::string& path, EffectSettings& settings, std::string* error = nullptr);
    static bool save(const std::string& path, const EffectSettings& settings, std::string* error = nullptr);
};

}  // namespace ffatmo
