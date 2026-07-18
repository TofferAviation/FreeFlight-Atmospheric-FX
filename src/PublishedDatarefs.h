#pragma once

#include "AtmosphereModel.h"

#include "XPLMDataAccess.h"

#include <array>
#include <vector>

namespace ffatmo {

class PublishedDatarefs {
public:
    enum InstanceValue : int {
        EngineLeft = 0,
        EngineRight,
        ContrailCore,
        ContrailSpread,
        PersistenceSeconds,
        WingCondensation,
        WingVortex,
        ParticleBudget,
        WakeCapture,
        PrimaryWake,
        SecondaryCurtain,
        ContrailCirrus,
        WakeLifetimeSeconds,
        VortexPhase,
        InstanceValueCount,
    };

    bool registerAll(EffectSettings& settings);
    void unregisterAll();
    void pullWritableControls(EffectSettings& settings);
    void publish(const EffectState& state, const EffectSettings& settings);
    const std::array<float, InstanceValueCount>& instanceValues() const { return instanceValues_; }
    static const std::array<const char*, InstanceValueCount + 1>& instanceNames();

private:
    struct Entry {
        XPLMDataRef handle = nullptr;
        float* value = nullptr;
    };

    static float readFloat(void* refcon);
    static void writeFloat(void* refcon, float value);
    XPLMDataRef add(const char* name, float* value, bool writable);

    std::vector<Entry> entries_;
    std::array<float, InstanceValueCount> instanceValues_ {};
    float enabled_ = 1.0f;
    float preview_ = 0.0f;
    float quality_ = 2.0f;
    float formationProbability_ = 0.0f;
    float relativeHumidityIce_ = 0.0f;
    float trailLengthKm_ = 0.0f;
    float lastPublishedEnabled_ = 1.0f;
    float lastPublishedPreview_ = 0.0f;
    float lastPublishedQuality_ = 2.0f;
    bool controlMirrorsInitialized_ = false;
};

}  // namespace ffatmo
