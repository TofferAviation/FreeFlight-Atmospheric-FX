#pragma once

#include "SimDatarefs.h"
#include "acf/AcfGeometry.h"

#include "XPLMDisplay.h"

#include <memory>

namespace ffatmo {

class GeometryDebugOverlay {
public:
    GeometryDebugOverlay() = default;
    ~GeometryDebugOverlay();

    bool start();
    void stop();

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    void toggle() { enabled_ = !enabled_; }

    void setPose(const AircraftPose& pose) { pose_ = pose; }
    void setProfile(std::shared_ptr<const acf::ParseResult> result) {
        result_ = std::move(result);
    }

private:
    static int drawCallback(XPLMDrawingPhase phase, int isBefore, void* refcon);
    int draw();

    XPLMDrawingPhase registeredPhase_ = xplm_Phase_Modern3D;
    int registeredBefore_ = 0;
    bool registered_ = false;
    bool enabled_ = true;
    AircraftPose pose_ {};
    std::shared_ptr<const acf::ParseResult> result_;
};

}  // namespace ffatmo
