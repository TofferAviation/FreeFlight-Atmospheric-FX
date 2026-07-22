#pragma once

#include "engine/SimulatorSnapshot.h"

#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ffatmo {

struct ContrailDebugRenderParcel {
    engine::Vec3d localPositionM {};
    std::uint32_t engineIndex = 0;
    float radiusM = 0.0f;
    float opticalDepth = 0.0f;
    float normalizedIceMass = 0.0f;
    float ageSeconds = 0.0f;
};

struct ContrailDebugRenderSource {
    engine::Vec3d localPositionM {};
    std::uint32_t engineIndex = 0;
};

struct ContrailDebugOverlayStatus {
    std::string aircraftIcao;
    std::string mode;
    std::string geometryStatus;
    std::uint64_t activeParcels = 0;
    std::uint64_t emittedParcels = 0;
    std::uint64_t expiredParcels = 0;
    std::uint64_t peakParcels = 0;
    std::uint64_t originRebases = 0;
    float formationPotential = 0.0f;
    float relativeHumidityIcePercent = 0.0f;
    float temperatureK = 0.0f;
    bool physicsFrozen = false;
    bool simulationEnabled = true;
};

class ContrailDebugOverlay {
public:
    ~ContrailDebugOverlay();

    bool start();
    void stop();

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    void toggle() { enabled_ = !enabled_; }

    void setFrame(std::vector<ContrailDebugRenderParcel> parcels,
                  std::vector<ContrailDebugRenderSource> sources,
                  ContrailDebugOverlayStatus status);

private:
    static int drawCallback(XPLMDrawingPhase phase, int before, void* refcon);
    int draw();

    XPLMDataRef worldMatrix_ = nullptr;
    XPLMDataRef projectionMatrix_ = nullptr;
    XPLMDataRef windowWidth_ = nullptr;
    XPLMDataRef windowHeight_ = nullptr;
    XPLMDrawingPhase registeredPhase_ = xplm_Phase_Window;
    int registeredBefore_ = 0;
    bool registered_ = false;
    bool enabled_ = true;

    std::vector<ContrailDebugRenderParcel> parcels_;
    std::vector<ContrailDebugRenderSource> sources_;
    ContrailDebugOverlayStatus status_;
};

}  // namespace ffatmo
