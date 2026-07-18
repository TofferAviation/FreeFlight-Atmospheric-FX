#pragma once

#include "AircraftProfile.h"
#include "AtmosphereModel.h"

#include <string>

namespace ffatmo {

struct PanelContext {
    std::string aircraftPath;
    const AircraftProfile* profile = nullptr;
    bool particleObjectLoaded = false;
    const char* version = "0.4.0";
};

struct PanelResult {
    bool settingsChanged = false;
    bool saveRequested = false;
    bool profileReloadRequested = false;
};

PanelResult drawPanel(EffectSettings& settings,
                      const EffectState& state,
                      const PanelContext& context,
                      bool* open);

}  // namespace ffatmo
