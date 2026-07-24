#pragma once

#include "engine/SimulatorSnapshot.h"

#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ffatmo::host {

struct DataRefStatistic {
    std::string name;
    bool resolved = false;
    int runtimeTypeMask = 0;
    int arrayLength = 0;
    std::uint64_t sampleCount = 0;
    std::uint64_t nonFiniteCount = 0;
    std::uint64_t unchangedCount = 0;
    double minimum = 0.0;
    double maximum = 0.0;
    double lastValue = 0.0;
    bool hasValue = false;
};

class XPlaneSnapshotSource {
public:
    bool resolve();
    bool ready() const { return ready_; }

    engine::SimulatorSnapshot capture(std::uint64_t sequenceNumber,
                                      float deltaTimeSeconds,
                                      std::uint32_t lifecycleFlags);

    // Non-const identity access refreshes X-Plane's current aircraft first. This
    // is used by recorder startup so filenames and metadata are not created from
    // the empty values that can exist while plugins are enabled before an ACF is
    // fully loaded.
    std::string aircraftName() {
        refreshIdentity();
        return cachedAircraftName_;
    }
    std::string aircraftIcao() {
        refreshIdentity();
        return cachedAircraftIcao_;
    }
    std::string aircraftRelativePath() {
        refreshIdentity();
        return cachedAircraftRelativePath_;
    }

    std::string aircraftName() const;
    std::string aircraftIcao() const;
    std::string aircraftRelativePath() const;
    std::string xplaneVersionString() const;

    const std::vector<DataRefStatistic>& statistics() const { return statistics_; }
    bool writeValidationReport(const std::filesystem::path& outputPath,
                               std::string* error = nullptr) const;

private:
    XPLMDataRef addScalar(const char* name, bool required = false);
    XPLMDataRef addFloatArray(const char* name);
    XPLMDataRef addIntArray(const char* name);
    XPLMDataRef addByteArray(const char* name);

    double readNumber(XPLMDataRef ref, const char* name, double fallback = 0.0);
    float readFloat(XPLMDataRef ref, const char* name, float fallback = 0.0f);
    int readInt(XPLMDataRef ref, const char* name, int fallback = 0);
    int readFloatArray(XPLMDataRef ref,
                       const char* name,
                       float* destination,
                       int maximumCount);
    int readIntArray(XPLMDataRef ref,
                     const char* name,
                     int* destination,
                     int maximumCount);
    std::string readString(XPLMDataRef ref) const;
    void observe(const char* name, double value);
    void refreshIdentity();

    XPLMDataRef framePeriod_ = nullptr;
    XPLMDataRef totalRunningTime_ = nullptr;
    XPLMDataRef totalFlightTime_ = nullptr;
    XPLMDataRef paused_ = nullptr;
    XPLMDataRef simSpeedActual_ = nullptr;
    XPLMDataRef replaying_ = nullptr;

    XPLMDataRef localX_ = nullptr;
    XPLMDataRef localY_ = nullptr;
    XPLMDataRef localZ_ = nullptr;
    XPLMDataRef latitude_ = nullptr;
    XPLMDataRef longitude_ = nullptr;
    XPLMDataRef elevation_ = nullptr;
    XPLMDataRef pitch_ = nullptr;
    XPLMDataRef roll_ = nullptr;
    XPLMDataRef heading_ = nullptr;
    XPLMDataRef localVx_ = nullptr;
    XPLMDataRef localVy_ = nullptr;
    XPLMDataRef localVz_ = nullptr;
    XPLMDataRef localAx_ = nullptr;
    XPLMDataRef localAy_ = nullptr;
    XPLMDataRef localAz_ = nullptr;
    XPLMDataRef rollRate_ = nullptr;
    XPLMDataRef pitchRate_ = nullptr;
    XPLMDataRef yawRate_ = nullptr;
    XPLMDataRef trueAirspeed_ = nullptr;
    XPLMDataRef groundSpeed_ = nullptr;
    XPLMDataRef angleOfAttack_ = nullptr;
    XPLMDataRef sideslip_ = nullptr;
    XPLMDataRef heightAgl_ = nullptr;
    XPLMDataRef normalLoad_ = nullptr;

    XPLMDataRef temperature_ = nullptr;
    XPLMDataRef pressure_ = nullptr;
    XPLMDataRef density_ = nullptr;
    XPLMDataRef speedOfSound_ = nullptr;
    XPLMDataRef windX_ = nullptr;
    XPLMDataRef windY_ = nullptr;
    XPLMDataRef windZ_ = nullptr;
    XPLMDataRef thermalRate_ = nullptr;
    XPLMDataRef precipitation_ = nullptr;
    XPLMDataRef snow_ = nullptr;
    XPLMDataRef hail_ = nullptr;
    XPLMDataRef gravity_ = nullptr;

    XPLMDataRef atmosphereLevels_ = nullptr;
    XPLMDataRef temperatureLevels_ = nullptr;
    XPLMDataRef dewPointLevels_ = nullptr;
    XPLMDataRef windAltitudeLevels_ = nullptr;
    XPLMDataRef windSpeedLevels_ = nullptr;
    XPLMDataRef windDirectionLevels_ = nullptr;
    XPLMDataRef shearSpeedLevels_ = nullptr;
    XPLMDataRef shearDirectionLevels_ = nullptr;
    XPLMDataRef turbulenceLevels_ = nullptr;

    XPLMDataRef engineCount_ = nullptr;
    XPLMDataRef engineRunning_ = nullptr;
    XPLMDataRef engineN1_ = nullptr;
    XPLMDataRef engineN2_ = nullptr;
    XPLMDataRef engineFuelFlow_ = nullptr;
    XPLMDataRef engineThrust_ = nullptr;
    XPLMDataRef engineThrottle_ = nullptr;
    XPLMDataRef engineEgt_ = nullptr;
    XPLMDataRef engineItt_ = nullptr;
    XPLMDataRef engineJetwash_ = nullptr;
    XPLMDataRef engineExhaustVelocity_ = nullptr;

    XPLMDataRef totalMass_ = nullptr;
    XPLMDataRef totalFuelMass_ = nullptr;
    XPLMDataRef flapRatio_ = nullptr;
    XPLMDataRef slatRatio_ = nullptr;

    XPLMDataRef aircraftName_ = nullptr;
    XPLMDataRef aircraftIcao_ = nullptr;
    XPLMDataRef aircraftRelativePath_ = nullptr;

    std::vector<DataRefStatistic> statistics_;
    std::unordered_map<std::string, std::size_t> statisticIndex_;
    std::string cachedAircraftName_;
    std::string cachedAircraftIcao_;
    std::string cachedAircraftRelativePath_;
    std::chrono::steady_clock::time_point monotonicOrigin_ = std::chrono::steady_clock::now();
    bool ready_ = false;
};

}  // namespace ffatmo::host
