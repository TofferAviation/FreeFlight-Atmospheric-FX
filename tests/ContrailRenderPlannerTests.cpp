#include "render/BillboardMath.h"
#include "render/ContrailRenderPlanner.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

ffatmo::render::ContrailRenderInput makeInput(std::uint64_t id,
                                              std::uint32_t engineIndex,
                                              double x,
                                              double y,
                                              double z,
                                              float age,
                                              float optical = 0.16f,
                                              bool syntheticHead = false) {
    ffatmo::render::ContrailRenderInput input;
    input.sourceParcelId = id;
    input.engineIndex = engineIndex;
    input.localPositionM = {x, y, z};
    input.physicsRadiusM = syntheticHead ? 0.25f : 1.25f + age * 0.08f;
    input.opticalDepth = optical;
    input.normalizedIceMass = 0.20f;
    input.ageSeconds = age;
    input.syntheticHead = syntheticHead;
    return input;
}

}  // namespace

int main() {
    using namespace ffatmo;

    const engine::Vec3d object {0.0, 0.0, 0.0};
    const engine::Vec3d trailTangent {0.32, -0.08, -0.94};
    for (const engine::Vec3d camera : {
             engine::Vec3d {0.0, 0.0, -100.0},
             engine::Vec3d {100.0, 0.0, 0.0},
             engine::Vec3d {-50.0, 80.0, 40.0},
             engine::Vec3d {0.0, -100.0, 0.0}}) {
        const auto angles = render::calculateTrailBillboardAngles(
            object, camera, trailTangent);
        const double billboardError = render::billboardAlignmentErrorDegrees(
            object, camera, angles);
        const double trailError = render::trailAlignmentErrorDegrees(
            object, camera, trailTangent, angles);
        require(billboardError <= 0.001,
                "billboard normal remains aimed at the camera");
        require(trailError <= 0.001,
                "elongated sections align with the projected trail tangent");
    }

    const engine::Vec3d parallelCamera {0.0, 0.0, -100.0};
    const engine::Vec3d parallelTangent {0.0, 0.0, -1.0};
    const auto parallelAngles = render::calculateTrailBillboardAngles(
        object, parallelCamera, parallelTangent);
    require(std::isfinite(parallelAngles.rollDeg),
            "camera-aligned tangent produces a finite stable roll");
    require(std::abs(parallelAngles.rollDeg) <= 0.001f,
            "camera-aligned tangent uses the stable zero-roll fallback");
    require(render::trailProjectionFactor(object, parallelCamera, parallelTangent) <= 0.001,
            "camera-aligned trail reports near-zero projected length");

    std::vector<render::ContrailRenderInput> inputs;
    std::uint64_t id = 1;
    for (std::uint32_t engineIndex = 0; engineIndex < 2; ++engineIndex) {
        const double x = engineIndex == 0 ? -5.0 : 5.0;
        for (int parcel = 0; parcel < 30; ++parcel) {
            const float age = 11.6f - static_cast<float>(parcel) * 0.4f;
            inputs.push_back(makeInput(
                id++, engineIndex, x, 1000.0 - age * 0.35, -age * 50.0, age, 0.30f));
        }
        inputs.push_back(makeInput(
            0xfff0000000000000ull + engineIndex,
            engineIndex,
            x,
            1000.0,
            2.0,
            0.0f,
            0.12f,
            true));
    }

    render::ContrailRenderPlannerSettings settings;
    settings.visibleCapacity = 1024;
    settings.maximumSamplesPerSegment = 8;
    settings.maximumSelectedSpacingM = 45.0;
    const auto first = render::planContrailRenderSamples(inputs, settings);
    const auto second = render::planContrailRenderSamples(inputs, settings);

    require(!first.samples.empty(), "planner produces visible trail sections");
    require(first.statistics.deterministicHash == second.statistics.deterministicHash,
            "same inputs produce the same v4.3 plan hash");
    require(first.samples.size() == second.samples.size(),
            "same inputs produce the same sample count");
    require(first.statistics.selectedCoreCount > 0, "short white core sections are selected");
    require(first.statistics.selectedHaloCount > first.statistics.selectedCoreCount,
            "halo receives the majority of the visible budget");
    require(first.statistics.generatedNearFieldCount > 0,
            "synthetic exhaust heads generate near-field sections");
    require(first.statistics.selectedNearFieldCount > 0,
            "near-field sections survive continuity-first selection");
    require(first.statistics.selectedSampleCount <= settings.visibleCapacity,
            "global visible capacity is respected");
    require(first.statistics.selectedCoreCount <= static_cast<std::size_t>(
                std::floor(settings.visibleCapacity * settings.maximumCoreShare)),
            "core selection remains inside the configured share");
    require(first.statistics.maximumSelectedSpacingM <=
                settings.maximumSelectedSpacingM + 1.0e-6,
            "selected streams never exceed the hard continuity spacing");
    require(first.statistics.maximumCurveDeviationM <= 11.0 + 1.0e-6,
            "curved interpolation is clamped to 22 percent of a 50 metre segment");
    require(first.statistics.generatedSampleCount < inputs.size() * 20,
            "v4.3 avoids runaway near-field over-generation");
    require(first.statistics.coreBucketClampCount > 0,
            "high optical input exercises the white-core opacity clamp");

    std::unordered_set<std::uint64_t> renderIds;
    std::array<std::size_t, render::kContrailRenderAssetCount> observedByAsset {};
    for (const auto& sample : first.samples) {
        require(renderIds.insert(sample.renderId).second,
                "every selected section has a unique persistent render id");
        if (sample.engineIndex == 0) {
            require(sample.localPositionM.x < 0.0,
                    "left-engine interpolation never crosses into the right stream");
        } else if (sample.engineIndex == 1) {
            require(sample.localPositionM.x > 0.0,
                    "right-engine interpolation never crosses into the left stream");
        }
        require(sample.widthM >= 0.30f && sample.widthM <= 24.0f,
                "section width stays inside v4.3 hard limits");
        require(sample.lengthM >= 0.60f && sample.lengthM <= 30.0f,
                "section length stays inside v4.3 hard limits");
        if (sample.layer == render::ContrailRenderLayer::Core) {
            require(sample.ageSeconds < settings.maximumCoreAgeSeconds,
                    "dense centre ends by twelve seconds");
            require(sample.opacityBucket <= settings.maximumCoreOpacityBucket,
                    "core never enters the dark high-opacity buckets");
            require(sample.lengthM <= 14.0f + 1.0e-6f,
                    "core sections remain short enough to avoid tube-like strands");
        }
        const std::size_t assetIndex =
            static_cast<std::size_t>(sample.opacityBucket) *
                render::kContrailTextureVariantCount +
            static_cast<std::size_t>(sample.textureVariant);
        ++observedByAsset[assetIndex];
    }
    require(observedByAsset == first.statistics.selectedByAsset,
            "per-asset diagnostics match the selected section set");
    for (std::size_t assetIndex = 0; assetIndex < observedByAsset.size(); ++assetIndex) {
        require(observedByAsset[assetIndex] <= settings.assetCapacities[assetIndex],
                "planner never oversubscribes an XPLM instance pool");
    }

    std::vector<render::ContrailRenderInput> capacityInputs;
    id = 1000;
    for (std::uint32_t engineIndex = 0; engineIndex < 2; ++engineIndex) {
        const double x = engineIndex == 0 ? -6.0 : 6.0;
        for (int parcel = 0; parcel < 80; ++parcel) {
            const float age = 31.6f - static_cast<float>(parcel) * 0.4f;
            capacityInputs.push_back(makeInput(
                id++, engineIndex, x, 2000.0, -age * 35.0, age, 0.20f));
        }
    }
    settings.visibleCapacity = 128;
    settings.assetCapacities.fill(16);
    const auto limited = render::planContrailRenderSamples(capacityInputs, settings);
    require(limited.samples.size() <= settings.visibleCapacity,
            "capacity planner respects the configured visible budget");
    std::size_t engine0Count = 0;
    std::size_t engine1Count = 0;
    for (const auto& sample : limited.samples) {
        if (sample.engineIndex == 0) ++engine0Count;
        if (sample.engineIndex == 1) ++engine1Count;
    }
    require(engine0Count >= 24 && engine1Count >= 24,
            "round-robin continuity selection protects both engines");
    require(limited.statistics.capacityRejectedCount > 0,
            "old sections are rejected when the visible budget is full");
    require(limited.statistics.selectedHaloCount >= limited.statistics.selectedCoreCount,
            "capacity pressure never makes the old dense core dominant");
    for (std::size_t assetIndex = 0;
         assetIndex < limited.statistics.selectedByAsset.size();
         ++assetIndex) {
        require(limited.statistics.selectedByAsset[assetIndex] <= 16,
                "every asset bucket remains within its physical pool");
    }

    std::vector<render::ContrailRenderInput> broken {
        makeInput(5000, 0, 0.0, 1000.0, 0.0, 4.0f),
        makeInput(5001, 0, 0.0, 1000.0, -400.0, 3.6f)
    };
    settings.visibleCapacity = 64;
    settings.assetCapacities.fill(16);
    const auto brokenPlan = render::planContrailRenderSamples(broken, settings);
    require(brokenPlan.statistics.streamBreakCount == 1,
            "segments beyond the 250 metre continuity limit are not bridged");

    std::cout << "FFAtmo Renderer Foundation v4.3 white-core tests passed\n";
    return 0;
}
