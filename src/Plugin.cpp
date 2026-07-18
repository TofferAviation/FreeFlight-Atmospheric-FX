#include "AircraftProfile.h"
#include "AtmosphereModel.h"
#include "CompanionBridge.h"
#include "ParticleInstance.h"
#include "PublishedDatarefs.h"
#include "Settings.h"
#include "SimDatarefs.h"

#include "XPLMDefs.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace ffatmo {
namespace {

class PluginRuntime {
public:
    bool start() {
        rootPath_ = findPluginRoot();
        settingsPath_ = findSettingsPath();
        statusPath_ = settingsPath_.parent_path() / "FFAtmo.status.ini";
        std::string settingsMessage;
        SettingsStore::load(settingsPath_.string(), settings_, &settingsMessage);
        if (std::filesystem::exists(settingsPath_)) {
            lastSettingsWrite_ = std::filesystem::last_write_time(settingsPath_);
        }

        if (!published_.registerAll(settings_)) {
            log("Could not register one or more custom datarefs.\n");
            return false;
        }
        published_.publish(state_, settings_);
        createCommandsAndMenu();
        log("Started. Standalone assets: " + rootPath_.string() + "\n");
        return true;
    }

    bool enable() {
        if (!sim_.resolve()) {
            log("Required aircraft position/weather datarefs are unavailable.\n");
            return false;
        }
        aircraftDirty_ = true;
        XPLMRegisterFlightLoopCallback(flightLoopCallback, -1.0f, this);
        return true;
    }

    void disable() {
        XPLMUnregisterFlightLoopCallback(flightLoopCallback, this);
        particles_.unload();
        model_.reset();
    }

    void stop() {
        SettingsStore::save(settingsPath_.string(), settings_);
        CompanionStatus status;
        status.pluginRunning = false;
        CompanionBridge::saveStatus(statusPath_.string(), status);
        destroyCommandsAndMenu();
        published_.unregisterAll();
    }

    void aircraftChanged() {
        aircraftDirty_ = true;
    }

private:
    static void log(const std::string& text) {
        XPLMDebugString(("[FFAtmo] " + text).c_str());
    }

    static std::filesystem::path findPluginRoot() {
        char pluginPath[2048] {};
        XPLMGetPluginInfo(XPLMGetMyID(), nullptr, pluginPath, nullptr, nullptr);
        std::filesystem::path binary(pluginPath);
        return binary.parent_path().parent_path();
    }

    static std::filesystem::path findSettingsPath() {
        char prefsPath[2048] {};
        XPLMGetPrefsPath(prefsPath);
        return std::filesystem::path(prefsPath).parent_path() / "FFAtmo.ini";
    }

    void refreshAircraft() {
        aircraftDirty_ = false;
        const std::string path = sim_.aircraftPath();
        if (path == aircraftPath_ && profile_) return;

        aircraftPath_ = path;
        profile_ = detectProfile(aircraftPath_);
        particles_.unload();
        model_.reset();

        if (!profile_) {
            log("No supported profile for aircraft: " + aircraftPath_ + "\n");
            return;
        }

        const std::filesystem::path objectPath = rootPath_ / "assets" / "FFAtmo_Lineage.obj";
        std::string error;
        if (!particles_.load(objectPath.string(), &error)) {
            log(error + "\n");
            return;
        }
        log("Loaded aircraft profile: " + profile_->displayName + "\n");
    }

    void pollCompanionSettings(float dt) {
        settingsPollSeconds_ += dt;
        if (settingsPollSeconds_ < 0.25f) return;
        settingsPollSeconds_ = 0.0f;
        std::error_code error;
        if (!std::filesystem::exists(settingsPath_, error)) return;
        const auto writeTime = std::filesystem::last_write_time(settingsPath_, error);
        if (error || writeTime == lastSettingsWrite_) return;
        lastSettingsWrite_ = writeTime;
        EffectSettings incoming = settings_;
        std::string message;
        if (SettingsStore::load(settingsPath_.string(), incoming, &message)) {
            settings_ = incoming;
            log("Applied live settings from FFAtmo Companion.\n");
        }
    }

    void publishCompanionStatus(float dt) {
        statusPublishSeconds_ += dt;
        if (statusPublishSeconds_ < 0.50f) return;
        statusPublishSeconds_ = 0.0f;
        CompanionStatus status;
        status.pluginRunning = true;
        status.particleObjectLoaded = particles_.loaded();
        status.aircraftPath = aircraftPath_;
        status.profileName = profile_ ? profile_->displayName : std::string();
        status.settings = settings_;
        status.input = lastInput_;
        status.state = state_;
        CompanionBridge::saveStatus(statusPath_.string(), status);
    }

    float update(float elapsedSinceLastCall) {
        published_.pullWritableControls(settings_);
        if (aircraftDirty_) refreshAircraft();

        const float dt = std::clamp(elapsedSinceLastCall, 0.001f, 0.25f);
        pollCompanionSettings(dt);
        const AtmosphereInput input = sim_.readAtmosphere();
        lastInput_ = input;
        state_ = model_.update(input, settings_, dt);
        published_.publish(state_, settings_);
        publishCompanionStatus(dt);

        if (profile_ && particles_.loaded()) {
            particles_.update(sim_.readPose(), published_.instanceValues());
        }
        return -1.0f;
    }

    static float flightLoopCallback(float elapsedSinceLastCall,
                                    float,
                                    int,
                                    void* refcon) {
        return static_cast<PluginRuntime*>(refcon)->update(elapsedSinceLastCall);
    }

    static int commandHandler(XPLMCommandRef command, XPLMCommandPhase phase, void* refcon) {
        if (phase != xplm_CommandBegin) return 1;
        PluginRuntime* self = static_cast<PluginRuntime*>(refcon);
        if (command == self->toggleEnabledCommand_) {
            self->settings_.enabled = !self->settings_.enabled;
            log(std::string("Effects ") + (self->settings_.enabled ? "enabled.\n" : "disabled.\n"));
        } else if (command == self->togglePreviewCommand_) {
            self->settings_.previewMode = !self->settings_.previewMode;
            if (self->settings_.previewMode) self->settings_.enabled = true;
            log(std::string("Preview Mode ") + (self->settings_.previewMode ? "enabled.\n" : "disabled.\n"));
        } else if (command == self->reloadProfileCommand_) {
            self->aircraftDirty_ = true;
        } else if (command == self->saveSettingsCommand_) {
            SettingsStore::save(self->settingsPath_.string(), self->settings_);
        }
        return 1;
    }

    void createCommandsAndMenu() {
        toggleEnabledCommand_ = XPLMCreateCommand("ffatmo/toggle_enabled", "Toggle FFAtmospherics");
        togglePreviewCommand_ = XPLMCreateCommand("ffatmo/toggle_preview", "Toggle FFAtmospherics Preview Mode");
        reloadProfileCommand_ = XPLMCreateCommand("ffatmo/reload_profile", "Reload FFAtmospherics aircraft profile");
        saveSettingsCommand_ = XPLMCreateCommand("ffatmo/save_settings", "Save FFAtmospherics settings");
        for (XPLMCommandRef command : {toggleEnabledCommand_, togglePreviewCommand_, reloadProfileCommand_, saveSettingsCommand_}) {
            XPLMRegisterCommandHandler(command, commandHandler, 1, this);
        }

        const int parentItem = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "FFAtmospherics", nullptr, 0);
        menu_ = XPLMCreateMenu("FFAtmospherics", XPLMFindPluginsMenu(), parentItem, nullptr, nullptr);
        XPLMAppendMenuItemWithCommand(menu_, "Enable / Disable", toggleEnabledCommand_);
        XPLMAppendMenuItemWithCommand(menu_, "Preview Mode", togglePreviewCommand_);
        XPLMAppendMenuSeparator(menu_);
        XPLMAppendMenuItemWithCommand(menu_, "Reload Aircraft Profile", reloadProfileCommand_);
        XPLMAppendMenuItemWithCommand(menu_, "Save Settings", saveSettingsCommand_);
    }

    void destroyCommandsAndMenu() {
        if (menu_) {
            XPLMDestroyMenu(menu_);
            menu_ = nullptr;
        }
        for (XPLMCommandRef command : {toggleEnabledCommand_, togglePreviewCommand_, reloadProfileCommand_, saveSettingsCommand_}) {
            if (command) XPLMUnregisterCommandHandler(command, commandHandler, 1, this);
        }
    }

    EffectSettings settings_ {};
    EffectState state_ {};
    AtmosphereInput lastInput_ {};
    AtmosphereModel model_ {};
    SimDatarefs sim_ {};
    PublishedDatarefs published_ {};
    ParticleInstance particles_ {};
    const AircraftProfile* profile_ = nullptr;
    std::filesystem::path rootPath_;
    std::filesystem::path settingsPath_;
    std::filesystem::path statusPath_;
    std::filesystem::file_time_type lastSettingsWrite_ {};
    std::string aircraftPath_;
    bool aircraftDirty_ = false;
    float settingsPollSeconds_ = 0.0f;
    float statusPublishSeconds_ = 0.0f;

    XPLMMenuID menu_ = nullptr;
    XPLMCommandRef toggleEnabledCommand_ = nullptr;
    XPLMCommandRef togglePreviewCommand_ = nullptr;
    XPLMCommandRef reloadProfileCommand_ = nullptr;
    XPLMCommandRef saveSettingsCommand_ = nullptr;
};

PluginRuntime gRuntime;

}  // namespace
}  // namespace ffatmo

PLUGIN_API int XPluginStart(char* outName, char* outSignature, char* outDescription) {
    std::snprintf(outName, 256, "%s", "FFAtmospherics");
    std::snprintf(outSignature, 256, "%s", "com.freeflight.ffatmo");
    std::snprintf(outDescription, 256, "%s", "Standalone condensation and contrail wake-lifecycle engine");
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

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, int message, void* parameter) {
    if (from == XPLM_PLUGIN_XPLANE && message == XPLM_MSG_PLANE_LOADED &&
        reinterpret_cast<intptr_t>(parameter) == 0) {
        ffatmo::gRuntime.aircraftChanged();
    }
}
