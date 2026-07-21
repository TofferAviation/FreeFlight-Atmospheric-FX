#pragma once

#include "SimDatarefs.h"
#include "acf/AcfGeometry.h"

#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"

#include <memory>

namespace ffatmo {

// Development-only visual validation of ACF-derived aircraft geometry in X-Plane.
//
// Geometry is projected into screen space during the 2-D window phase. This follows
// Laminar's coach-mark pattern and remains visible under X-Plane's Vulkan renderer.
class GeometryDebugOverlay {
public:
    GeometryDebugOverlay() = default;
    ~GeometryDebugOverlay();

    bool start();
    void stop();

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    void toggle() { enabled_ = !enabled_; }

    // Retained for the plugin-facing interface. The screen projection uses X-Plane's
    // aircraft matrix, which is authoritative for the current rendered view.
    void setPose(const AircraftPose& pose) { pose_ = pose; }
    void setProfile(std::shared_ptr<const acf::ParseResult> result) {
        result_ = std::move(result);
    }

private:
    static int drawCallback(XPLMDrawingPhase phase, int isBefore, void* refcon);
    int draw();

    XPLMDrawingPhase registeredPhase_ = xplm_Phase_Window;
    int registeredBefore_ = 0;
    bool registered_ = false;
    bool enabled_ = true;
    AircraftPose pose_ {};
    std::shared_ptr<const acf::ParseResult> result_;

    XPLMDataRef aircraftMatrix_ = nullptr;
    XPLMDataRef projectionMatrix_ = nullptr;
    XPLMDataRef windowWidth_ = nullptr;
    XPLMDataRef windowHeight_ = nullptr;
};

}  // namespace ffatmo
