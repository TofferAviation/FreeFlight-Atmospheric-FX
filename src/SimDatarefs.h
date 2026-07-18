#pragma once

#include "AtmosphereModel.h"

#include "XPLMDataAccess.h"

#include <array>
#include <initializer_list>
#include <string>

namespace ffatmo {

struct AircraftPose {
    double localX = 0.0;
    double localY = 0.0;
    double localZ = 0.0;
    float pitchDeg = 0.0f;
    float headingDeg = 0.0f;
    float rollDeg = 0.0f;
};

class SimDatarefs {
public:
    bool resolve();
    AtmosphereInput readAtmosphere() const;
    AircraftPose readPose() const;
    std::string aircraftPath() const;
    bool ready() const { return ready_; }

private:
    static XPLMDataRef findFirst(std::initializer_list<const char*> names);
    static float scalar(XPLMDataRef ref, float fallback = 0.0f);
    static double scalarDouble(XPLMDataRef ref, double fallback = 0.0);
    static float arrayValue(XPLMDataRef ref, int index, float fallback = 0.0f);
    static int intArrayValue(XPLMDataRef ref, int index, int fallback = 0);

    XPLMDataRef temperature_ = nullptr;
    XPLMDataRef dewPoint_ = nullptr;
    XPLMDataRef relativeHumidity_ = nullptr;
    XPLMDataRef pressure_ = nullptr;
    XPLMDataRef precipitation_ = nullptr;
    XPLMDataRef turbulence_ = nullptr;
    XPLMDataRef elevation_ = nullptr;
    XPLMDataRef trueAirspeed_ = nullptr;
    XPLMDataRef mach_ = nullptr;
    XPLMDataRef angleOfAttack_ = nullptr;
    XPLMDataRef normalG_ = nullptr;
    XPLMDataRef flaps_ = nullptr;
    XPLMDataRef n1_ = nullptr;
    XPLMDataRef engineBurning_ = nullptr;
    XPLMDataRef framePeriod_ = nullptr;
    XPLMDataRef localX_ = nullptr;
    XPLMDataRef localY_ = nullptr;
    XPLMDataRef localZ_ = nullptr;
    XPLMDataRef pitch_ = nullptr;
    XPLMDataRef heading_ = nullptr;
    XPLMDataRef roll_ = nullptr;
    bool ready_ = false;
};

}  // namespace ffatmo
