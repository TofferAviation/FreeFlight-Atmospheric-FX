#pragma once

#include "engine/SimulatorSnapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ffatmo::render {

inline constexpr std::size_t kContrailOpacityBucketCount = 4;
inline constexpr std::size_t kContrailTextureVariantCount = 2;
inline constexpr std::size_t kContrailRenderAssetCount =
    kContrailOpacityBucketCount * kContrailTextureVariantCount;

// v4.4 renders one composite section. Halo remains as an alias so existing
// diagnostics continue to describe the single soft outer/composite layer.
enum class ContrailRenderLayer : std::uint8_t {
    Core = 0,
    Halo = 1,
    Composite = Halo
};

struct ContrailRenderInput {
    std::uint64_t sourceParcelId = 0;
    std::uint32_t engineIndex = 0;
    engine::Vec3d localPositionM {};
    float physicsRadiusM = 0.0f;
    float opticalDepth = 0.0f;
    float normalizedIceMass = 0.0f;
    float ageSeconds = 0.0f;
    bool syntheticHead = false;
};

struct ContrailRenderSample {
    std::uint64_t renderId = 0;
    std::uint64_t sourceParcelId = 0;
    std::uint32_t engineIndex = 0;
    engine::Vec3d localPositionM {};
    engine::Vec3d trailTangentLocal {0.0, 0.0, -1.0};
    float widthM = 0.0f;
    float lengthM = 0.0f;
    float opacityStrength = 0.0f;
    float ageSeconds = 0.0f;
    float priority = 0.0f;
    std::uint8_t opacityBucket = 0;
    std::uint8_t textureVariant = 0;
    ContrailRenderLayer layer = ContrailRenderLayer::Composite;
    bool nearField = false;
};

struct ContrailRenderPlannerSettings {
    std::size_t visibleCapacity = 1024;
    std::size_t maximumSamplesPerSegment = 8;
    double maximumSegmentGapM = 250.0;
    float maximumAgeGapSeconds = 1.0f;
    float minimumOpticalDepth = 0.004f;
    float minimumOpacityStrength = 0.0008f;
    float heatHandoffStartSeconds = 0.02f;
    float heatHandoffFullSeconds = 0.80f;

    // Retained for source compatibility with v4.3 callers. The v4.4 planner
    // never creates a separate core layer and ignores these values.
    float maximumCoreAgeSeconds = 12.0f;
    float coreOpacityScale = 0.32f;
    std::uint8_t maximumCoreOpacityBucket = 1;
    float maximumCoreShare = 0.30f;

    double maximumSelectedSpacingM = 45.0;
    std::array<std::size_t, kContrailRenderAssetCount> assetCapacities {
        192, 192, 192, 192, 192, 192, 192, 192
    };
};

struct ContrailRenderPlannerStatistics {
    std::size_t inputParcelCount = 0;
    std::size_t validParcelCount = 0;
    std::size_t generatedSampleCount = 0;
    std::size_t selectedSampleCount = 0;

    // Core stays zero in v4.4. Halo is the legacy diagnostic name for the
    // single composite layer so existing reports remain readable.
    std::size_t generatedCoreCount = 0;
    std::size_t generatedHaloCount = 0;
    std::size_t selectedCoreCount = 0;
    std::size_t selectedHaloCount = 0;

    std::size_t generatedNearFieldCount = 0;
    std::size_t selectedNearFieldCount = 0;
    std::size_t streamBreakCount = 0;
    std::size_t capacityRejectedCount = 0;
    std::size_t assetCapacityRejectedCount = 0;
    std::size_t assetBucketRemapCount = 0;
    std::size_t coreBucketClampCount = 0;
    std::size_t continuityTrimmedCount = 0;
    std::array<std::size_t, kContrailOpacityBucketCount> generatedByBucket {};
    std::array<std::size_t, kContrailRenderAssetCount> selectedByAsset {};
    double maximumSelectedSpacingM = 0.0;
    double maximumCurveDeviationM = 0.0;
    std::uint64_t deterministicHash = 0;
};

struct ContrailRenderPlan {
    std::vector<ContrailRenderSample> samples;
    ContrailRenderPlannerStatistics statistics;
};

ContrailRenderPlan planContrailRenderSamples(
    const std::vector<ContrailRenderInput>& inputs,
    const ContrailRenderPlannerSettings& settings = {});

}  // namespace ffatmo::render
