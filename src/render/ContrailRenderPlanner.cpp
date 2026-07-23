#include "render/ContrailRenderPlanner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace ffatmo::render {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

struct Candidate {
    ContrailRenderSample sample;
};

bool finite(double value) { return std::isfinite(value); }
bool finite(float value) { return std::isfinite(value); }

engine::Vec3d add(const engine::Vec3d& a, const engine::Vec3d& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

engine::Vec3d subtract(const engine::Vec3d& a, const engine::Vec3d& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

engine::Vec3d multiply(const engine::Vec3d& value, double scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

double dot(const engine::Vec3d& a, const engine::Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

double length(const engine::Vec3d& value) {
    return std::sqrt(dot(value, value));
}

double distance(const engine::Vec3d& a, const engine::Vec3d& b) {
    return length(subtract(b, a));
}

engine::Vec3d normalized(const engine::Vec3d& value,
                         const engine::Vec3d& fallback = {1.0, 0.0, 0.0}) {
    const double magnitude = length(value);
    if (!finite(magnitude) || magnitude <= 1.0e-9) return fallback;
    return multiply(value, 1.0 / magnitude);
}

engine::Vec3d cross(const engine::Vec3d& a, const engine::Vec3d& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

engine::Vec3d lerp(const engine::Vec3d& a, const engine::Vec3d& b, double ratio) {
    return add(a, multiply(subtract(b, a), ratio));
}

float mix(float a, float b, float ratio) {
    return a + (b - a) * ratio;
}

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float value) {
    if (std::abs(edge1 - edge0) <= 1.0e-6f) return value >= edge1 ? 1.0f : 0.0f;
    const float ratio = clamp01((value - edge0) / (edge1 - edge0));
    return ratio * ratio * (3.0f - 2.0f * ratio);
}

float interpolate(float ageSeconds,
                  float age0,
                  float value0,
                  float age1,
                  float value1) {
    if (ageSeconds <= age0) return value0;
    if (ageSeconds >= age1) return value1;
    return mix(value0, value1, (ageSeconds - age0) / (age1 - age0));
}

float coreHalfWidthForAge(float ageSeconds, float physicsRadiusM) {
    float halfWidth = 0.22f;
    if (ageSeconds <= 1.0f) {
        halfWidth = interpolate(ageSeconds, 0.0f, 0.22f, 1.0f, 0.42f);
    } else if (ageSeconds <= 4.0f) {
        halfWidth = interpolate(ageSeconds, 1.0f, 0.42f, 4.0f, 0.90f);
    } else if (ageSeconds <= 10.0f) {
        halfWidth = interpolate(ageSeconds, 4.0f, 0.90f, 10.0f, 1.55f);
    } else {
        halfWidth = interpolate(ageSeconds, 10.0f, 1.55f, 22.0f, 2.70f);
    }
    const float physicsFactor = std::clamp(
        std::sqrt(std::max(physicsRadiusM, 0.05f) / 1.25f), 0.80f, 1.14f);
    return std::clamp(halfWidth * physicsFactor, 0.20f, 3.2f);
}

float haloHalfWidthForAge(float ageSeconds, float coreHalfWidthM) {
    if (ageSeconds <= 1.25f) return 0.0f;
    float halfWidth = 0.90f;
    if (ageSeconds <= 8.0f) {
        halfWidth = interpolate(ageSeconds, 1.25f, 0.90f, 8.0f, 2.35f);
    } else if (ageSeconds <= 22.0f) {
        halfWidth = interpolate(ageSeconds, 8.0f, 2.35f, 22.0f, 5.20f);
    } else {
        halfWidth = interpolate(ageSeconds, 22.0f, 5.20f, 65.0f, 11.0f);
    }
    return std::clamp(std::max(halfWidth, coreHalfWidthM * 1.55f), 0.90f, 12.0f);
}

float targetSegmentSpacingForAge(float ageSeconds, float coreWidthM) {
    float spacing = 2.0f;
    if (ageSeconds <= 1.0f) {
        spacing = interpolate(ageSeconds, 0.0f, 1.8f, 1.0f, 2.8f);
    } else if (ageSeconds <= 6.0f) {
        spacing = interpolate(ageSeconds, 1.0f, 2.8f, 6.0f, 4.5f);
    } else if (ageSeconds <= 20.0f) {
        spacing = interpolate(ageSeconds, 6.0f, 4.5f, 20.0f, 7.0f);
    } else {
        spacing = interpolate(ageSeconds, 20.0f, 7.0f, 65.0f, 12.0f);
    }
    return std::clamp(std::max(spacing, coreWidthM * 0.72f), 1.6f, 13.0f);
}

float coreAgeFactor(float ageSeconds, float maximumCoreAgeSeconds) {
    if (ageSeconds >= maximumCoreAgeSeconds) return 0.0f;
    if (ageSeconds <= 3.0f) return 1.0f;
    if (ageSeconds <= 10.0f) return interpolate(ageSeconds, 3.0f, 1.0f, 10.0f, 0.42f);
    if (ageSeconds <= 18.0f) return interpolate(ageSeconds, 10.0f, 0.42f, 18.0f, 0.10f);
    return interpolate(ageSeconds, 18.0f, 0.10f, maximumCoreAgeSeconds, 0.0f);
}

float haloAgeFactor(float ageSeconds) {
    if (ageSeconds <= 1.25f) return 0.0f;
    if (ageSeconds <= 8.0f) return interpolate(ageSeconds, 1.25f, 0.06f, 8.0f, 0.34f);
    if (ageSeconds <= 24.0f) return interpolate(ageSeconds, 8.0f, 0.34f, 24.0f, 0.56f);
    if (ageSeconds <= 45.0f) return interpolate(ageSeconds, 24.0f, 0.56f, 45.0f, 0.42f);
    return interpolate(ageSeconds, 45.0f, 0.42f, 70.0f, 0.16f);
}

float lateralJitterFraction(float ageSeconds) {
    if (ageSeconds <= 2.0f) return 0.008f;
    if (ageSeconds <= 20.0f) return interpolate(ageSeconds, 2.0f, 0.02f, 20.0f, 0.09f);
    return interpolate(ageSeconds, 20.0f, 0.09f, 65.0f, 0.18f);
}

std::uint64_t mixId(std::uint64_t a,
                    std::uint64_t b,
                    std::uint32_t ordinal,
                    ContrailRenderLayer layer) {
    std::uint64_t value = a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6u) + (a >> 2u));
    value ^= static_cast<std::uint64_t>(ordinal) * 0xbf58476d1ce4e5b9ull;
    value ^= layer == ContrailRenderLayer::Halo
        ? 0x94d049bb133111ebull
        : 0x2545f4914f6cdd1dull;
    value ^= value >> 30u;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27u;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31u;
    return value;
}

std::uint8_t opacityBucket(float strength) {
    if (strength < 0.012f) return 0;
    if (strength < 0.030f) return 1;
    if (strength < 0.060f) return 2;
    return 3;
}

float candidatePriority(const ContrailRenderSample& sample) {
    float score = sample.opacityStrength * 1000.0f;
    score += std::max(0.0f, 65.0f - sample.ageSeconds) * 4.0f;
    if (sample.nearField) score += 8000.0f;
    if (sample.ageSeconds <= 8.0f) score += 1500.0f;
    if (sample.layer == ContrailRenderLayer::Halo && sample.ageSeconds >= 10.0f) {
        score += 600.0f;
    }
    return score;
}

bool validInput(const ContrailRenderInput& input,
                const ContrailRenderPlannerSettings& settings) {
    return input.engineIndex < engine::kMaximumRecordedEngines &&
           finite(input.localPositionM.x) && finite(input.localPositionM.y) &&
           finite(input.localPositionM.z) && finite(input.physicsRadiusM) &&
           finite(input.opticalDepth) && finite(input.normalizedIceMass) &&
           finite(input.ageSeconds) && input.physicsRadiusM > 0.0f &&
           input.opticalDepth >= settings.minimumOpticalDepth && input.ageSeconds >= 0.0f;
}

engine::Vec3d clampedHermite(const engine::Vec3d& p0,
                            const engine::Vec3d& p1,
                            const engine::Vec3d& p2,
                            const engine::Vec3d& p3,
                            double ratio,
                            double& deviationM) {
    const double segmentLength = std::max(distance(p1, p2), 1.0e-6);
    engine::Vec3d tangent1 = multiply(subtract(p2, p0), 0.5);
    engine::Vec3d tangent2 = multiply(subtract(p3, p1), 0.5);
    const double tangentLimit = segmentLength * 1.15;
    if (length(tangent1) > tangentLimit) tangent1 = multiply(normalized(tangent1), tangentLimit);
    if (length(tangent2) > tangentLimit) tangent2 = multiply(normalized(tangent2), tangentLimit);

    const double t2 = ratio * ratio;
    const double t3 = t2 * ratio;
    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + ratio;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;
    engine::Vec3d curve = add(
        add(multiply(p1, h00), multiply(tangent1, h10)),
        add(multiply(p2, h01), multiply(tangent2, h11)));

    const engine::Vec3d linear = lerp(p1, p2, ratio);
    engine::Vec3d deviation = subtract(curve, linear);
    deviationM = length(deviation);
    const double maximumDeviation = segmentLength * 0.22;
    if (deviationM > maximumDeviation && deviationM > 1.0e-9) {
        deviation = multiply(deviation, maximumDeviation / deviationM);
        curve = add(linear, deviation);
        deviationM = maximumDeviation;
    }
    return curve;
}

void hashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= kFnvPrime;
    }
}

template <typename T>
void hashValue(std::uint64_t& hash, const T& value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashSample(std::uint64_t& hash, const ContrailRenderSample& sample) {
    hashValue(hash, sample.renderId);
    hashValue(hash, sample.sourceParcelId);
    hashValue(hash, sample.engineIndex);
    hashValue(hash, sample.localPositionM.x);
    hashValue(hash, sample.localPositionM.y);
    hashValue(hash, sample.localPositionM.z);
    hashValue(hash, sample.trailTangentLocal.x);
    hashValue(hash, sample.trailTangentLocal.y);
    hashValue(hash, sample.trailTangentLocal.z);
    hashValue(hash, sample.widthM);
    hashValue(hash, sample.lengthM);
    hashValue(hash, sample.opacityStrength);
    hashValue(hash, sample.ageSeconds);
    hashValue(hash, sample.opacityBucket);
    hashValue(hash, sample.textureVariant);
    const auto layer = static_cast<std::uint8_t>(sample.layer);
    const auto nearField = static_cast<std::uint8_t>(sample.nearField);
    hashValue(hash, layer);
    hashValue(hash, nearField);
}

void appendLayer(std::vector<Candidate>& candidates,
                 ContrailRenderPlannerStatistics& statistics,
                 const ContrailRenderInput& first,
                 const ContrailRenderInput& second,
                 const engine::Vec3d& centrelinePosition,
                 const engine::Vec3d& trailTangent,
                 const engine::Vec3d& right,
                 const engine::Vec3d& up,
                 float ratio,
                 float centreSpacingM,
                 std::uint32_t ordinal,
                 ContrailRenderLayer layer,
                 const ContrailRenderPlannerSettings& settings) {
    const float age = mix(first.ageSeconds, second.ageSeconds, ratio);
    if (layer == ContrailRenderLayer::Core && age >= settings.maximumCoreAgeSeconds) return;

    const float physicsRadius = mix(first.physicsRadiusM, second.physicsRadiusM, ratio);
    const float optical = mix(first.opticalDepth, second.opticalDepth, ratio);
    const float heatHandoff = smoothstep(
        settings.heatHandoffStartSeconds,
        settings.heatHandoffFullSeconds,
        age);
    const float dryAgeFade = clamp01(1.0f - std::max(0.0f, age - 45.0f) / 25.0f);
    const float layerFactor = layer == ContrailRenderLayer::Core
        ? coreAgeFactor(age, settings.maximumCoreAgeSeconds)
        : haloAgeFactor(age);
    const float strength = optical * heatHandoff * dryAgeFade * layerFactor;
    if (!finite(strength) || strength < settings.minimumOpacityStrength) return;

    const float coreHalfWidth = coreHalfWidthForAge(age, physicsRadius);
    float halfWidth = layer == ContrailRenderLayer::Core
        ? coreHalfWidth
        : haloHalfWidthForAge(age, coreHalfWidth);
    if (layer == ContrailRenderLayer::Halo && halfWidth <= 0.0f) return;

    const bool nearField = first.syntheticHead || second.syntheticHead || age < 1.0f;
    if (nearField) {
        halfWidth *= interpolate(age, 0.0f, 0.42f, 1.0f, 1.0f);
    }

    const std::uint64_t renderId = mixId(
        first.sourceParcelId, second.sourceParcelId, ordinal, layer);
    const float phase = static_cast<float>(
        age * 0.31 + static_cast<double>(first.engineIndex) * 1.731 +
        static_cast<double>((renderId >> 12u) & 0xffu) * 0.0027);

    engine::Vec3d position = centrelinePosition;
    const float jitter = halfWidth * lateralJitterFraction(age);
    if (!nearField) {
        if (layer == ContrailRenderLayer::Core) {
            position = add(position, multiply(right, jitter * std::sin(phase)));
            position = add(position, multiply(up, jitter * 0.18 * std::cos(phase * 0.71f)));
        } else {
            const float growth = clamp01((age - 1.25f) / 35.0f);
            position = add(position, multiply(right, jitter * 1.7f * growth * std::sin(phase * 0.59f)));
            position = add(position, multiply(up, jitter * 0.40f * growth * std::cos(phase * 0.43f)));
        }
    }

    ContrailRenderSample sample;
    sample.renderId = renderId;
    sample.sourceParcelId = ratio < 0.5f ? first.sourceParcelId : second.sourceParcelId;
    sample.engineIndex = first.engineIndex;
    sample.localPositionM = position;
    sample.trailTangentLocal = normalized(trailTangent, {0.0, 0.0, -1.0});
    sample.widthM = std::clamp(halfWidth * 2.0f, 0.30f, 24.0f);
    const float overlapFactor = nearField ? 1.20f :
        (layer == ContrailRenderLayer::Core ? 1.38f : 1.55f);
    sample.lengthM = std::clamp(
        std::max(centreSpacingM * overlapFactor, sample.widthM * 1.08f),
        0.60f,
        30.0f);
    sample.opacityStrength = strength;
    sample.ageSeconds = age;
    sample.opacityBucket = opacityBucket(strength);
    if (layer == ContrailRenderLayer::Halo && sample.opacityBucket > 0) {
        --sample.opacityBucket;
    }
    sample.textureVariant = static_cast<std::uint8_t>(
        (renderId ^ (static_cast<std::uint64_t>(first.engineIndex) * 0x9e3779b9u)) & 1u);
    sample.layer = layer;
    sample.nearField = nearField;
    sample.priority = candidatePriority(sample);
    candidates.push_back({sample});
    ++statistics.generatedByBucket[sample.opacityBucket];
    if (nearField) ++statistics.generatedNearFieldCount;
    if (layer == ContrailRenderLayer::Core) {
        ++statistics.generatedCoreCount;
    } else {
        ++statistics.generatedHaloCount;
    }
}

std::size_t queueIndex(std::uint32_t engineIndex, ContrailRenderLayer layer) {
    return static_cast<std::size_t>(engineIndex) * 2u +
           (layer == ContrailRenderLayer::Core ? 0u : 1u);
}

}  // namespace

ContrailRenderPlan planContrailRenderSamples(
    const std::vector<ContrailRenderInput>& inputs,
    const ContrailRenderPlannerSettings& settings) {
    ContrailRenderPlan result;
    auto& statistics = result.statistics;
    statistics.inputParcelCount = inputs.size();

    std::array<std::vector<ContrailRenderInput>, engine::kMaximumRecordedEngines> streams;
    for (const auto& input : inputs) {
        if (!validInput(input, settings)) continue;
        streams[input.engineIndex].push_back(input);
        ++statistics.validParcelCount;
    }

    std::vector<Candidate> candidates;
    candidates.reserve(statistics.validParcelCount * 8);
    const std::size_t maximumSubdivisions = std::max<std::size_t>(
        1, std::min<std::size_t>(settings.maximumSamplesPerSegment, 8));

    for (std::size_t engineIndex = 0; engineIndex < streams.size(); ++engineIndex) {
        auto& stream = streams[engineIndex];
        if (stream.empty()) continue;
        std::stable_sort(stream.begin(), stream.end(), [](const auto& a, const auto& b) {
            if (a.ageSeconds != b.ageSeconds) return a.ageSeconds > b.ageSeconds;
            if (a.syntheticHead != b.syntheticHead) return !a.syntheticHead;
            return a.sourceParcelId < b.sourceParcelId;
        });

        if (stream.size() == 1) {
            const float width = coreHalfWidthForAge(
                stream.front().ageSeconds, stream.front().physicsRadiusM) * 2.0f;
            appendLayer(candidates,
                        statistics,
                        stream.front(),
                        stream.front(),
                        stream.front().localPositionM,
                        {0.0, 0.0, -1.0},
                        {1.0, 0.0, 0.0},
                        {0.0, 1.0, 0.0},
                        0.0f,
                        std::max(width, 0.6f),
                        0,
                        ContrailRenderLayer::Core,
                        settings);
            continue;
        }

        for (std::size_t index = 0; index + 1 < stream.size(); ++index) {
            const auto& first = stream[index];
            const auto& second = stream[index + 1];
            const double gapM = distance(first.localPositionM, second.localPositionM);
            const float ageGap = std::abs(first.ageSeconds - second.ageSeconds);
            if (!finite(gapM) || gapM > settings.maximumSegmentGapM ||
                ageGap > settings.maximumAgeGapSeconds) {
                ++statistics.streamBreakCount;
                continue;
            }

            const float meanAge = 0.5f * (first.ageSeconds + second.ageSeconds);
            const float meanPhysicsRadius = 0.5f * (first.physicsRadiusM + second.physicsRadiusM);
            const float coreWidth = coreHalfWidthForAge(meanAge, meanPhysicsRadius) * 2.0f;
            const float targetSpacing = targetSegmentSpacingForAge(meanAge, coreWidth);
            const std::size_t subdivisions = std::clamp<std::size_t>(
                static_cast<std::size_t>(std::ceil(gapM / std::max(targetSpacing, 0.01f))),
                1,
                maximumSubdivisions);
            const float centreSpacingM = static_cast<float>(gapM / subdivisions);

            const auto& p0 = index > 0 ? stream[index - 1] : first;
            const auto& p3 = index + 2 < stream.size() ? stream[index + 2] : second;
            const bool p0Continuous = index > 0 &&
                distance(p0.localPositionM, first.localPositionM) <= settings.maximumSegmentGapM &&
                std::abs(p0.ageSeconds - first.ageSeconds) <= settings.maximumAgeGapSeconds;
            const bool p3Continuous = index + 2 < stream.size() &&
                distance(second.localPositionM, p3.localPositionM) <= settings.maximumSegmentGapM &&
                std::abs(second.ageSeconds - p3.ageSeconds) <= settings.maximumAgeGapSeconds;
            const engine::Vec3d curveP0 = p0Continuous ? p0.localPositionM : first.localPositionM;
            const engine::Vec3d curveP3 = p3Continuous ? p3.localPositionM : second.localPositionM;
            const engine::Vec3d trailTangent = normalized(
                subtract(second.localPositionM, first.localPositionM), {0.0, 0.0, -1.0});
            const engine::Vec3d right = normalized(
                cross(trailTangent, {0.0, 1.0, 0.0}), {1.0, 0.0, 0.0});
            const engine::Vec3d up = normalized(cross(right, trailTangent), {0.0, 1.0, 0.0});

            for (std::size_t step = 0; step < subdivisions; ++step) {
                const float ratio = (static_cast<float>(step) + 0.5f) /
                                    static_cast<float>(subdivisions);
                double curveDeviation = 0.0;
                const engine::Vec3d position = clampedHermite(
                    curveP0,
                    first.localPositionM,
                    second.localPositionM,
                    curveP3,
                    ratio,
                    curveDeviation);
                statistics.maximumCurveDeviationM = std::max(
                    statistics.maximumCurveDeviationM, curveDeviation);
                const std::uint32_t ordinal = static_cast<std::uint32_t>(
                    (index & 0xffffu) * 16u + step);
                appendLayer(candidates,
                            statistics,
                            first,
                            second,
                            position,
                            trailTangent,
                            right,
                            up,
                            ratio,
                            centreSpacingM,
                            ordinal,
                            ContrailRenderLayer::Core,
                            settings);
                if (mix(first.ageSeconds, second.ageSeconds, ratio) > 1.25f) {
                    appendLayer(candidates,
                                statistics,
                                first,
                                second,
                                position,
                                trailTangent,
                                right,
                                up,
                                ratio,
                                centreSpacingM,
                                ordinal,
                                ContrailRenderLayer::Halo,
                                settings);
                }
            }
        }
    }

    statistics.generatedSampleCount = candidates.size();
    const std::size_t totalAssetCapacity = std::accumulate(
        settings.assetCapacities.begin(), settings.assetCapacities.end(), std::size_t {0});
    const std::size_t capacity = std::min(settings.visibleCapacity, totalAssetCapacity);
    if (capacity == 0 || candidates.empty()) {
        statistics.capacityRejectedCount = candidates.size();
        return result;
    }

    constexpr std::size_t kQueueCount = engine::kMaximumRecordedEngines * 2u;
    std::array<std::vector<std::size_t>, kQueueCount> queues;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& sample = candidates[index].sample;
        queues[queueIndex(sample.engineIndex, sample.layer)].push_back(index);
    }
    for (auto& queue : queues) {
        std::stable_sort(queue.begin(), queue.end(), [&](std::size_t a, std::size_t b) {
            const auto& first = candidates[a].sample;
            const auto& second = candidates[b].sample;
            if (first.ageSeconds != second.ageSeconds) return first.ageSeconds < second.ageSeconds;
            if (first.nearField != second.nearField) return first.nearField;
            if (first.priority != second.priority) return first.priority > second.priority;
            return first.renderId < second.renderId;
        });
    }

    std::array<std::size_t, kQueueCount> cursors {};
    std::array<bool, kQueueCount> closed {};
    std::array<bool, kQueueCount> hasLast {};
    std::array<engine::Vec3d, kQueueCount> lastPosition {};
    std::array<std::size_t, kContrailRenderAssetCount> assetUsage {};
    std::vector<ContrailRenderSample> selected;
    selected.reserve(std::min(capacity, candidates.size()));

    auto selectAsset = [&](ContrailRenderSample& sample) -> bool {
        const int originalBucket = static_cast<int>(sample.opacityBucket);
        static constexpr std::array<int, 7> bucketOffsets {0, -1, 1, -2, 2, -3, 3};
        for (const int offset : bucketOffsets) {
            const int bucket = originalBucket + offset;
            if (bucket < 0 || bucket >= static_cast<int>(kContrailOpacityBucketCount)) continue;
            const std::size_t preferred = sample.textureVariant % kContrailTextureVariantCount;
            for (std::size_t variantPass = 0; variantPass < 2; ++variantPass) {
                const std::size_t variant = variantPass == 0 ? preferred : 1u - preferred;
                const std::size_t assetIndex =
                    static_cast<std::size_t>(bucket) * kContrailTextureVariantCount + variant;
                if (assetUsage[assetIndex] >= settings.assetCapacities[assetIndex]) continue;
                if (bucket != originalBucket) ++statistics.assetBucketRemapCount;
                sample.opacityBucket = static_cast<std::uint8_t>(bucket);
                sample.textureVariant = static_cast<std::uint8_t>(variant);
                ++assetUsage[assetIndex];
                return true;
            }
        }
        ++statistics.assetCapacityRejectedCount;
        return false;
    };

    auto chooseNext = [&](std::size_t queueId) -> bool {
        if (closed[queueId]) return false;
        auto& queue = queues[queueId];
        auto& cursor = cursors[queueId];
        while (cursor < queue.size()) {
            ContrailRenderSample sample = candidates[queue[cursor++]].sample;
            if (hasLast[queueId]) {
                const double spacing = distance(lastPosition[queueId], sample.localPositionM);
                if (!finite(spacing) || spacing > settings.maximumSelectedSpacingM) {
                    statistics.continuityTrimmedCount += queue.size() - (cursor - 1u);
                    closed[queueId] = true;
                    return false;
                }
                statistics.maximumSelectedSpacingM = std::max(
                    statistics.maximumSelectedSpacingM, spacing);
            }
            if (!selectAsset(sample)) continue;
            selected.push_back(sample);
            hasLast[queueId] = true;
            lastPosition[queueId] = sample.localPositionM;
            return true;
        }
        closed[queueId] = true;
        return false;
    };

    while (selected.size() < capacity) {
        bool progress = false;
        for (std::uint32_t engineIndex = 0;
             engineIndex < engine::kMaximumRecordedEngines && selected.size() < capacity;
             ++engineIndex) {
            const std::size_t coreQueue = queueIndex(engineIndex, ContrailRenderLayer::Core);
            const std::size_t haloQueue = queueIndex(engineIndex, ContrailRenderLayer::Halo);
            progress = chooseNext(coreQueue) || progress;
            if (selected.size() >= capacity) break;
            progress = chooseNext(haloQueue) || progress;
        }
        if (!progress) break;
    }

    result.samples = std::move(selected);
    std::stable_sort(result.samples.begin(), result.samples.end(), [](const auto& a, const auto& b) {
        if (a.engineIndex != b.engineIndex) return a.engineIndex < b.engineIndex;
        if (a.ageSeconds != b.ageSeconds) return a.ageSeconds > b.ageSeconds;
        if (a.layer != b.layer) return a.layer == ContrailRenderLayer::Core;
        return a.renderId < b.renderId;
    });

    statistics.selectedSampleCount = result.samples.size();
    statistics.capacityRejectedCount = candidates.size() - result.samples.size();
    for (const auto& sample : result.samples) {
        const std::size_t assetIndex =
            static_cast<std::size_t>(sample.opacityBucket) * kContrailTextureVariantCount +
            static_cast<std::size_t>(sample.textureVariant);
        ++statistics.selectedByAsset[assetIndex];
        if (sample.nearField) ++statistics.selectedNearFieldCount;
        if (sample.layer == ContrailRenderLayer::Core) {
            ++statistics.selectedCoreCount;
        } else {
            ++statistics.selectedHaloCount;
        }
    }

    std::uint64_t hash = kFnvOffset;
    for (const auto& sample : result.samples) hashSample(hash, sample);
    statistics.deterministicHash = hash;
    return result;
}

}  // namespace ffatmo::render
