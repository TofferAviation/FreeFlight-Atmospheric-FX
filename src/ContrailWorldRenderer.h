#pragma once

#include "ContrailDebugOverlay.h"

#include "XPLMDataAccess.h"
#include "XPLMInstance.h"
#include "XPLMScenery.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace ffatmo {

class ContrailWorldRenderer {
public:
    static constexpr std::size_t kOpacityBucketCount = 4;
    static constexpr std::size_t kInstancesPerBucket = 144;

    ContrailWorldRenderer();
    ~ContrailWorldRenderer();

    bool start(const std::filesystem::path& assetDirectory);
    void stop();
    void update(const std::vector<ContrailDebugRenderParcel>& parcels);
    void setEnabled(bool enabled);

    bool enabled() const { return enabled_; }
    bool ready() const;
    std::size_t visibleInstanceCount() const { return visibleInstanceCount_; }
    std::size_t loadedObjectCount() const { return loadedObjectCount_; }

private:
    struct Pool {
        XPLMObjectRef object = nullptr;
        std::vector<XPLMInstanceRef> instances;
    };

    struct LoadContext {
        ContrailWorldRenderer* owner = nullptr;
        std::size_t bucket = 0;
    };

    static float readScale(void* refcon);
    static void objectLoadedCallback(XPLMObjectRef object, void* refcon);

    void objectLoaded(std::size_t bucket, XPLMObjectRef object);
    void hideAll();
    void setInstance(XPLMInstanceRef instance,
                     const ContrailDebugRenderParcel& parcel,
                     float scaleM,
                     float rollDeg);
    void hideInstance(XPLMInstanceRef instance, std::size_t ordinal);
    static std::size_t opacityBucket(const ContrailDebugRenderParcel& parcel);

    std::array<Pool, kOpacityBucketCount> pools_;
    std::array<LoadContext, kOpacityBucketCount> loadContexts_;
    XPLMDataRef scaleDataRef_ = nullptr;
    std::filesystem::path assetDirectory_;
    std::size_t loadedObjectCount_ = 0;
    std::size_t visibleInstanceCount_ = 0;
    bool running_ = false;
    bool enabled_ = true;
};

}  // namespace ffatmo
