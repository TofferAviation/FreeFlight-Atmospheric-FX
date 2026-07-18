#include "Panel.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace ffatmo {
namespace {

enum class Page {
    Overview,
    Effects,
    Weather,
    Performance,
    Profile,
    Advanced,
};

Page gPage = Page::Overview;

void statusDot(bool active, const char* activeText, const char* inactiveText) {
    const ImVec4 color = active ? ImVec4(0.34f, 0.82f, 0.59f, 1.0f) : ImVec4(0.47f, 0.54f, 0.65f, 1.0f);
    ImGui::TextColored(color, "%s  %s", active ? "●" : "○", active ? activeText : inactiveText);
}

void section(const char* title) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.90f, 0.72f, 0.34f, 1.0f), "%s", title);
    ImGui::Separator();
}

void metric(const char* label, const char* value) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(220.0f);
    ImGui::TextUnformatted(value);
}

bool navButton(Page page, const char* label) {
    const bool selected = gPage == page;
    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.30f, 0.52f, 1.0f));
    const bool clicked = ImGui::Button(label, ImVec2(-1.0f, 42.0f));
    if (selected) ImGui::PopStyleColor();
    if (clicked) gPage = page;
    return clicked;
}

void overviewPage(const EffectSettings& settings, const EffectState& state, const PanelContext& context) {
    ImGui::TextUnformatted("LIVE OVERVIEW");
    ImGui::TextDisabled("Real-time atmospheric and aircraft effect state");
    section("SYSTEM STATUS");
    statusDot(settings.enabled, "Effects enabled", "Effects disabled");
    statusDot(context.profile != nullptr, "Lineage profile detected", "Waiting for a supported aircraft");
    statusDot(context.particleObjectLoaded, "Particle object attached", "Particle object not loaded");
    if (settings.previewMode) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.90f, 0.72f, 0.34f, 1.0f), "PREVIEW MODE");
    }

    section("ATMOSPHERE");
    char value[64];
    std::snprintf(value, sizeof(value), "%.1f %%", state.relativeHumidityWaterPercent);
    metric("Relative humidity (water)", value);
    std::snprintf(value, sizeof(value), "%.1f %%", state.relativeHumidityIcePercent);
    metric("Relative humidity (ice)", value);
    std::snprintf(value, sizeof(value), "%.1f °C", state.criticalContrailTemperatureC);
    metric("Critical contrail temperature", value);
    std::snprintf(value, sizeof(value), "%.0f %%", state.formationProbability * 100.0f);
    metric("Formation probability", value);

    section("LIVE EFFECTS");
    ImGui::ProgressBar(state.engineContrailRatio[0], ImVec2(-1.0f, 18.0f), "Left engine contrail");
    ImGui::ProgressBar(state.engineContrailRatio[1], ImVec2(-1.0f, 18.0f), "Right engine contrail");
    ImGui::ProgressBar(state.primaryWakeRatio, ImVec2(-1.0f, 18.0f), "Primary vortex wake");
    ImGui::ProgressBar(state.secondaryCurtainRatio, ImVec2(-1.0f, 18.0f), "Secondary ice curtain");
    ImGui::ProgressBar(state.contrailCirrusRatio, ImVec2(-1.0f, 18.0f), "Contrail-cirrus transition");
    ImGui::ProgressBar(state.wingCondensationRatio, ImVec2(-1.0f, 18.0f), "Over-wing condensation");
    ImGui::ProgressBar(state.wingVortexRatio, ImVec2(-1.0f, 18.0f), "Wingtip vortices");
    std::snprintf(value, sizeof(value), "%.0f seconds / %.1f km", state.contrailPersistenceSeconds,
                  state.estimatedTrailLengthKm);
    metric("Estimated persistence", value);
}

bool effectsPage(EffectSettings& settings, const EffectState& state) {
    bool changed = false;
    ImGui::TextUnformatted("EFFECTS CONTROL");
    ImGui::TextDisabled("Every control updates the live particle data on the next frame");
    section("MASTER");
    changed |= ImGui::Checkbox("Enable FFAtmospherics", &settings.enabled);
    changed |= ImGui::Checkbox("Preview Mode (force visible effects)", &settings.previewMode);
    section("ENGINE CONTRAILS");
    changed |= ImGui::SliderFloat("Intensity##contrail", &settings.contrailIntensity, 0.0f, 2.0f, "%.0f %%", 0);
    changed |= ImGui::SliderFloat("Persistence scale", &settings.persistenceScale, 0.25f, 2.0f, "%.2fx");
    ImGui::ProgressBar(state.contrailSpreadRatio, ImVec2(-1.0f, 16.0f), "Current spread layer");
    section("WAKE LIFECYCLE");
    ImGui::ProgressBar(state.wakeCaptureRatio, ImVec2(-1.0f, 16.0f), "Exhaust capture");
    ImGui::ProgressBar(state.primaryWakeRatio, ImVec2(-1.0f, 16.0f), "Descending primary wake");
    ImGui::ProgressBar(state.secondaryCurtainRatio, ImVec2(-1.0f, 16.0f), "Secondary curtain");
    ImGui::ProgressBar(state.contrailCirrusRatio, ImVec2(-1.0f, 16.0f), "Delayed cirrus breakup");
    ImGui::TextDisabled("The two primary-wake layers use opposite corkscrew emission and slow descent before breaking into the curtain and cirrus populations.");
    section("WING EFFECTS");
    changed |= ImGui::SliderFloat("Over-wing vapour", &settings.wingCondensationIntensity, 0.0f, 2.0f, "%.2fx");
    changed |= ImGui::SliderFloat("Wingtip vortices", &settings.wingVortexIntensity, 0.0f, 2.0f, "%.2fx");
    return changed;
}

bool weatherPage(EffectSettings& settings, const EffectState& state) {
    bool changed = false;
    ImGui::TextUnformatted("WEATHER & REALISM");
    ImGui::TextDisabled("The physics engine reads weather; it never changes the simulator weather");
    section("FORMATION MODEL");
    changed |= ImGui::Checkbox("Automatic live-weather evaluation", &settings.automaticWeather);
    changed |= ImGui::SliderFloat("Minimum contrail altitude", &settings.minimumContrailAltitudeFt,
                                  0.0f, 40000.0f, "%.0f ft");
    ImGui::TextWrapped("Contrails require cold exhaust mixing plus sufficient humidity. Long persistence is reserved for ice-supersaturated air. Young ice is captured by the descending primary wake; surviving ice detrains into a secondary curtain and later spreads as contrail cirrus.");
    section("CURRENT DECISION");
    ImGui::ProgressBar(state.formationProbability, ImVec2(-1.0f, 20.0f), "Formation");
    ImGui::ProgressBar(state.persistenceProbability, ImVec2(-1.0f, 20.0f), "Persistence");
    return changed;
}

bool performancePage(EffectSettings& settings, const EffectState& state) {
    bool changed = false;
    ImGui::TextUnformatted("QUALITY & PERFORMANCE");
    ImGui::TextDisabled("Presets alter emission density and safe lifetime caps");
    section("QUALITY PRESET");
    int quality = static_cast<int>(settings.quality);
    const char* labels[] = {"Low", "Medium", "High", "Ultra"};
    if (ImGui::Combo("Preset", &quality, labels, 4)) {
        settings.quality = static_cast<QualityPreset>(quality);
        changed = true;
    }
    changed |= ImGui::Checkbox("Adaptive quality", &settings.adaptiveQuality);
    changed |= ImGui::SliderFloat("Target frame rate", &settings.adaptiveTargetFps, 20.0f, 90.0f, "%.0f FPS");
    section("LIVE BUDGET");
    ImGui::ProgressBar(std::clamp(state.particleBudgetRatio / 1.3f, 0.0f, 1.0f),
                       ImVec2(-1.0f, 20.0f), "Particle budget");
    ImGui::TextDisabled("Adaptive quality changes new-particle density only. Existing trails fade naturally.");
    return changed;
}

void profilePage(const PanelContext& context) {
    ImGui::TextUnformatted("AIRCRAFT PROFILE");
    ImGui::TextDisabled("Aircraft matching and emitter calibration");
    section("DETECTION");
    metric("Detected aircraft", context.profile ? context.profile->displayName.c_str() : "Unsupported / not loaded");
    ImGui::TextWrapped("%s", context.aircraftPath.empty() ? "No aircraft path available" : context.aircraftPath.c_str());
    if (!context.profile) return;
    section("LINEAGE EMITTERS");
    if (ImGui::BeginTable("emitters", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Emitter");
        ImGui::TableSetupColumn("X (m)");
        ImGui::TableSetupColumn("Y (m)");
        ImGui::TableSetupColumn("Z (m)");
        ImGui::TableHeadersRow();
        const std::array<const char*, 6> names {{"Engine L", "Engine R", "Wingtip L", "Wingtip R", "Wing vapour L", "Wing vapour R"}};
        const std::array<Vec3, 6> points {{context.profile->engineExhaust[0], context.profile->engineExhaust[1],
            context.profile->wingTips[0], context.profile->wingTips[1], context.profile->wingCondensation[0],
            context.profile->wingCondensation[1]}};
        for (int i = 0; i < 6; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(names[i]);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", points[i].x);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", points[i].y);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", points[i].z);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("XYZ fine calibration will unlock after the first in-sim alignment pass.");
}

PanelResult advancedPage(EffectSettings& settings, const EffectState& state) {
    PanelResult result;
    ImGui::TextUnformatted("ADVANCED & TEST");
    ImGui::TextDisabled("Diagnostics, forced effects, persistence, and profile reload");
    section("TEST CONTROLS");
    if (ImGui::Checkbox("Force Preview Mode", &settings.previewMode)) result.settingsChanged = true;
    if (ImGui::Button("Reload aircraft profile", ImVec2(220.0f, 36.0f))) result.profileReloadRequested = true;
    ImGui::SameLine();
    if (ImGui::Button("Save settings", ImVec2(160.0f, 36.0f))) result.saveRequested = true;
    section("LIVE DATAREF OUTPUT");
    ImGui::Text("ffatmo/contrail/core_ratio             %.3f", state.contrailCoreRatio);
    ImGui::Text("ffatmo/contrail/spread_ratio           %.3f", state.contrailSpreadRatio);
    ImGui::Text("ffatmo/contrail/persistence_seconds    %.1f", state.contrailPersistenceSeconds);
    ImGui::Text("ffatmo/wing/condensation_ratio         %.3f", state.wingCondensationRatio);
    ImGui::Text("ffatmo/wing/vortex_ratio               %.3f", state.wingVortexRatio);
    ImGui::Text("ffatmo/system/particle_budget_ratio    %.3f", state.particleBudgetRatio);
    ImGui::Text("ffatmo/wake/capture_ratio              %.3f", state.wakeCaptureRatio);
    ImGui::Text("ffatmo/wake/primary_ratio              %.3f", state.primaryWakeRatio);
    ImGui::Text("ffatmo/wake/secondary_curtain_ratio    %.3f", state.secondaryCurtainRatio);
    ImGui::Text("ffatmo/wake/contrail_cirrus_ratio      %.3f", state.contrailCirrusRatio);
    ImGui::Text("ffatmo/wake/vortex_lifetime_seconds    %.1f", state.wakeVortexLifetimeSeconds);
    ImGui::Text("ffatmo/wake/vortex_phase               %.3f", state.vortexPhase);
    return result;
}

}  // namespace

PanelResult drawPanel(EffectSettings& settings,
                      const EffectState& state,
                      const PanelContext& context,
                      bool* open) {
    PanelResult result;
    ImGui::SetNextWindowSize(ImVec2(980.0f, 660.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("FFAtmospherics Control Panel", open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return result;
    }

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.10f, 0.30f, 0.52f, 1.0f));
    ImGui::BeginChild("sidebar", ImVec2(215.0f, 0.0f), true);
    ImGui::TextColored(ImVec4(0.90f, 0.72f, 0.34f, 1.0f), "FREEFLIGHT");
    ImGui::TextUnformatted("FFAtmospherics");
    ImGui::TextDisabled("v%s", context.version);
    ImGui::Spacing();
    navButton(Page::Overview, "Live Overview");
    navButton(Page::Effects, "Effects Control");
    navButton(Page::Weather, "Weather & Realism");
    navButton(Page::Performance, "Quality & Performance");
    navButton(Page::Profile, "Aircraft Profile");
    navButton(Page::Advanced, "Advanced & Test");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("content", ImVec2(0.0f, 0.0f), true);

    switch (gPage) {
        case Page::Overview: overviewPage(settings, state, context); break;
        case Page::Effects: result.settingsChanged |= effectsPage(settings, state); break;
        case Page::Weather: result.settingsChanged |= weatherPage(settings, state); break;
        case Page::Performance: result.settingsChanged |= performancePage(settings, state); break;
        case Page::Profile: profilePage(context); break;
        case Page::Advanced: {
            const PanelResult advanced = advancedPage(settings, state);
            result.settingsChanged |= advanced.settingsChanged;
            result.saveRequested |= advanced.saveRequested;
            result.profileReloadRequested |= advanced.profileReloadRequested;
            break;
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
    return result;
}

}  // namespace ffatmo
