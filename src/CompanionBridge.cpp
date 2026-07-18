#include "CompanionBridge.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace ffatmo {
namespace {

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(),
        [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(),
        [](unsigned char c) { return std::isspace(c); }).base();
    return first < last ? std::string(first, last) : std::string();
}

bool asBool(const std::unordered_map<std::string, std::string>& values,
            const char* key, bool fallback = false) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    return it->second == "1" || it->second == "true" || it->second == "yes";
}

float asFloat(const std::unordered_map<std::string, std::string>& values,
              const char* key, float fallback = 0.0f) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    try { return std::stof(it->second); } catch (...) { return fallback; }
}

std::string asString(const std::unordered_map<std::string, std::string>& values,
                     const char* key) {
    const auto it = values.find(key);
    return it == values.end() ? std::string() : it->second;
}

}  // namespace

bool CompanionBridge::loadStatus(const std::string& path, CompanionStatus& status,
                                 std::string* error) {
    std::ifstream input(path);
    if (!input) {
        if (error) *error = "status file is not available";
        return false;
    }
    std::unordered_map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        values[trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
    }
    status.pluginRunning = asBool(values, "plugin_running");
    status.particleObjectLoaded = asBool(values, "particle_object_loaded");
    status.aircraftPath = asString(values, "aircraft_path");
    status.profileName = asString(values, "profile_name");
    status.settings.enabled = asBool(values, "enabled", true);
    status.settings.previewMode = asBool(values, "preview_mode");
    status.settings.quality = static_cast<QualityPreset>(std::clamp(
        static_cast<int>(asFloat(values, "quality", 2.0f)), 0, 3));
    status.input.ambientTemperatureC = asFloat(values, "ambient_temperature_c", 15.0f);
    status.input.pressurePa = asFloat(values, "pressure_pa", 101325.0f);
    status.input.altitudeFt = asFloat(values, "altitude_ft");
    status.input.trueAirspeedMps = asFloat(values, "true_airspeed_mps");
    status.input.mach = asFloat(values, "mach");
    status.input.angleOfAttackDeg = asFloat(values, "angle_of_attack_deg");
    status.input.frameRateFps = asFloat(values, "frame_rate_fps", 60.0f);
    status.state.relativeHumidityWaterPercent = asFloat(values, "rh_water_percent");
    status.state.relativeHumidityIcePercent = asFloat(values, "rh_ice_percent");
    status.state.formationProbability = asFloat(values, "formation_probability");
    status.state.persistenceProbability = asFloat(values, "persistence_probability");
    status.state.contrailCoreRatio = asFloat(values, "contrail_core_ratio");
    status.state.contrailSpreadRatio = asFloat(values, "contrail_spread_ratio");
    status.state.contrailPersistenceSeconds = asFloat(values, "persistence_seconds");
    status.state.estimatedTrailLengthKm = asFloat(values, "estimated_length_km");
    status.state.wingCondensationRatio = asFloat(values, "wing_condensation_ratio");
    status.state.wingVortexRatio = asFloat(values, "wing_vortex_ratio");
    status.state.primaryWakeRatio = asFloat(values, "primary_wake_ratio");
    status.state.secondaryCurtainRatio = asFloat(values, "secondary_curtain_ratio");
    status.state.contrailCirrusRatio = asFloat(values, "contrail_cirrus_ratio");
    status.state.particleBudgetRatio = asFloat(values, "particle_budget_ratio", 1.0f);
    return true;
}

bool CompanionBridge::saveStatus(const std::string& path, const CompanionStatus& status,
                                 std::string* error) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        if (error) *error = "could not write companion status";
        return false;
    }
    output << std::fixed << std::setprecision(4)
           << "plugin_running=" << (status.pluginRunning ? 1 : 0) << '\n'
           << "particle_object_loaded=" << (status.particleObjectLoaded ? 1 : 0) << '\n'
           << "aircraft_path=" << status.aircraftPath << '\n'
           << "profile_name=" << status.profileName << '\n'
           << "enabled=" << (status.settings.enabled ? 1 : 0) << '\n'
           << "preview_mode=" << (status.settings.previewMode ? 1 : 0) << '\n'
           << "quality=" << static_cast<int>(status.settings.quality) << '\n'
           << "ambient_temperature_c=" << status.input.ambientTemperatureC << '\n'
           << "pressure_pa=" << status.input.pressurePa << '\n'
           << "altitude_ft=" << status.input.altitudeFt << '\n'
           << "true_airspeed_mps=" << status.input.trueAirspeedMps << '\n'
           << "mach=" << status.input.mach << '\n'
           << "angle_of_attack_deg=" << status.input.angleOfAttackDeg << '\n'
           << "frame_rate_fps=" << status.input.frameRateFps << '\n'
           << "rh_water_percent=" << status.state.relativeHumidityWaterPercent << '\n'
           << "rh_ice_percent=" << status.state.relativeHumidityIcePercent << '\n'
           << "formation_probability=" << status.state.formationProbability << '\n'
           << "persistence_probability=" << status.state.persistenceProbability << '\n'
           << "contrail_core_ratio=" << status.state.contrailCoreRatio << '\n'
           << "contrail_spread_ratio=" << status.state.contrailSpreadRatio << '\n'
           << "persistence_seconds=" << status.state.contrailPersistenceSeconds << '\n'
           << "estimated_length_km=" << status.state.estimatedTrailLengthKm << '\n'
           << "wing_condensation_ratio=" << status.state.wingCondensationRatio << '\n'
           << "wing_vortex_ratio=" << status.state.wingVortexRatio << '\n'
           << "primary_wake_ratio=" << status.state.primaryWakeRatio << '\n'
           << "secondary_curtain_ratio=" << status.state.secondaryCurtainRatio << '\n'
           << "contrail_cirrus_ratio=" << status.state.contrailCirrusRatio << '\n'
           << "particle_budget_ratio=" << status.state.particleBudgetRatio << '\n';
    if (!output.good()) {
        if (error) *error = "failed while writing companion status";
        return false;
    }
    return true;
}

}  // namespace ffatmo
