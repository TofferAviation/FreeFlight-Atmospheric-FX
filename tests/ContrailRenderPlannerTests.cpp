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
                                             float optical = 0.16f) {
    ffatmo::render::ContrailRenderInput input;
    input.sourceParcelId = id;
    input.engineIndex = engineIndex;
    input.localPositionM = {x, y, z};
    input.physicsRadiusM = 1.25f + age * 0.08f;
    input.opticalDepth = optical;
    input.normalizedIceMass = 0.20f;
    input.ageSeconds = age;
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
                "the elongated segment remains aligned with the projected trail tangent");
    }

    std::vector<render::ContrailRenderInput> inputs;
    std::uint64_t id = 1;
    for (std::uint32_t engineIndex = 0; engineIndex < 2; ++engineIndex) {
        const double x = engineIndex == 0 ? -5.0 : 5.0;
        for (int parcel = 0; parcel < 18; ++parcel) {
            const float age = 7.2f - static_cast<float>(parcel) * 0.4f;
            inputs.push_back(makeInput(
                id++, engineIndex, x, 1000.0 - age * 0.35, -age * 70.0, age));
        }
    }

    render::ContrailRenderPlannerSettings settings;
    settings.visibleCapacity = 1024;
    settings.maximumSamplesPerSegment = 16;
    const auto first = render::planContrailRenderSamples(inputs, settings);
    const auto second = render::planContrailRenderSamples(inputs, settings);

    require(!first.samples.empty(), "planner produces visible trail segments");
    require(first.statistics.deterministicHash == second.statistics.deterministicHash,
            "same physics inputs produce the same render-plan hash");
    require(first.samples.size() == second.samples.size(),
            "same physics inputs produce the same sample count");
    require(first.statistics.selectedCoreCount > 0, "core segments are generated");
    require(first.statistics.selectedHaloCount > 0, "aged parcels generate halo segments");
    require(first.statistics.selectedSampleCount <= settings.visibleCapacity,
            "global visible capacity is respected");
    require(first.statistics.maximumCurveDeviationM <= 17.5 + 1.0e-6,
            "curved interpolation is clamped to 25 percent of a 70 metre segment");
    require(first.statistics.generatedSampleCount < inputs.size() * 20,
            "elongated segments avoid the v4 over-generation rate");

    std::unordered_set<std::uint64_t> renderIds;
    std::array<std::size_t, render::kContrailRenderAssetCount> observedByAsset {};
    for (const auto& sample : first.samples) {
        require(renderIds.insert(sample.renderId).second,
                "every selected segment has a unique persistent render id");
        if (sample.engineIndex == 0) {
            require(sample.localPositionM.x < 0.0,
                    "left-engine interpolation never crosses into the right stream");
        } else if (sample.engineIndex == 1) {
            require(sample.localPositionM.x > 0.0,
                    "right-engine interpolation never crosses into the left stream");
        }
        require(sample.widthM >= 0.50f && sample.widthM <= 24.0f,
                "segment width stays inside v4.1 hard limits");
        require(sample.lengthM >= 1.0f && sample.lengthM <= 32.0f,
                "segment length stays inside v4.1 hard limits");
        require(sample.lengthM >= sample.widthM * 1.19f,
                "each selected sprite is an elongated trail segment rather than a disc");
        if (sample.ageSeconds <= 2.0f) {
            require(sample.layer == render::ContrailRenderLayer::Core,
                    "the first two seconds contain no halo layer");
        }
        const std::size_t assetIndex =
            static_cast<std::size_t>(sample.opacityBucket) *
                render::kContrailTextureVariantCount +
            static_cast<std::size_t>(sample.textureVariant);
        ++observedByAsset[assetIndex];
    }
    require(observedByAsset == first.statistics.selectedByAsset,
            "per-asset diagnostics match the selected segment set");
    for (std::size_t assetIndex = 0; assetIndex < observedByAsset.size(); ++assetIndex) {
        require(observedByAsset[assetIndex] <= settings.assetCapacities[assetIndex],
                "the planner never oversubscribes an XPLM instance pool");
    }

    std::vector<render::ContrailRenderInput> capacityInputs;
    id = 1000;
    for (std::uint32_t engineIndex = 0; engineIndex < 2; ++engineIndex) {
        const double x = engineIndex == 0 ? -6.0 : 6.0;
        for (int parcel = 0; parcel < 80; ++parcel) {
            const float age = 31.6f - static_cast<float>(parcel) * 0.4f;
            capacityInputs.push_back(makeInput(
                id++, engineIndex, x, 2000.0, -age * 45.0, age, 0.20f));
        }
    }
    settings.visibleCapacity = 64;
    settings.assetCapacities.fill(8);
    const auto limited = render::planContrailRenderSamples(capacityInputs, settings);
    require(limited.samples.size() <= settings.visibleCapacity,
            "capacity planner respects the configured visible budget");
    std::size_t engine0Count = 0;
    std::size_t engine1Count = 0;
    for (const auto& sample : limited.samples) {
        if (sample.engineIndex == 0) ++engine0Count;
        if (sample.engineIndex == 1) ++engine1Count;
    }
    require(engine0Count >= 12 && engine1Count >= 12,
            "each engine retains approximately a 20 percent share under pressure");
    require(limited.statistics.capacityRejectedCount > 0,
            "weak segments are rejected when the render budget is full");
    for (std::size_t assetIndex = 0;
         assetIndex < limited.statistics.selectedByAsset.size();
         ++assetIndex) {
        require(limited.statistics.selectedByAsset[assetIndex] <= 8,
                "every selected asset bucket remains within its physical pool");
    }

    std::vector<render::ContrailRenderInput> broken {
        makeInput(5000, 0, 0.0, 1000.0, 0.0, 4.0f),
        makeInput(5001, 0, 0.0, 1000.0, -400.0, 3.6f)
    };
    settings.visibleCapacity = 64;
    const auto brokenPlan = render::planContrailRenderSamples(broken, settings);
    require(brokenPlan.statistics.streamBreakCount == 1,
            "segments beyond the 250 metre continuity limit are not bridged");

    std::cout << "FFAtmo Renderer v4.1 foundation tests passed\n";
    return 0;
}
