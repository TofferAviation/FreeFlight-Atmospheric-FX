#include "Settings.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace ffatmo {
namespace {

std::string trim(std::string text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return first < last ? std::string(first, last) : std::string();
}

bool parseBool(const std::string& text, bool fallback) {
    if (text == "1" || text == "true" || text == "yes" || text == "on") return true;
    if (text == "0" || text == "false" || text == "no" || text == "off") return false;
    return fallback;
}

float parseFloat(const std::string& text, float fallback) {
    try {
        return std::stof(text);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

bool SettingsStore::load(const std::string& path, EffectSettings& settings, std::string* error) {
    std::ifstream stream(path);
    if (!stream) {
        if (error) *error = "settings file does not exist; defaults retained";
        return false;
    }

    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));

        if (key == "enabled") settings.enabled = parseBool(value, settings.enabled);
        else if (key == "automatic_weather") settings.automaticWeather = parseBool(value, settings.automaticWeather);
        else if (key == "preview_mode") settings.previewMode = parseBool(value, settings.previewMode);
        else if (key == "quality") settings.quality = static_cast<QualityPreset>(
            std::clamp(static_cast<int>(parseFloat(value, static_cast<float>(settings.quality))), 0, 3));
        else if (key == "contrail_intensity") settings.contrailIntensity = std::clamp(parseFloat(value, 1.0f), 0.0f, 2.0f);
        else if (key == "wing_condensation_intensity") settings.wingCondensationIntensity = std::clamp(parseFloat(value, 1.0f), 0.0f, 2.0f);
        else if (key == "wing_vortex_intensity") settings.wingVortexIntensity = std::clamp(parseFloat(value, 1.0f), 0.0f, 2.0f);
        else if (key == "persistence_scale") settings.persistenceScale = std::clamp(parseFloat(value, 1.0f), 0.25f, 2.0f);
        else if (key == "minimum_contrail_altitude_ft") settings.minimumContrailAltitudeFt =
            std::clamp(parseFloat(value, 18000.0f), 0.0f, 50000.0f);
        else if (key == "adaptive_target_fps") settings.adaptiveTargetFps =
            std::clamp(parseFloat(value, 30.0f), 15.0f, 120.0f);
        else if (key == "adaptive_quality") settings.adaptiveQuality = parseBool(value, settings.adaptiveQuality);
    }
    return true;
}

bool SettingsStore::save(const std::string& path, const EffectSettings& settings, std::string* error) {
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
        if (error) *error = "could not open settings file for writing";
        return false;
    }
    stream << "# FFAtmospherics settings\n"
           << "enabled=" << (settings.enabled ? 1 : 0) << '\n'
           << "automatic_weather=" << (settings.automaticWeather ? 1 : 0) << '\n'
           << "preview_mode=" << (settings.previewMode ? 1 : 0) << '\n'
           << "quality=" << static_cast<int>(settings.quality) << '\n'
           << "contrail_intensity=" << settings.contrailIntensity << '\n'
           << "wing_condensation_intensity=" << settings.wingCondensationIntensity << '\n'
           << "wing_vortex_intensity=" << settings.wingVortexIntensity << '\n'
           << "persistence_scale=" << settings.persistenceScale << '\n'
           << "minimum_contrail_altitude_ft=" << settings.minimumContrailAltitudeFt << '\n'
           << "adaptive_target_fps=" << settings.adaptiveTargetFps << '\n'
           << "adaptive_quality=" << (settings.adaptiveQuality ? 1 : 0) << '\n';
    if (!stream.good()) {
        if (error) *error = "failed while writing settings";
        return false;
    }
    return true;
}

}  // namespace ffatmo
