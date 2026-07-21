#include "GeometryDebugOverlay.h"
#include "acf/AcfGeometry.h"

#include "XPLMDataAccess.h"
#include "XPLMDefs.h"
#include "XPLMMenus.h"
#include "XPLMPlanes.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace ffatmo {
namespace {

class GeometryDebugRuntime {
public:
    bool start() {
        pluginRoot_ = findPluginRoot();
        reportPath_ = pluginRoot_ / "reports" / "geometry_validation.txt";
        profileService_ = std::make_unique<acf::AcfProfileService>();
        createCommandsAndMenu();
        log("Started. Load an aircraft, then use Plugins > FFAtmo Engine Geometry Debug.\n");
        return true;
    }

    bool enable() {
        if (!resolvePoseDatarefs()) {
            log("Required aircraft pose DataRefs are unavailable.\n");
            return false;
        }
        if (!overlay_.start()) {
            log("Could not register the geometry drawing callback.\n");
            return false;
        }
        overlay_.setEnabled(true);
        requestCurrentAircraft();
        XPLMRegisterFlightLoopCallback(flightLoopCallback, -1.0f, this);
        return true;
    }

    void disable() {
        XPLMUnregisterFlightLoopCallback(flightLoopCallback, this);
        overlay_.stop();
    }

    void stop() {
        disable();
        destroyCommandsAndMenu();
        profileService_.reset();
    }

    void aircraftChanged() {
        aircraftDirty_ = true;
    }

private:
    static void log(const std::string& text) {
        XPLMDebugString(("[FFAtmo Geometry Debug] " + text).c_str());
    }

    static std::filesystem::path findPluginRoot() {
        char pluginPath[2048] {};
        XPLMGetPluginInfo(XPLMGetMyID(), nullptr, pluginPath, nullptr, nullptr);
        const std::filesystem::path binary(pluginPath);
        return binary.parent_path().parent_path();
    }

    static std::filesystem::path xPlaneRoot() {
        char systemPath[2048] {};
        XPLMGetSystemPath(systemPath);
        return std::filesystem::path(systemPath);
    }

    bool resolvePoseDatarefs() {
        localX_ = XPLMFindDataRef("sim/flightmodel/position/local_x");
        localY_ = XPLMFindDataRef("sim/flightmodel/position/local_y");
        localZ_ = XPLMFindDataRef("sim/flightmodel/position/local_z");
        pitch_ = XPLMFindDataRef("sim/flightmodel/position/theta");
        heading_ = XPLMFindDataRef("sim/flightmodel/position/psi");
        roll_ = XPLMFindDataRef("sim/flightmodel/position/phi");
        return localX_ && localY_ && localZ_ && pitch_ && heading_ && roll_;
    }

    static double readDouble(XPLMDataRef ref) {
        if (!ref) return 0.0;
        const XPLMDataTypeID type = XPLMGetDataRefTypes(ref);
        if (type & xplmType_Double) return XPLMGetDatad(ref);
        if (type & xplmType_Float) return static_cast<double>(XPLMGetDataf(ref));
        if (type & xplmType_Int) return static_cast<double>(XPLMGetDatai(ref));
        return 0.0;
    }

    AircraftPose readPose() const {
        AircraftPose pose;
        pose.localX = readDouble(localX_);
        pose.localY = readDouble(localY_);
        pose.localZ = readDouble(localZ_);
        pose.pitchDeg = static_cast<float>(readDouble(pitch_));
        pose.headingDeg = static_cast<float>(readDouble(heading_));
        pose.rollDeg = static_cast<float>(readDouble(roll_));
        return pose;
    }

    std::filesystem::path currentAircraftPath() const {
        char fileName[512] {};
        char pathBuffer[2048] {};
        XPLMGetNthAircraftModel(0, fileName, pathBuffer);

        std::filesystem::path path =
            pathBuffer[0] ? std::filesystem::path(pathBuffer) : std::filesystem::path(fileName);
        if (path.is_relative()) path = xPlaneRoot() / path;

        std::error_code error;
        const auto canonical = std::filesystem::weakly_canonical(path, error);
        return error ? path.lexically_normal() : canonical;
    }

    void requestCurrentAircraft() {
        aircraftDirty_ = false;
        if (!profileService_) return;

        const auto path = currentAircraftPath();
        if (path.empty()) {
            log("X-Plane did not return an aircraft ACF path.\n");
            return;
        }

        activeAircraftPath_ = path;
        profileService_->request(path);
        expectedGeneration_ = profileService_->requestedGeneration();
        log("Parsing aircraft geometry: " + path.string() + "\n");
    }

    void pollCompletedProfile() {
        if (!profileService_ ||
            profileService_->completedGeneration() < expectedGeneration_ ||
            deliveredGeneration_ >= expectedGeneration_) {
            return;
        }

        const auto result = profileService_->latest();
        if (!result) return;

        deliveredGeneration_ = profileService_->completedGeneration();
        currentResult_ = result;
        overlay_.setProfile(result);

        if (result->ok) {
            log("Geometry ready for " + result->profile.aircraftName +
                ": " + std::to_string(result->profile.engines.size()) + " engines, " +
                std::to_string(result->profile.mainWingSegments.size()) +
                " main-wing segments. Overlay is ON.\n");
            XPLMSpeakString("FF Atmo geometry debug profile ready");
        } else {
            log("Geometry parsing failed. Use Export Validation Report and send the report with Log.txt.\n");
            XPLMSpeakString("FF Atmo geometry debug profile failed");
        }
    }

    void exportReport() {
        if (!currentResult_) {
            log("No completed ACF parse is available to export.\n");
            XPLMSpeakString("No geometry report is available yet");
            return;
        }

        std::string error;
        if (acf::writeValidationReport(*currentResult_, reportPath_, &error)) {
            log("Validation report written to: " + reportPath_.string() + "\n");
            XPLMSpeakString("FF Atmo geometry validation report exported");
        } else {
            log("Could not write validation report: " + error + "\n");
        }
    }

    float update() {
        if (aircraftDirty_) requestCurrentAircraft();
        overlay_.setPose(readPose());
        pollCompletedProfile();
        return -1.0f;
    }

    static float flightLoopCallback(float, float, int, void* refcon) {
        return static_cast<GeometryDebugRuntime*>(refcon)->update();
    }

    static int commandHandler(XPLMCommandRef command,
                              XPLMCommandPhase phase,
                              void* refcon) {
        if (phase != xplm_CommandBegin) return 1;
        auto* self = static_cast<GeometryDebugRuntime*>(refcon);

        if (command == self->toggleOverlayCommand_) {
            self->overlay_.toggle();
            log(std::string("Geometry overlay ") +
                (self->overlay_.enabled() ? "enabled.\n" : "disabled.\n"));
        } else if (command == self->reloadAcfCommand_) {
            self->requestCurrentAircraft();
        } else if (command == self->exportReportCommand_) {
            self->exportReport();
        }
        return 1;
    }

    void createCommandsAndMenu() {
        toggleOverlayCommand_ = XPLMCreateCommand(
            "ffatmo_debug/toggle_geometry_overlay",
            "Toggle FFAtmo parsed ACF geometry overlay");
        reloadAcfCommand_ = XPLMCreateCommand(
            "ffatmo_debug/reload_acf_geometry",
            "Reload and parse the current aircraft ACF");
        exportReportCommand_ = XPLMCreateCommand(
            "ffatmo_debug/export_geometry_report",
            "Export the FFAtmo ACF geometry validation report");

        for (XPLMCommandRef command :
             {toggleOverlayCommand_, reloadAcfCommand_, exportReportCommand_}) {
            XPLMRegisterCommandHandler(command, commandHandler, 1, this);
        }

        const int parentItem = XPLMAppendMenuItem(
            XPLMFindPluginsMenu(), "FFAtmo Engine Geometry Debug", nullptr, 0);
        menu_ = XPLMCreateMenu(
            "FFAtmo Engine Geometry Debug",
            XPLMFindPluginsMenu(),
            parentItem,
            nullptr,
            nullptr);
        XPLMAppendMenuItemWithCommand(
            menu_, "Geometry Overlay: ON / OFF", toggleOverlayCommand_);
        XPLMAppendMenuItemWithCommand(
            menu_, "Reload Current Aircraft ACF", reloadAcfCommand_);
        XPLMAppendMenuSeparator(menu_);
        XPLMAppendMenuItemWithCommand(
            menu_, "Export Validation Report", exportReportCommand_);
    }

    void destroyCommandsAndMenu() {
        if (menu_) {
            XPLMDestroyMenu(menu_);
            menu_ = nullptr;
        }
        for (XPLMCommandRef command :
             {toggleOverlayCommand_, reloadAcfCommand_, exportReportCommand_}) {
            if (command) {
                XPLMUnregisterCommandHandler(command, commandHandler, 1, this);
            }
        }
        toggleOverlayCommand_ = nullptr;
        reloadAcfCommand_ = nullptr;
        exportReportCommand_ = nullptr;
    }

    std::filesystem::path pluginRoot_;
    std::filesystem::path reportPath_;
    std::filesystem::path activeAircraftPath_;
    std::unique_ptr<acf::AcfProfileService> profileService_;
    std::shared_ptr<const acf::ParseResult> currentResult_;
    GeometryDebugOverlay overlay_;

    XPLMDataRef localX_ = nullptr;
    XPLMDataRef localY_ = nullptr;
    XPLMDataRef localZ_ = nullptr;
    XPLMDataRef pitch_ = nullptr;
    XPLMDataRef heading_ = nullptr;
    XPLMDataRef roll_ = nullptr;

    XPLMMenuID menu_ = nullptr;
    XPLMCommandRef toggleOverlayCommand_ = nullptr;
    XPLMCommandRef reloadAcfCommand_ = nullptr;
    XPLMCommandRef exportReportCommand_ = nullptr;

    bool aircraftDirty_ = false;
    std::uint64_t expectedGeneration_ = 0;
    std::uint64_t deliveredGeneration_ = 0;
};

GeometryDebugRuntime gRuntime;

}  // namespace
}  // namespace ffatmo

PLUGIN_API int XPluginStart(char* outName,
                            char* outSignature,
                            char* outDescription) {
    std::snprintf(outName, 256, "%s", "FFAtmo Engine Geometry Debug");
    std::snprintf(outSignature, 256, "%s", "com.freeflight.ffatmo.geometrydebug");
    std::snprintf(
        outDescription,
        256,
        "%s",
        "Parses the loaded ACF and visualises engines, exhaust axes, wing chains, and wingtips");
    return ffatmo::gRuntime.start() ? 1 : 0;
}

PLUGIN_API void XPluginStop() {
    ffatmo::gRuntime.stop();
}

PLUGIN_API int XPluginEnable() {
    return ffatmo::gRuntime.enable() ? 1 : 0;
}

PLUGIN_API void XPluginDisable() {
    ffatmo::gRuntime.disable();
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from,
                                      int message,
                                      void* parameter) {
    if (from == XPLM_PLUGIN_XPLANE &&
        message == XPLM_MSG_PLANE_LOADED &&
        reinterpret_cast<intptr_t>(parameter) == 0) {
        ffatmo::gRuntime.aircraftChanged();
    }
}
