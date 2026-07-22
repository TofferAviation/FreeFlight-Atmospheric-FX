#pragma once

#include "engine/SimulatorSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ffatmo::render {

enum class ContrailRenderLayer : std::uint8_t {
    Core = 0,
    Halo = 1
};

struct ContrailRenderInput {
    std::uint64_t sourceParcelId = 0;
    std::uint32_t engineIndex = 0;
    engine::Vec3d localPositionM {};
    float physicsRadiusM = 0.0f;
    float opticalDepth = 0.0f;
    float normalizedIceMass = 0.0f;
    float ageSeconds = 0.0f;
};

struct ContrailRenderSample {
    std::uint64_t renderId = 0;
    std::uint64_t sourceParcelId = 0;
    std::uint32_t engineIndex = 0;
    engine::Vec3d localPositionM {};
    float radiusM = 0.0f;
    float opacityStrength = 0.0f;
    float ageSeconds = 0.0f;
    float priority = 0.0f;
    std::uint8_t opacityBucket = 0;
    std::uint8_t textureVariant = 0;
    ContrailRenderLayer layer = ContrailRenderLayer::Core;
};

struct ContrailRenderPlannerSettings {
    std::size_t visibleCapacity = 1024;
    std::size_t maximumSamplesPerSegment = 16;
    double maximumSegmentGapM = 250.0;
    float maximumAgeGapSeconds = 1.0f;
    float minimumOpticalDepth = 0.004f;
    float minimumOpacityStrength = 0.002f;
};

struct ContrailRenderPlannerStatistics {
    std::size_t inputParcelCount = 0;
    std::size_t validParcelCount = 0;
    std::size_t generatedSampleCount = 0;
    std::size_t selectedSampleCount = 0;
    std::size_t generatedCoreCount = 0;
    std::size_t generatedHaloCount = 0;
    std::size_t selectedCoreCount = 0;
    std::size_t selectedHaloCount = 0;
    std::size_t streamBreakCount = 0;
    std::size_t capacityRejectedCount = 0;
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
