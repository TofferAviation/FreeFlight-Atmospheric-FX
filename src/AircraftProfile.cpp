#include "AircraftProfile.h"

#include <algorithm>
#include <cctype>

namespace ffatmo {
namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

}  // namespace

const AircraftProfile& lineage1000Profile() {
    // Initial positions converted from the supplied Lineage.acf (feet -> metres).
    // The in-sim calibration page intentionally exposes XYZ correction before release.
    static const AircraftProfile profile {
        "xcrafts_lineage_1000",
        "X-Crafts Lineage 1000",
        {"lineage.acf", "lineage 1000", "lineage1000"},
        {{{-4.511f, -1.859f, 14.600f}, {4.511f, -1.859f, 14.600f}}},
        {{{-13.868f, 0.454f, 19.568f}, {13.868f, 0.454f, 19.568f}}},
        {{{-7.900f, -0.150f, 16.600f}, {7.900f, -0.150f, 16.600f}}},
    };
    return profile;
}

const AircraftProfile* detectProfile(const std::string& acfPath) {
    const std::string candidate = lower(acfPath);
    const AircraftProfile& profile = lineage1000Profile();
    for (const std::string& token : profile.acfPathTokens) {
        if (candidate.find(lower(token)) != std::string::npos) {
            return &profile;
        }
    }
    return nullptr;
}

}  // namespace ffatmo
