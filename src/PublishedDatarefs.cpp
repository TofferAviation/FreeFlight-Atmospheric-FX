#include "PublishedDatarefs.h"

#include <algorithm>

namespace ffatmo {

const std::array<const char*, PublishedDatarefs::InstanceValueCount + 1>& PublishedDatarefs::instanceNames() {
    static const std::array<const char*, InstanceValueCount + 1> names {{
        "ffatmo/engine/left_ratio",
        "ffatmo/engine/right_ratio",
        "ffatmo/contrail/core_ratio",
        "ffatmo/contrail/spread_ratio",
        "ffatmo/contrail/persistence_seconds",
        "ffatmo/wing/condensation_ratio",
        "ffatmo/wing/vortex_ratio",
        "ffatmo/system/particle_budget_ratio",
        "ffatmo/wake/capture_ratio",
        "ffatmo/wake/primary_ratio",
        "ffatmo/wake/secondary_curtain_ratio",
        "ffatmo/wake/contrail_cirrus_ratio",
        "ffatmo/wake/vortex_lifetime_seconds",
        "ffatmo/wake/vortex_phase",
        nullptr,
    }};
    return names;
}

float PublishedDatarefs::readFloat(void* refcon) {
    return refcon ? *static_cast<float*>(refcon) : 0.0f;
}

void PublishedDatarefs::writeFloat(void* refcon, float value) {
    if (refcon) *static_cast<float*>(refcon) = value;
}

XPLMDataRef PublishedDatarefs::add(const char* name, float* value, bool writable) {
    XPLMDataRef handle = XPLMRegisterDataAccessor(
        name, xplmType_Float, writable ? 1 : 0,
        nullptr, nullptr, readFloat, writable ? writeFloat : nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        value, value);
    entries_.push_back({handle, value});
    return handle;
}

bool PublishedDatarefs::registerAll(EffectSettings& settings) {
    (void)settings;
    const auto& names = instanceNames();
    for (int i = 0; i < InstanceValueCount; ++i) {
        if (!add(names[i], &instanceValues_[i], false)) return false;
    }
    if (!add("ffatmo/system/enabled", &enabled_, true)) return false;
    if (!add("ffatmo/system/preview_mode", &preview_, true)) return false;
    if (!add("ffatmo/system/quality", &quality_, true)) return false;
    if (!add("ffatmo/environment/formation_probability", &formationProbability_, false)) return false;
    if (!add("ffatmo/environment/rh_ice_percent", &relativeHumidityIce_, false)) return false;
    if (!add("ffatmo/contrail/estimated_length_km", &trailLengthKm_, false)) return false;
    return true;
}

void PublishedDatarefs::unregisterAll() {
    for (const Entry& entry : entries_) {
        if (entry.handle) XPLMUnregisterDataAccessor(entry.handle);
    }
    entries_.clear();
}

void PublishedDatarefs::pullWritableControls(EffectSettings& settings) {
    // Only consume a mirror when an external dataref writer changed it. Menu
    // commands and the ImGui panel modify EffectSettings directly; treating an
    // unchanged mirror as authoritative would undo those controls next frame.
    if (!controlMirrorsInitialized_) return;
    if (enabled_ != lastPublishedEnabled_) settings.enabled = enabled_ >= 0.5f;
    if (preview_ != lastPublishedPreview_) settings.previewMode = preview_ >= 0.5f;
    if (quality_ != lastPublishedQuality_) {
        settings.quality = static_cast<QualityPreset>(
            std::clamp(static_cast<int>(quality_ + 0.5f), 0, 3));
    }
}

void PublishedDatarefs::publish(const EffectState& state, const EffectSettings& settings) {
    enabled_ = settings.enabled ? 1.0f : 0.0f;
    preview_ = settings.previewMode ? 1.0f : 0.0f;
    quality_ = static_cast<float>(settings.quality);
    lastPublishedEnabled_ = enabled_;
    lastPublishedPreview_ = preview_;
    lastPublishedQuality_ = quality_;
    controlMirrorsInitialized_ = true;
    formationProbability_ = state.formationProbability;
    relativeHumidityIce_ = state.relativeHumidityIcePercent;
    trailLengthKm_ = state.estimatedTrailLengthKm;

    instanceValues_[EngineLeft] = state.engineContrailRatio[0];
    instanceValues_[EngineRight] = state.engineContrailRatio[1];
    instanceValues_[ContrailCore] = state.contrailCoreRatio;
    instanceValues_[ContrailSpread] = state.contrailSpreadRatio;
    instanceValues_[PersistenceSeconds] = state.contrailPersistenceSeconds;
    instanceValues_[WingCondensation] = state.wingCondensationRatio;
    instanceValues_[WingVortex] = state.wingVortexRatio;
    instanceValues_[ParticleBudget] = state.particleBudgetRatio;
    instanceValues_[WakeCapture] = state.wakeCaptureRatio;
    instanceValues_[PrimaryWake] = state.primaryWakeRatio;
    instanceValues_[SecondaryCurtain] = state.secondaryCurtainRatio;
    instanceValues_[ContrailCirrus] = state.contrailCirrusRatio;
    instanceValues_[WakeLifetimeSeconds] = state.wakeVortexLifetimeSeconds;
    instanceValues_[VortexPhase] = state.vortexPhase;
}

}  // namespace ffatmo
