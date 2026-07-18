#include "SimDatarefs.h"

#include "XPLMPlanes.h"

#include <algorithm>
#include <cmath>

namespace ffatmo {

XPLMDataRef SimDatarefs::findFirst(std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (XPLMDataRef ref = XPLMFindDataRef(name)) return ref;
    }
    return nullptr;
}

float SimDatarefs::scalar(XPLMDataRef ref, float fallback) {
    if (!ref) return fallback;
    const XPLMDataTypeID types = XPLMGetDataRefTypes(ref);
    if (types & xplmType_Float) return XPLMGetDataf(ref);
    if (types & xplmType_Double) return static_cast<float>(XPLMGetDatad(ref));
    if (types & xplmType_Int) return static_cast<float>(XPLMGetDatai(ref));
    return fallback;
}

double SimDatarefs::scalarDouble(XPLMDataRef ref, double fallback) {
    if (!ref) return fallback;
    const XPLMDataTypeID types = XPLMGetDataRefTypes(ref);
    if (types & xplmType_Double) return XPLMGetDatad(ref);
    if (types & xplmType_Float) return static_cast<double>(XPLMGetDataf(ref));
    return fallback;
}

float SimDatarefs::arrayValue(XPLMDataRef ref, int index, float fallback) {
    if (!ref) return fallback;
    float value = fallback;
    return XPLMGetDatavf(ref, &value, index, 1) == 1 ? value : fallback;
}

int SimDatarefs::intArrayValue(XPLMDataRef ref, int index, int fallback) {
    if (!ref) return fallback;
    int value = fallback;
    return XPLMGetDatavi(ref, &value, index, 1) == 1 ? value : fallback;
}

bool SimDatarefs::resolve() {
    // X-Plane 12 names first, followed by XP11-era replacements where useful.
    temperature_ = findFirst({"sim/weather/aircraft/temperature_ambient_deg_c",
                              "sim/weather/temperature_ambient_c"});
    dewPoint_ = findFirst({"sim/weather/aircraft/dewpoint_deg_c",
                           "sim/weather/dewpoi_sealevel_c"});
    relativeHumidity_ = findFirst({"sim/weather/aircraft/relative_humidity_percent",
                                   "sim/weather/relative_humidity_sealevel_percent"});
    pressure_ = findFirst({"sim/weather/aircraft/barometer_current_pas",
                           "sim/weather/barometer_current_inhg"});
    precipitation_ = findFirst({"sim/weather/aircraft/precipitation_on_aircraft_ratio",
                                "sim/weather/precipitation_on_aircraft_ratio"});
    turbulence_ = findFirst({"sim/weather/aircraft/turbulence_ratio",
                             "sim/weather/turbulence[0]"});
    elevation_ = XPLMFindDataRef("sim/flightmodel/position/elevation");
    trueAirspeed_ = XPLMFindDataRef("sim/flightmodel/position/true_airspeed");
    mach_ = XPLMFindDataRef("sim/flightmodel/misc/machno");
    angleOfAttack_ = XPLMFindDataRef("sim/flightmodel2/misc/AoA_angle_degrees");
    normalG_ = XPLMFindDataRef("sim/flightmodel2/misc/gforce_normal");
    flaps_ = findFirst({"sim/flightmodel2/controls/flap_handle_deploy_ratio",
                        "sim/cockpit2/controls/flap_ratio"});
    n1_ = XPLMFindDataRef("sim/cockpit2/engine/indicators/N1_percent");
    engineBurning_ = XPLMFindDataRef("sim/flightmodel2/engines/engine_is_burning_fuel");
    framePeriod_ = XPLMFindDataRef("sim/operation/misc/frame_rate_period");
    localX_ = XPLMFindDataRef("sim/flightmodel/position/local_x");
    localY_ = XPLMFindDataRef("sim/flightmodel/position/local_y");
    localZ_ = XPLMFindDataRef("sim/flightmodel/position/local_z");
    pitch_ = XPLMFindDataRef("sim/flightmodel/position/theta");
    heading_ = XPLMFindDataRef("sim/flightmodel/position/psi");
    roll_ = XPLMFindDataRef("sim/flightmodel/position/phi");

    ready_ = temperature_ && elevation_ && trueAirspeed_ && localX_ && localY_ && localZ_;
    return ready_;
}

AtmosphereInput SimDatarefs::readAtmosphere() const {
    AtmosphereInput input;
    input.ambientTemperatureC = scalar(temperature_, 15.0f);
    input.hasDewPoint = dewPoint_ != nullptr;
    input.dewPointC = scalar(dewPoint_, input.ambientTemperatureC - 10.0f);
    input.hasRelativeHumidity = relativeHumidity_ != nullptr;
    input.relativeHumidityWaterPercent = scalar(relativeHumidity_, 50.0f);
    input.altitudeFt = static_cast<float>(scalarDouble(elevation_) * 3.280839895f);
    input.trueAirspeedMps = scalar(trueAirspeed_);
    input.mach = scalar(mach_);
    input.angleOfAttackDeg = scalar(angleOfAttack_);
    input.normalG = scalar(normalG_, 1.0f);
    input.flapDeployRatio = scalar(flaps_);
    input.precipitationRatio = scalar(precipitation_);
    input.turbulenceRatio = scalar(turbulence_);

    float pressure = scalar(pressure_, 0.0f);
    if (pressure_ && pressure > 10.0f && pressure < 40.0f) pressure *= 3386.389f;  // inHg fallback.
    if (pressure < 1000.0f) {
        const float altitudeM = input.altitudeFt / 3.280839895f;
        pressure = 101325.0f * std::pow(std::max(1.0f - 2.25577e-5f * altitudeM, 0.05f), 5.25588f);
    }
    input.pressurePa = pressure;

    for (int engine = 0; engine < 2; ++engine) {
        input.n1Percent[engine] = arrayValue(n1_, engine);
        input.engineBurning[engine] = intArrayValue(engineBurning_, engine) != 0;
    }
    const float framePeriod = scalar(framePeriod_, 1.0f / 60.0f);
    input.frameRateFps = framePeriod > 0.0001f ? 1.0f / framePeriod : 60.0f;
    return input;
}

AircraftPose SimDatarefs::readPose() const {
    AircraftPose pose;
    pose.localX = scalarDouble(localX_);
    pose.localY = scalarDouble(localY_);
    pose.localZ = scalarDouble(localZ_);
    pose.pitchDeg = scalar(pitch_);
    pose.headingDeg = scalar(heading_);
    pose.rollDeg = scalar(roll_);
    return pose;
}

std::string SimDatarefs::aircraftPath() const {
    char fileName[512] {};
    char path[2048] {};
    XPLMGetNthAircraftModel(0, fileName, path);
    return path[0] ? std::string(path) : std::string(fileName);
}

}  // namespace ffatmo
