#include "AircraftProfile.h"
#include "AtmosphereModel.h"
#include "CompanionBridge.h"
#include "Settings.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

ffatmo::AtmosphereInput cruiseInput() {
    ffatmo::AtmosphereInput input;
    input.ambientTemperatureC = -56.0f;
    input.dewPointC = -57.0f;
    input.hasDewPoint = true;
    input.pressurePa = 23800.0f;
    input.altitudeFt = 35000.0f;
    input.trueAirspeedMps = 235.0f;
    input.mach = 0.78f;
    input.n1Percent = {86.0f, 86.0f};
    input.engineBurning = {true, true};
    return input;
}

}  // namespace

int main() {
    using namespace ffatmo;

    require(AtmosphereModel::saturationVapourPressureWaterHpa(0.0f) > 6.0f,
            "water saturation pressure near freezing");
    require(AtmosphereModel::saturationVapourPressureIceHpa(-40.0f) > 0.1f,
            "ice saturation pressure at cruise temperature");

    AtmosphereModel model;
    EffectSettings settings;
    AtmosphereInput persistent = cruiseInput();
    EffectState state = model.update(persistent, settings, 1.0f);
    require(state.formationProbability > 0.65f, "cold humid cruise forms contrails");
    require(state.contrailPersistenceSeconds >= 100.0f, "ice-supersaturated cruise persists");
    require(state.estimatedTrailLengthKm >= 45.0f && state.estimatedTrailLengthKm <= 75.0f,
            "persistent cruise trail is in the 45-75 km design range");
    require(state.primaryWakeRatio > 0.55f, "young contrail is captured by the primary wake");
    require(state.secondaryCurtainRatio > 0.45f, "persistent ice forms a secondary curtain");
    require(state.contrailCirrusRatio > 0.60f, "persistent wake transitions toward contrail cirrus");
    require(state.wakeVortexLifetimeSeconds >= 90.0f,
            "humid cruise sustains a visible vortex phase");
    require(state.vortexPhase >= 0.0f && state.vortexPhase < 1.0f,
            "vortex emission phase remains normalized");

    model.reset();
    AtmosphereInput dry = cruiseInput();
    dry.dewPointC = -75.0f;
    state = model.update(dry, settings, 1.0f);
    require(state.formationProbability < 0.35f, "very dry cruise suppresses contrails");
    require(state.contrailCirrusRatio < 0.10f, "very dry cruise cannot grow contrail cirrus");

    model.reset();
    AtmosphereInput humidApproach;
    humidApproach.ambientTemperatureC = 12.0f;
    humidApproach.dewPointC = 11.0f;
    humidApproach.hasDewPoint = true;
    humidApproach.trueAirspeedMps = 95.0f;
    humidApproach.angleOfAttackDeg = 7.5f;
    humidApproach.normalG = 1.2f;
    humidApproach.flapDeployRatio = 0.5f;
    state = model.update(humidApproach, settings, 1.0f);
    require(state.wingCondensationRatio > 0.25f, "humid high-lift condition creates wing vapour");

    model.reset();
    settings.previewMode = true;
    AtmosphereInput ground;
    state = model.update(ground, settings, 1.0f);
    require(state.engineContrailRatio[0] > 0.8f && state.wingCondensationRatio > 0.7f,
            "preview mode visibly overrides weather");
    require(state.primaryWakeRatio > 0.8f && state.secondaryCurtainRatio > 0.7f,
            "preview mode exposes the v0.3 wake lifecycle");

    require(detectProfile("Aircraft/X-Crafts/Lineage/Lineage.acf") != nullptr,
            "Lineage profile detection");
    require(detectProfile("Aircraft/Other/Cessna.acf") == nullptr,
            "unrelated aircraft is not matched");

    const std::string settingsPath = "ffatmo_test_settings.ini";
    require(SettingsStore::save(settingsPath, settings), "settings save");
    EffectSettings loaded;
    require(SettingsStore::load(settingsPath, loaded), "settings load");
    require(loaded.previewMode, "settings round trip");
    std::remove(settingsPath.c_str());

    const std::string statusPath = "ffatmo_test_status.ini";
    CompanionStatus savedStatus;
    savedStatus.pluginRunning = true;
    savedStatus.particleObjectLoaded = true;
    savedStatus.profileName = "X-Crafts Lineage 1000";
    savedStatus.settings = settings;
    savedStatus.input = persistent;
    savedStatus.state = state;
    require(CompanionBridge::saveStatus(statusPath, savedStatus), "companion status save");
    CompanionStatus loadedStatus;
    require(CompanionBridge::loadStatus(statusPath, loadedStatus), "companion status load");
    require(loadedStatus.pluginRunning && loadedStatus.particleObjectLoaded,
            "companion connection state round trip");
    require(loadedStatus.profileName == "X-Crafts Lineage 1000",
            "companion aircraft profile round trip");
    require(std::abs(loadedStatus.input.altitudeFt - persistent.altitudeFt) < 0.1f,
            "companion live flight input round trip");
    std::remove(statusPath.c_str());

    std::cout << "FFAtmo core tests passed\n";
    return 0;
}
