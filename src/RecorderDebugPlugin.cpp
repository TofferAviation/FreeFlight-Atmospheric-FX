#include "diagnostics/ReplayFile.h"
#include "engine/SimulatorSnapshot.h"
#include "host/XPlaneSnapshotSource.h"

#include "XPLMDefs.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

#ifndef FFATMO_RECORDER_BUILD
#define FFATMO_RECORDER_BUILD "engine-v1-recorder-dev"
#endif

#ifndef FFATMO_GIT_REVISION
#define FFATMO_GIT_REVISION "unknown"
#endif

namespace ffatmo {
namespace {

class RecorderRuntime {
public:
    bool start() {
        pluginRoot_ = findPluginRoot();
        recordingsDirectory_ = pluginRoot_ / "recordings";
        reportsDirectory_ = pluginRoot_ / "reports";
        createCommandsAndMenu();
        log("Started. Use Plugins > FFAtmo Engine Recorder > Start Recording.\n");
        return true;
    }

    bool enable() {
        if (!source_.resolve()) {
            log("Required X-Plane snapshot DataRefs are unavailable.\n");
            source_.writeValidationReport(reportsDirectory_ / "dataref_validation.txt");
            return false;
        }
        pendingLifecycleFlags_ |= engine::LifecycleAircraftLoaded;
        XPLMRegisterFlightLoopCallback(flightLoopCallback, 0.05f, this);
        log("Snapshot source ready at 20 Hz. Recording is stopped until started from the menu.\n");
        return true;
    }

    void disable() {
        stopRecording();
        XPLMUnregisterFlightLoopCallback(flightLoopCallback, this);
    }

    void stop() {
        disable();
        destroyCommandsAndMenu();
    }

    void aircraftChanged() {
        pendingLifecycleFlags_ |= engine::LifecycleAircraftChanged;
    }

private:
    static void log(const std::string& text) {
        XPLMDebugString(("[FFAtmo Engine Recorder] " + text).c_str());
    }

    static std::filesystem::path findPluginRoot() {
        char pluginPath[2048] {};
        XPLMGetPluginInfo(XPLMGetMyID(), nullptr, pluginPath, nullptr, nullptr);
        return std::filesystem::path(pluginPath).parent_path().parent_path();
    }

    static std::string timestamp() {
        const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local {};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        std::ostringstream stream;
        stream << std::put_time(&local, "%Y%m%d-%H%M%S");
        return stream.str();
    }

    static std::string sanitized(std::string value) {
        if (value.empty()) return "AIRCRAFT";
        for (char& character : value) {
            const bool safe = (character >= 'A' && character <= 'Z') ||
                              (character >= 'a' && character <= 'z') ||
                              (character >= '0' && character <= '9') ||
                              character == '-' || character == '_';
            if (!safe) character = '_';
        }
        return value;
    }

    diagnostics::ReplayMetadata metadata() const {
        diagnostics::ReplayMetadata value;
        value.engineBuild = FFATMO_RECORDER_BUILD;
        value.gitRevision = FFATMO_GIT_REVISION;
        value.xplaneVersion = source_.xplaneVersionString();
#if defined(_WIN32)
        value.platform = "Windows x64";
#elif defined(__APPLE__)
        value.platform = "macOS";
#else
        value.platform = "Linux x64";
#endif
        value.aircraftName = source_.aircraftName();
        value.aircraftIcao = source_.aircraftIcao();
        value.aircraftRelativePath = source_.aircraftRelativePath();
        value.scenarioSeed = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        return value;
    }

    void startRecording() {
        if (recorder_.recording()) {
            log("Recording is already active.\n");
            XPLMSpeakString("FF Atmo recording is already active");
            return;
        }
        std::error_code directoryError;
        std::filesystem::create_directories(recordingsDirectory_, directoryError);
        std::filesystem::create_directories(reportsDirectory_, directoryError);

        currentStem_ = timestamp() + "-" + sanitized(source_.aircraftIcao());
        currentReplayPath_ = recordingsDirectory_ / (currentStem_ + ".ffar");
        std::string error;
        if (!recorder_.start(currentReplayPath_, metadata(), &error)) {
            log("Could not start recording: " + error + "\n");
            XPLMSpeakString("FF Atmo recording failed to start");
            return;
        }
        sequenceNumber_ = 0;
        pendingLifecycleFlags_ |= engine::LifecycleRecorderStarted;
        log("Recording started: " + currentReplayPath_.string() + "\n");
        XPLMSpeakString("FF Atmo snapshot recording started");
    }

    void stopRecording() {
        if (!recorder_.recording()) return;

        auto finalSnapshot = source_.capture(
            ++sequenceNumber_, 0.0f,
            pendingLifecycleFlags_ | engine::LifecycleRecorderStopping);
        pendingLifecycleFlags_ = engine::LifecycleNone;
        recorder_.tryPush(finalSnapshot);

        std::string error;
        const bool stoppedCleanly = recorder_.stop(&error);
        const auto validationPath = reportsDirectory_ / (currentStem_ + "-dataref_validation.txt");
        source_.writeValidationReport(validationPath, &error);

        const auto replay = diagnostics::readReplayFile(currentReplayPath_);
        const auto summaryPath = reportsDirectory_ / (currentStem_ + "-recording_summary.txt");
        diagnostics::writeReplaySummary(replay, summaryPath, &error);

        if (stoppedCleanly && replay.ok) {
            log("Recording finalized: " + currentReplayPath_.string() + "\n");
            log("Snapshots: " + std::to_string(replay.summary.snapshotCount) +
                ", dropped: " + std::to_string(replay.summary.droppedSnapshotCount) + "\n");
            log("Reports: " + validationPath.string() + " and " + summaryPath.string() + "\n");
            XPLMSpeakString("FF Atmo snapshot recording saved");
        } else {
            log("Recording finalization reported an error: " +
                (error.empty() ? replay.error : error) + "\n");
            XPLMSpeakString("FF Atmo recording finished with an error");
        }
    }

    void addMarker() {
        pendingLifecycleFlags_ |= engine::LifecycleManualMarker;
        log("Manual event marker queued for the next snapshot.\n");
        XPLMSpeakString("FF Atmo event marker added");
    }

    void exportReport() {
        std::filesystem::path path = reportsDirectory_ / "dataref_validation.txt";
        if (!currentStem_.empty()) path = reportsDirectory_ / (currentStem_ + "-dataref_validation.txt");
        std::string error;
        if (source_.writeValidationReport(path, &error)) {
            log("DataRef report written to: " + path.string() + "\n");
            XPLMSpeakString("FF Atmo Data Ref report exported");
        } else {
            log("Could not export DataRef report: " + error + "\n");
        }
    }

    float update(float elapsedSinceLastCall) {
        if (recorder_.recording()) {
            auto snapshot = source_.capture(
                ++sequenceNumber_, elapsedSinceLastCall, pendingLifecycleFlags_);
            pendingLifecycleFlags_ = engine::LifecycleNone;
            recorder_.tryPush(snapshot);
        }
        return 0.05f;
    }

    static float flightLoopCallback(float elapsedSinceLastCall,
                                    float,
                                    int,
                                    void* refcon) {
        return static_cast<RecorderRuntime*>(refcon)->update(elapsedSinceLastCall);
    }

    static int commandHandler(XPLMCommandRef command,
                              XPLMCommandPhase phase,
                              void* refcon) {
        if (phase != xplm_CommandBegin) return 1;
        auto* self = static_cast<RecorderRuntime*>(refcon);
        if (command == self->startRecordingCommand_) self->startRecording();
        else if (command == self->stopRecordingCommand_) self->stopRecording();
        else if (command == self->addMarkerCommand_) self->addMarker();
        else if (command == self->exportReportCommand_) self->exportReport();
        return 1;
    }

    void createCommandsAndMenu() {
        startRecordingCommand_ = XPLMCreateCommand(
            "ffatmo_recorder/start", "Start FFAtmo simulator snapshot recording");
        stopRecordingCommand_ = XPLMCreateCommand(
            "ffatmo_recorder/stop", "Stop and finalize FFAtmo simulator snapshot recording");
        addMarkerCommand_ = XPLMCreateCommand(
            "ffatmo_recorder/add_marker", "Add an event marker to the FFAtmo recording");
        exportReportCommand_ = XPLMCreateCommand(
            "ffatmo_recorder/export_dataref_report", "Export FFAtmo DataRef validation report");

        for (XPLMCommandRef command :
             {startRecordingCommand_, stopRecordingCommand_, addMarkerCommand_, exportReportCommand_}) {
            XPLMRegisterCommandHandler(command, commandHandler, 1, this);
        }

        const int parentItem = XPLMAppendMenuItem(
            XPLMFindPluginsMenu(), "FFAtmo Engine Recorder", nullptr, 0);
        menu_ = XPLMCreateMenu(
            "FFAtmo Engine Recorder", XPLMFindPluginsMenu(), parentItem, nullptr, nullptr);
        XPLMAppendMenuItemWithCommand(menu_, "Start Recording", startRecordingCommand_);
        XPLMAppendMenuItemWithCommand(menu_, "Stop and Finalize", stopRecordingCommand_);
        XPLMAppendMenuItemWithCommand(menu_, "Add Event Marker", addMarkerCommand_);
        XPLMAppendMenuSeparator(menu_);
        XPLMAppendMenuItemWithCommand(menu_, "Export DataRef Report", exportReportCommand_);
    }

    void destroyCommandsAndMenu() {
        if (menu_) {
            XPLMDestroyMenu(menu_);
            menu_ = nullptr;
        }
        for (XPLMCommandRef command :
             {startRecordingCommand_, stopRecordingCommand_, addMarkerCommand_, exportReportCommand_}) {
            if (command) XPLMUnregisterCommandHandler(command, commandHandler, 1, this);
        }
        startRecordingCommand_ = nullptr;
        stopRecordingCommand_ = nullptr;
        addMarkerCommand_ = nullptr;
        exportReportCommand_ = nullptr;
    }

    host::XPlaneSnapshotSource source_;
    diagnostics::ReplayRecorder recorder_;
    std::filesystem::path pluginRoot_;
    std::filesystem::path recordingsDirectory_;
    std::filesystem::path reportsDirectory_;
    std::filesystem::path currentReplayPath_;
    std::string currentStem_;
    std::uint64_t sequenceNumber_ = 0;
    std::uint32_t pendingLifecycleFlags_ = engine::LifecycleNone;

    XPLMMenuID menu_ = nullptr;
    XPLMCommandRef startRecordingCommand_ = nullptr;
    XPLMCommandRef stopRecordingCommand_ = nullptr;
    XPLMCommandRef addMarkerCommand_ = nullptr;
    XPLMCommandRef exportReportCommand_ = nullptr;
};

RecorderRuntime gRuntime;

}  // namespace
}  // namespace ffatmo

PLUGIN_API int XPluginStart(char* outName,
                            char* outSignature,
                            char* outDescription) {
    std::snprintf(outName, 256, "%s", "FFAtmo Engine Recorder");
    std::snprintf(outSignature, 256, "%s", "com.freeflight.ffatmo.recorder");
    std::snprintf(outDescription, 256, "%s",
                  "Records immutable X-Plane aircraft, atmosphere, weather-profile, and propulsion snapshots");
    return ffatmo::gRuntime.start() ? 1 : 0;
}

PLUGIN_API void XPluginStop() { ffatmo::gRuntime.stop(); }
PLUGIN_API int XPluginEnable() { return ffatmo::gRuntime.enable() ? 1 : 0; }
PLUGIN_API void XPluginDisable() { ffatmo::gRuntime.disable(); }

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from,
                                      int message,
                                      void* parameter) {
    if (from == XPLM_PLUGIN_XPLANE &&
        message == XPLM_MSG_PLANE_LOADED &&
        reinterpret_cast<intptr_t>(parameter) == 0) {
        ffatmo::gRuntime.aircraftChanged();
    }
}
