#include "host/XPlaneSnapshotSource.h"

#include "XPLMPlugin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace ffatmo::host {
namespace {

constexpr float kKelvinOffset = 273.15f;

int arrayLength(XPLMDataRef ref, XPLMDataTypeID type) {
    if (!ref) return 0;
    if (type & xplmType_FloatArray) return XPLMGetDatavf(ref, nullptr, 0, 0);
    if (type & xplmType_IntArray) return XPLMGetDatavi(ref, nullptr, 0, 0);
    if (type & xplmType_Data) return XPLMGetDatab(ref, nullptr, 0, 0);
    return 0;
}

std::uint32_t boundedCount(int value, std::size_t maximum) {
    return static_cast<std::uint32_t>(std::clamp(value, 0, static_cast<int>(maximum)));
}

}  // namespace

XPLMDataRef XPlaneSnapshotSource::addScalar(const char* name, bool) {
    XPLMDataRef ref = XPLMFindDataRef(name);
    DataRefStatistic statistic;
    statistic.name = name;
    statistic.resolved = ref != nullptr;
    if (ref) statistic.runtimeTypeMask = static_cast<int>(XPLMGetDataRefTypes(ref));
    statisticIndex_[statistic.name] = statistics_.size();
    statistics_.push_back(std::move(statistic));
    return ref;
}

XPLMDataRef XPlaneSnapshotSource::addFloatArray(const char* name) {
    XPLMDataRef ref = XPLMFindDataRef(name);
    DataRefStatistic statistic;
    statistic.name = name;
    statistic.resolved = ref != nullptr;
    if (ref) {
        const auto type = XPLMGetDataRefTypes(ref);
        statistic.runtimeTypeMask = static_cast<int>(type);
        statistic.arrayLength = arrayLength(ref, type);
    }
    statisticIndex_[statistic.name] = statistics_.size();
    statistics_.push_back(std::move(statistic));
    return ref;
}

XPLMDataRef XPlaneSnapshotSource::addIntArray(const char* name) {
    return addFloatArray(name);
}

XPLMDataRef XPlaneSnapshotSource::addByteArray(const char* name) {
    return addFloatArray(name);
}

bool XPlaneSnapshotSource::resolve() {
    statistics_.clear();
    statisticIndex_.clear();
    statistics_.reserve(80);
    statisticIndex_.reserve(80);

    framePeriod_ = addScalar("sim/operation/misc/frame_rate_period");
    totalRunningTime_ = addScalar("sim/time/total_running_time_sec", true);
    totalFlightTime_ = addScalar("sim/time/total_flight_time_sec");
    paused_ = addScalar("sim/time/paused");
    simSpeedActual_ = addScalar("sim/time/sim_speed_actual");
    replaying_ = addScalar("sim/time/is_in_replay");

    localX_ = addScalar("sim/flightmodel/position/local_x", true);
    localY_ = addScalar("sim/flightmodel/position/local_y", true);
    localZ_ = addScalar("sim/flightmodel/position/local_z", true);
    latitude_ = addScalar("sim/flightmodel/position/latitude");
    longitude_ = addScalar("sim/flightmodel/position/longitude");
    elevation_ = addScalar("sim/flightmodel/position/elevation");
    pitch_ = addScalar("sim/flightmodel/position/theta");
    roll_ = addScalar("sim/flightmodel/position/phi");
    heading_ = addScalar("sim/flightmodel/position/psi");
    localVx_ = addScalar("sim/flightmodel/position/local_vx");
    localVy_ = addScalar("sim/flightmodel/position/local_vy");
    localVz_ = addScalar("sim/flightmodel/position/local_vz");
    localAx_ = addScalar("sim/flightmodel/position/local_ax");
    localAy_ = addScalar("sim/flightmodel/position/local_ay");
    localAz_ = addScalar("sim/flightmodel/position/local_az");
    rollRate_ = addScalar("sim/flightmodel/position/Prad");
    pitchRate_ = addScalar("sim/flightmodel/position/Qrad");
    yawRate_ = addScalar("sim/flightmodel/position/Rrad");
    trueAirspeed_ = addScalar("sim/flightmodel/position/true_airspeed");
    groundSpeed_ = addScalar("sim/flightmodel/position/groundspeed");
    angleOfAttack_ = addScalar("sim/flightmodel/position/alpha");
    sideslip_ = addScalar("sim/flightmodel/position/beta");
    heightAgl_ = addScalar("sim/flightmodel/position/y_agl");
    normalLoad_ = addScalar("sim/flightmodel/forces/g_nrml");

    temperature_ = addScalar("sim/weather/aircraft/temperature_ambient_deg_c", true);
    pressure_ = addScalar("sim/weather/aircraft/barometer_current_pas", true);
    density_ = addScalar("sim/weather/rho");
    speedOfSound_ = addScalar("sim/weather/aircraft/speed_sound_ms");
    windX_ = addScalar("sim/weather/aircraft/wind_now_x_msc");
    windY_ = addScalar("sim/weather/aircraft/wind_now_y_msc");
    windZ_ = addScalar("sim/weather/aircraft/wind_now_z_msc");
    thermalRate_ = addScalar("sim/weather/aircraft/thermal_rate_ms");
    precipitation_ = addScalar("sim/weather/aircraft/precipitation_on_aircraft_ratio");
    snow_ = addScalar("sim/weather/aircraft/snow_on_aircraft_ratio");
    hail_ = addScalar("sim/weather/aircraft/hail_on_aircraft_ratio");
    gravity_ = addScalar("sim/weather/aircraft/gravity_mss");

    atmosphereLevels_ = addFloatArray("sim/weather/region/atmosphere_alt_levels_m");
    temperatureLevels_ = addFloatArray("sim/weather/region/temperature_altitude_msl_m");
    dewPointLevels_ = addFloatArray("sim/weather/region/dewpoint_deg_c");
    windAltitudeLevels_ = addFloatArray("sim/weather/region/wind_altitude_msl_m");
    windSpeedLevels_ = addFloatArray("sim/weather/region/wind_speed_msc");
    windDirectionLevels_ = addFloatArray("sim/weather/region/wind_direction_degt");
    shearSpeedLevels_ = addFloatArray("sim/weather/region/shear_speed_msc");
    shearDirectionLevels_ = addFloatArray("sim/weather/region/shear_direction_degt");
    turbulenceLevels_ = addFloatArray("sim/weather/region/turbulence");

    engineCount_ = addScalar("sim/aircraft/engine/acf_num_engines");
    engineRunning_ = addIntArray("sim/flightmodel/engine/ENGN_running");
    engineN1_ = addFloatArray("sim/flightmodel/engine/ENGN_N1_");
    engineN2_ = addFloatArray("sim/flightmodel/engine/ENGN_N2_");
    engineFuelFlow_ = addFloatArray("sim/flightmodel/engine/ENGN_FF_");
    engineThrust_ = addFloatArray("sim/flightmodel/engine/POINT_thrust");
    engineThrottle_ = addFloatArray("sim/flightmodel/engine/ENGN_thro_use");
    engineEgt_ = addFloatArray("sim/flightmodel2/engines/EGT_deg_cel");
    engineItt_ = addFloatArray("sim/flightmodel2/engines/ITT_deg_cel");
    engineJetwash_ = addFloatArray("sim/flightmodel2/engines/jetwash_mtr_sec");
    engineExhaustVelocity_ = addFloatArray("sim/flightmodel2/engines/engn_exhaust_speed_msc");

    totalMass_ = addScalar("sim/flightmodel/weight/m_total");
    totalFuelMass_ = addScalar("sim/flightmodel/weight/m_fuel_total");
    flapRatio_ = addScalar("sim/flightmodel/controls/flaprat");
    slatRatio_ = addScalar("sim/flightmodel/controls/slatrat");

    aircraftName_ = addByteArray("sim/aircraft/view/acf_ui_name");
    aircraftIcao_ = addByteArray("sim/aircraft/view/acf_ICAO");
    aircraftRelativePath_ = addByteArray("sim/aircraft/view/acf_relative_path");

    refreshIdentity();
    monotonicOrigin_ = std::chrono::steady_clock::now();
    ready_ = totalRunningTime_ && localX_ && localY_ && localZ_ && temperature_ && pressure_;
    return ready_;
}

void XPlaneSnapshotSource::observe(const char* name, double value) {
    const auto found = statisticIndex_.find(name);
    if (found == statisticIndex_.end()) return;
    auto& statistic = statistics_[found->second];
    ++statistic.sampleCount;
    if (!std::isfinite(value)) {
        ++statistic.nonFiniteCount;
        return;
    }
    if (!statistic.hasValue) {
        statistic.minimum = value;
        statistic.maximum = value;
        statistic.lastValue = value;
        statistic.hasValue = true;
        return;
    }
    if (value == statistic.lastValue) ++statistic.unchangedCount;
    statistic.minimum = std::min(statistic.minimum, value);
    statistic.maximum = std::max(statistic.maximum, value);
    statistic.lastValue = value;
}

double XPlaneSnapshotSource::readNumber(XPLMDataRef ref, const char* name, double fallback) {
    if (!ref) return fallback;
    const auto type = XPLMGetDataRefTypes(ref);
    double value = fallback;
    if (type & xplmType_Double) value = XPLMGetDatad(ref);
    else if (type & xplmType_Float) value = static_cast<double>(XPLMGetDataf(ref));
    else if (type & xplmType_Int) value = static_cast<double>(XPLMGetDatai(ref));
    observe(name, value);
    return std::isfinite(value) ? value : fallback;
}

float XPlaneSnapshotSource::readFloat(XPLMDataRef ref, const char* name, float fallback) {
    return static_cast<float>(readNumber(ref, name, fallback));
}

int XPlaneSnapshotSource::readInt(XPLMDataRef ref, const char* name, int fallback) {
    return static_cast<int>(readNumber(ref, name, fallback));
}

int XPlaneSnapshotSource::readFloatArray(XPLMDataRef ref,
                                         const char* name,
                                         float* destination,
                                         int maximumCount) {
    if (!ref || !destination || maximumCount <= 0) return 0;
    const int count = XPLMGetDatavf(ref, destination, 0, maximumCount);
    for (int index = 0; index < count; ++index) observe(name, destination[index]);
    return count;
}

int XPlaneSnapshotSource::readIntArray(XPLMDataRef ref,
                                       const char* name,
                                       int* destination,
                                       int maximumCount) {
    if (!ref || !destination || maximumCount <= 0) return 0;
    const int count = XPLMGetDatavi(ref, destination, 0, maximumCount);
    for (int index = 0; index < count; ++index) observe(name, destination[index]);
    return count;
}

std::string XPlaneSnapshotSource::readString(XPLMDataRef ref) const {
    if (!ref) return {};
    const int sourceLength = XPLMGetDatab(ref, nullptr, 0, 0);
    if (sourceLength <= 0) return {};
    const int length = std::min(sourceLength, 2048);
    std::vector<char> bytes(static_cast<std::size_t>(length) + 1, '\0');
    const int read = XPLMGetDatab(ref, bytes.data(), 0, length);
    if (read <= 0) return {};
    return std::string(bytes.data(), bytes.data() + read).c_str();
}

void XPlaneSnapshotSource::refreshIdentity() {
    cachedAircraftName_ = readString(aircraftName_);
    cachedAircraftIcao_ = readString(aircraftIcao_);
    cachedAircraftRelativePath_ = readString(aircraftRelativePath_);
}

engine::SimulatorSnapshot XPlaneSnapshotSource::capture(std::uint64_t sequenceNumber,
                                                        float deltaTimeSeconds,
                                                        std::uint32_t lifecycleFlags) {
    if ((lifecycleFlags & (engine::LifecycleAircraftLoaded | engine::LifecycleAircraftChanged)) != 0u ||
        cachedAircraftRelativePath_.empty()) {
        refreshIdentity();
    }

    engine::SimulatorSnapshot snapshot;
    snapshot.sequenceNumber = sequenceNumber;
    snapshot.lifecycleFlags = lifecycleFlags;
    snapshot.deltaTimeSeconds = std::clamp(deltaTimeSeconds, 0.0f, 1.0f);
    snapshot.monotonicTimeSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - monotonicOrigin_).count();

    snapshot.simulatorUptimeSeconds = readNumber(totalRunningTime_, "sim/time/total_running_time_sec");
    snapshot.flightTimeSeconds = readNumber(totalFlightTime_, "sim/time/total_flight_time_sec");
    snapshot.paused = readInt(paused_, "sim/time/paused") != 0 ? 1u : 0u;
    snapshot.timeAcceleration = readFloat(simSpeedActual_, "sim/time/sim_speed_actual", 1.0f);
    snapshot.replaying = readInt(replaying_, "sim/time/is_in_replay") != 0 ? 1u : 0u;
    snapshot.validityMask |= engine::ValidTime;

    snapshot.latitudeDeg = readNumber(latitude_, "sim/flightmodel/position/latitude");
    snapshot.longitudeDeg = readNumber(longitude_, "sim/flightmodel/position/longitude");
    snapshot.elevationMslM = readNumber(elevation_, "sim/flightmodel/position/elevation");
    if (latitude_ && longitude_ && elevation_) snapshot.validityMask |= engine::ValidGeodeticPosition;

    snapshot.localPositionM.x = readNumber(localX_, "sim/flightmodel/position/local_x");
    snapshot.localPositionM.y = readNumber(localY_, "sim/flightmodel/position/local_y");
    snapshot.localPositionM.z = readNumber(localZ_, "sim/flightmodel/position/local_z");
    snapshot.pitchDeg = readFloat(pitch_, "sim/flightmodel/position/theta");
    snapshot.rollDeg = readFloat(roll_, "sim/flightmodel/position/phi");
    snapshot.headingDegTrue = readFloat(heading_, "sim/flightmodel/position/psi");
    snapshot.validityMask |= engine::ValidLocalTransform;

    snapshot.linearVelocityLocalMps.x = readFloat(localVx_, "sim/flightmodel/position/local_vx");
    snapshot.linearVelocityLocalMps.y = readFloat(localVy_, "sim/flightmodel/position/local_vy");
    snapshot.linearVelocityLocalMps.z = readFloat(localVz_, "sim/flightmodel/position/local_vz");
    snapshot.accelerationLocalMps2.x = readFloat(localAx_, "sim/flightmodel/position/local_ax");
    snapshot.accelerationLocalMps2.y = readFloat(localAy_, "sim/flightmodel/position/local_ay");
    snapshot.accelerationLocalMps2.z = readFloat(localAz_, "sim/flightmodel/position/local_az");
    if (localVx_ && localVy_ && localVz_) snapshot.validityMask |= engine::ValidLinearMotion;

    snapshot.angularVelocityBodyRadps.x = readFloat(rollRate_, "sim/flightmodel/position/Prad");
    snapshot.angularVelocityBodyRadps.y = readFloat(pitchRate_, "sim/flightmodel/position/Qrad");
    snapshot.angularVelocityBodyRadps.z = readFloat(yawRate_, "sim/flightmodel/position/Rrad");
    if (rollRate_ && pitchRate_ && yawRate_) snapshot.validityMask |= engine::ValidAngularMotion;

    snapshot.trueAirspeedMps = readFloat(trueAirspeed_, "sim/flightmodel/position/true_airspeed");
    snapshot.groundSpeedMps = readFloat(groundSpeed_, "sim/flightmodel/position/groundspeed");
    snapshot.angleOfAttackDeg = readFloat(angleOfAttack_, "sim/flightmodel/position/alpha");
    snapshot.sideslipDeg = readFloat(sideslip_, "sim/flightmodel/position/beta");
    snapshot.heightAglM = readFloat(heightAgl_, "sim/flightmodel/position/y_agl");
    snapshot.normalLoadFactorG = readFloat(normalLoad_, "sim/flightmodel/forces/g_nrml", 1.0f);
    if (trueAirspeed_ && angleOfAttack_) snapshot.validityMask |= engine::ValidAerodynamics;

    snapshot.totalMassKg = readFloat(totalMass_, "sim/flightmodel/weight/m_total");
    snapshot.totalFuelMassKg = readFloat(totalFuelMass_, "sim/flightmodel/weight/m_fuel_total");
    snapshot.flapRatio = readFloat(flapRatio_, "sim/flightmodel/controls/flaprat");
    snapshot.slatRatio = readFloat(slatRatio_, "sim/flightmodel/controls/slatrat");
    if (totalMass_) snapshot.validityMask |= engine::ValidMassAndConfiguration;

    auto& atmosphere = snapshot.atmosphere;
    atmosphere.temperatureK = readFloat(temperature_, "sim/weather/aircraft/temperature_ambient_deg_c") + kKelvinOffset;
    atmosphere.staticPressurePa = readFloat(pressure_, "sim/weather/aircraft/barometer_current_pas");
    atmosphere.densityKgM3 = readFloat(density_, "sim/weather/rho");
    atmosphere.speedOfSoundMps = readFloat(speedOfSound_, "sim/weather/aircraft/speed_sound_ms");
    atmosphere.windLocalMps.x = readFloat(windX_, "sim/weather/aircraft/wind_now_x_msc");
    atmosphere.windLocalMps.y = readFloat(windY_, "sim/weather/aircraft/wind_now_y_msc");
    atmosphere.windLocalMps.z = readFloat(windZ_, "sim/weather/aircraft/wind_now_z_msc");
    atmosphere.thermalVerticalRateMps = readFloat(thermalRate_, "sim/weather/aircraft/thermal_rate_ms");
    atmosphere.precipitationRatio = readFloat(precipitation_, "sim/weather/aircraft/precipitation_on_aircraft_ratio");
    atmosphere.snowRatio = readFloat(snow_, "sim/weather/aircraft/snow_on_aircraft_ratio");
    atmosphere.hailRatio = readFloat(hail_, "sim/weather/aircraft/hail_on_aircraft_ratio");
    atmosphere.gravityMps2 = readFloat(gravity_, "sim/weather/aircraft/gravity_mss", 9.80665f);
    snapshot.validityMask |= engine::ValidAtmosphereAtAircraft;

    auto& profile = atmosphere.profile;
    std::array<float, engine::kMaximumAtmosphereLevels> rawTemperatureC {};
    std::array<float, engine::kMaximumAtmosphereLevels> rawDewPointC {};
    const int temperatureAltitudeCount = readFloatArray(
        temperatureLevels_, "sim/weather/region/temperature_altitude_msl_m",
        profile.temperatureAltitudeMslM.data(), static_cast<int>(profile.temperatureAltitudeMslM.size()));
    const int temperatureValueCount = readFloatArray(
        XPLMFindDataRef("sim/weather/region/temperatures_aloft_deg_c"),
        "sim/weather/region/temperatures_aloft_deg_c",
        rawTemperatureC.data(), static_cast<int>(rawTemperatureC.size()));
    const int dewAltitudeCount = readFloatArray(
        atmosphereLevels_, "sim/weather/region/atmosphere_alt_levels_m",
        profile.dewPointAltitudeMslM.data(), static_cast<int>(profile.dewPointAltitudeMslM.size()));
    const int dewValueCount = readFloatArray(
        dewPointLevels_, "sim/weather/region/dewpoint_deg_c",
        rawDewPointC.data(), static_cast<int>(rawDewPointC.size()));

    profile.temperatureLevelCount = boundedCount(
        std::min(temperatureAltitudeCount, temperatureValueCount), profile.temperatureK.size());
    profile.dewPointLevelCount = boundedCount(
        std::min(dewAltitudeCount, dewValueCount), profile.dewPointK.size());
    for (std::size_t index = 0; index < profile.temperatureLevelCount; ++index) {
        profile.temperatureK[index] = rawTemperatureC[index] + kKelvinOffset;
    }
    for (std::size_t index = 0; index < profile.dewPointLevelCount; ++index) {
        profile.dewPointK[index] = rawDewPointC[index] + kKelvinOffset;
    }

    const int windAltitudeCount = readFloatArray(
        windAltitudeLevels_, "sim/weather/region/wind_altitude_msl_m",
        profile.windAltitudeMslM.data(), static_cast<int>(profile.windAltitudeMslM.size()));
    const int windSpeedCount = readFloatArray(
        windSpeedLevels_, "sim/weather/region/wind_speed_msc",
        profile.windSpeedMps.data(), static_cast<int>(profile.windSpeedMps.size()));
    const int windDirectionCount = readFloatArray(
        windDirectionLevels_, "sim/weather/region/wind_direction_degt",
        profile.windDirectionDegTrue.data(), static_cast<int>(profile.windDirectionDegTrue.size()));
    const int shearSpeedCount = readFloatArray(
        shearSpeedLevels_, "sim/weather/region/shear_speed_msc",
        profile.shearSpeedMps.data(), static_cast<int>(profile.shearSpeedMps.size()));
    const int shearDirectionCount = readFloatArray(
        shearDirectionLevels_, "sim/weather/region/shear_direction_degt",
        profile.shearDirectionDegTrue.data(), static_cast<int>(profile.shearDirectionDegTrue.size()));
    const int turbulenceCount = readFloatArray(
        turbulenceLevels_, "sim/weather/region/turbulence",
        profile.turbulenceScale.data(), static_cast<int>(profile.turbulenceScale.size()));
    profile.windLevelCount = boundedCount(
        std::min({windAltitudeCount, windSpeedCount, windDirectionCount,
                  shearSpeedCount, shearDirectionCount, turbulenceCount}),
        profile.windSpeedMps.size());
    if (profile.temperatureLevelCount > 0 || profile.dewPointLevelCount > 0 || profile.windLevelCount > 0) {
        snapshot.validityMask |= engine::ValidAtmosphereProfile;
    }

    const int requestedEngineCount = readInt(engineCount_, "sim/aircraft/engine/acf_num_engines");
    snapshot.engineCount = boundedCount(requestedEngineCount, snapshot.engines.size());
    std::array<int, engine::kMaximumRecordedEngines> running {};
    std::array<float, engine::kMaximumRecordedEngines> n1 {};
    std::array<float, engine::kMaximumRecordedEngines> n2 {};
    std::array<float, engine::kMaximumRecordedEngines> fuelFlow {};
    std::array<float, engine::kMaximumRecordedEngines> thrust {};
    std::array<float, engine::kMaximumRecordedEngines> throttle {};
    std::array<float, engine::kMaximumRecordedEngines> egtC {};
    std::array<float, engine::kMaximumRecordedEngines> ittC {};
    std::array<float, engine::kMaximumRecordedEngines> jetwash {};
    std::array<float, engine::kMaximumRecordedEngines> exhaustVelocity {};
    const int maximumEngines = static_cast<int>(snapshot.engines.size());
    readIntArray(engineRunning_, "sim/flightmodel/engine/ENGN_running", running.data(), maximumEngines);
    readFloatArray(engineN1_, "sim/flightmodel/engine/ENGN_N1_", n1.data(), maximumEngines);
    readFloatArray(engineN2_, "sim/flightmodel/engine/ENGN_N2_", n2.data(), maximumEngines);
    readFloatArray(engineFuelFlow_, "sim/flightmodel/engine/ENGN_FF_", fuelFlow.data(), maximumEngines);
    readFloatArray(engineThrust_, "sim/flightmodel/engine/POINT_thrust", thrust.data(), maximumEngines);
    readFloatArray(engineThrottle_, "sim/flightmodel/engine/ENGN_thro_use", throttle.data(), maximumEngines);
    readFloatArray(engineEgt_, "sim/flightmodel2/engines/EGT_deg_cel", egtC.data(), maximumEngines);
    readFloatArray(engineItt_, "sim/flightmodel2/engines/ITT_deg_cel", ittC.data(), maximumEngines);
    readFloatArray(engineJetwash_, "sim/flightmodel2/engines/jetwash_mtr_sec", jetwash.data(), maximumEngines);
    readFloatArray(engineExhaustVelocity_, "sim/flightmodel2/engines/engn_exhaust_speed_msc", exhaustVelocity.data(), maximumEngines);
    for (std::size_t index = 0; index < snapshot.engineCount; ++index) {
        auto& engine = snapshot.engines[index];
        engine.running = running[index] != 0 ? 1u : 0u;
        engine.n1Percent = n1[index];
        engine.n2Percent = n2[index];
        engine.fuelFlowKgps = fuelFlow[index];
        engine.thrustN = thrust[index];
        engine.throttleRatio = throttle[index];
        engine.exhaustGasTemperatureK = egtC[index] + kKelvinOffset;
        engine.interTurbineTemperatureK = ittC[index] + kKelvinOffset;
        engine.jetwashVelocityMps = jetwash[index];
        engine.exhaustVelocityMps = exhaustVelocity[index];
    }
    if (snapshot.engineCount > 0 && engineRunning_) snapshot.validityMask |= engine::ValidPropulsion;

    engine::copyTruncated(snapshot.aircraftName, cachedAircraftName_);
    engine::copyTruncated(snapshot.aircraftIcao, cachedAircraftIcao_);
    engine::copyTruncated(snapshot.aircraftRelativePath, cachedAircraftRelativePath_);
    if (!cachedAircraftRelativePath_.empty()) snapshot.validityMask |= engine::ValidAircraftIdentity;
    return snapshot;
}

std::string XPlaneSnapshotSource::aircraftName() const { return cachedAircraftName_; }
std::string XPlaneSnapshotSource::aircraftIcao() const { return cachedAircraftIcao_; }
std::string XPlaneSnapshotSource::aircraftRelativePath() const { return cachedAircraftRelativePath_; }

std::string XPlaneSnapshotSource::xplaneVersionString() const {
    int xplaneVersion = 0;
    int xplmVersion = 0;
    XPLMHostApplicationID hostId = xplm_Host_Unknown;
    XPLMGetVersions(&xplaneVersion, &xplmVersion, &hostId);
    std::ostringstream stream;
    stream << xplaneVersion << " (XPLM " << xplmVersion << ", host " << static_cast<int>(hostId) << ')';
    return stream.str();
}

bool XPlaneSnapshotSource::writeValidationReport(const std::filesystem::path& outputPath,
                                                 std::string* error) const {
    std::error_code directoryError;
    if (!outputPath.parent_path().empty()) {
        std::filesystem::create_directories(outputPath.parent_path(), directoryError);
        if (directoryError) {
            if (error) *error = directoryError.message();
            return false;
        }
    }
    std::ofstream stream(outputPath, std::ios::trunc);
    if (!stream) {
        if (error) *error = "Could not open DataRef validation report";
        return false;
    }
    stream << "FFAtmo DataRef Validation Report\n";
    stream << "status=" << (ready_ ? "READY" : "MISSING_REQUIRED_DATAREFS") << '\n';
    stream << "xplane_version=" << xplaneVersionString() << '\n';
    stream << "aircraft_name=" << cachedAircraftName_ << '\n';
    stream << "aircraft_icao=" << cachedAircraftIcao_ << '\n';
    stream << "aircraft_relative_path=" << cachedAircraftRelativePath_ << '\n';
    stream << "dataref_count=" << statistics_.size() << '\n';
    stream << std::setprecision(12);
    for (const auto& statistic : statistics_) {
        stream << "\n[dataref]\n";
        stream << "name=" << statistic.name << '\n';
        stream << "resolved=" << (statistic.resolved ? 1 : 0) << '\n';
        stream << "runtime_type_mask=" << statistic.runtimeTypeMask << '\n';
        stream << "array_length=" << statistic.arrayLength << '\n';
        stream << "sample_count=" << statistic.sampleCount << '\n';
        stream << "non_finite_count=" << statistic.nonFiniteCount << '\n';
        stream << "unchanged_count=" << statistic.unchangedCount << '\n';
        if (statistic.hasValue) {
            stream << "minimum=" << statistic.minimum << '\n';
            stream << "maximum=" << statistic.maximum << '\n';
            stream << "last_value=" << statistic.lastValue << '\n';
        }
    }
    if (!stream) {
        if (error) *error = "Could not write DataRef validation report";
        return false;
    }
    return true;
}

}  // namespace ffatmo::host
