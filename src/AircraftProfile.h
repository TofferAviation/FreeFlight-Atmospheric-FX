#pragma once

#include <array>
#include <string>
#include <vector>

namespace ffatmo {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct AircraftProfile {
    std::string id;
    std::string displayName;
    std::vector<std::string> acfPathTokens;
    std::array<Vec3, 2> engineExhaust;
    std::array<Vec3, 2> wingTips;
    std::array<Vec3, 2> wingCondensation;
};

const AircraftProfile& lineage1000Profile();
const AircraftProfile* detectProfile(const std::string& acfPath);

}  // namespace ffatmo
