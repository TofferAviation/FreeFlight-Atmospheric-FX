#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace ffatmo::engine {

constexpr std::uint32_t kSimulatorSnapshotSchemaVersion = 1;
constexpr std::size_t kMaximumRecordedEngines = 16;
constexpr std::size_t kMaximumAtmosphereLevels = 32;
constexpr std::size_t kAircraftNameBytes = 96;
constexpr std::size_t kAircraftIcaoBytes = 16;
constexpr std::size_t kAircraftPathBytes = 260;

enum SnapshotValidity : std::uint64_t {
    ValidTime = 1ull << 0,
    ValidGeodeticPosition = 1ull << 1,
    ValidLocalTransform = 1ull << 2,
    ValidLinearMotion = 1ull << 3,
    ValidAngularMotion = 1ull << 4,
    ValidAerodynamics = 1ull << 5,
    ValidAtmosphereAtAircraft = 1ull << 6,
    ValidAtmosphereProfile = 1ull << 7,
    ValidPropulsion = 1ull << 8,
    ValidMassAndConfiguration = 1ull << 9,
    ValidAircraftIdentity = 1ull << 10
};

enum SnapshotLifecycleFlag : std::uint32_t {
    LifecycleNone = 0,
    LifecycleAircraftLoaded = 1u << 0,
    LifecycleAircraftChanged = 1u << 1,
    LifecycleManualMarker = 1u << 2,
    LifecycleRecorderStarted = 1u << 3,
    LifecycleRecorderStopping = 1u << 4
};

struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct EngineSnapshot {
    std::uint8_t running = 0;
    std::uint8_t reserved0 = 0;
    std::uint16_t reserved1 = 0;
    float n1Percent = 0.0f;
    float n2Percent = 0.0f;
    float fuelFlowKgps = 0.0f;
    float thrustN = 0.0f;
    float throttleRatio = 0.0f;
    float exhaustGasTemperatureK = 0.0f;
    float interTurbineTemperatureK = 0.0f;
    float jetwashVelocityMps = 0.0f;
    float exhaustVelocityMps = 0.0f;
};

struct AtmosphereProfileSnapshot {
    std::uint32_t temperatureLevelCount = 0;
    std::uint32_t dewPointLevelCount = 0;
    std::uint32_t windLevelCount = 0;
    std::uint32_t reserved = 0;
    std::array<float, kMaximumAtmosphereLevels> temperatureAltitudeMslM {};
    std::array<float, kMaximumAtmosphereLevels> temperatureK {};
    std::array<float, kMaximumAtmosphereLevels> dewPointAltitudeMslM {};
    std::array<float, kMaximumAtmosphereLevels> dewPointK {};
    std::array<float, kMaximumAtmosphereLevels> windAltitudeMslM {};
    std::array<float, kMaximumAtmosphereLevels> windSpeedMps {};
    std::array<float, kMaximumAtmosphereLevels> windDirectionDegTrue {};
    std::array<float, kMaximumAtmosphereLevels> shearSpeedMps {};
    std::array<float, kMaximumAtmosphereLevels> shearDirectionDegTrue {};
    std::array<float, kMaximumAtmosphereLevels> turbulenceScale {};
};

struct AtmosphereSnapshot {
    float temperatureK = 0.0f;
    float staticPressurePa = 0.0f;
    float densityKgM3 = 0.0f;
    float speedOfSoundMps = 0.0f;
    Vec3f windLocalMps {};
    float thermalVerticalRateMps = 0.0f;
    float precipitationRatio = 0.0f;
    float snowRatio = 0.0f;
    float hailRatio = 0.0f;
    float gravityMps2 = 0.0f;
    AtmosphereProfileSnapshot profile {};
};

struct SimulatorSnapshot {
    std::uint32_t schemaVersion = kSimulatorSnapshotSchemaVersion;
    std::uint32_t lifecycleFlags = LifecycleNone;
    std::uint64_t sequenceNumber = 0;
    std::uint64_t validityMask = 0;

    double simulatorUptimeSeconds = 0.0;
    double flightTimeSeconds = 0.0;
    double monotonicTimeSeconds = 0.0;
    float deltaTimeSeconds = 0.0f;
    float timeAcceleration = 1.0f;
    std::uint8_t paused = 0;
    std::uint8_t replaying = 0;
    std::uint16_t reservedTime = 0;

    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    double elevationMslM = 0.0;
    Vec3d localPositionM {};
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
    float headingDegTrue = 0.0f;

    Vec3f linearVelocityLocalMps {};
    Vec3f accelerationLocalMps2 {};
    Vec3f angularVelocityBodyRadps {};

    float trueAirspeedMps = 0.0f;
    float groundSpeedMps = 0.0f;
    float angleOfAttackDeg = 0.0f;
    float sideslipDeg = 0.0f;
    float heightAglM = 0.0f;
    float normalLoadFactorG = 0.0f;

    float totalMassKg = 0.0f;
    float totalFuelMassKg = 0.0f;
    float flapRatio = 0.0f;
    float slatRatio = 0.0f;

    AtmosphereSnapshot atmosphere {};

    std::uint32_t engineCount = 0;
    std::array<EngineSnapshot, kMaximumRecordedEngines> engines {};

    std::array<char, kAircraftNameBytes> aircraftName {};
    std::array<char, kAircraftIcaoBytes> aircraftIcao {};
    std::array<char, kAircraftPathBytes> aircraftRelativePath {};
};

template <std::size_t N>
inline void copyTruncated(std::array<char, N>& destination, std::string_view source) {
    destination.fill('\0');
    if constexpr (N > 0) {
        const std::size_t count = source.size() < (N - 1) ? source.size() : (N - 1);
        for (std::size_t index = 0; index < count; ++index) destination[index] = source[index];
    }
}

static_assert(std::is_trivially_copyable_v<SimulatorSnapshot>,
              "SimulatorSnapshot must remain safe for lock-free queue publication");

}  // namespace ffatmo::engine
