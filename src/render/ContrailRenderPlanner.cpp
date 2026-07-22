#include "render/ContrailRenderPlanner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <tuple>
#include <vector>

namespace ffatmo::render {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr double kPi = 3.14159265358979323846;

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

engine::Vec3d lerp(const engine::Vec3d& a, const engine::Vec3d& b, double t) {
    return add(a, multiply(subtract(b, a), t));
}

float mix(float a, float b, float t) {
    return a + (b - a) * t;
}

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float piecewiseRadius(float ageSeconds,
                      float age0,
                      float value0,
                      float age1,
                      float value1) {
    if (ageSeconds <= age0) return value0;
    if (ageSeconds >= age1) return value1;
    const float ratio = (ageSeconds - age0) / (age1 - age0);
    return mix(value0, value1, ratio);
}

float coreRadiusForAge(float ageSeconds, float physicsRadiusM) {
    float radius = 0.30f;
    if (ageSeconds <= 2.0f) {
        radius = piecewiseRadius(ageSeconds, 0.0f, 0.30f, 2.0f, 0.75f);
    } else if (ageSeconds <= 8.0f) {
        radius = piecewiseRadius(ageSeconds, 2.0f, 0.75f, 8.0f, 1.60f);
    } else if (ageSeconds <= 20.0f) {
        radius = piecewiseRadius(ageSeconds, 8.0f, 1.60f, 20.0f, 3.20f);
    } else {
        radius = piecewiseRadius(ageSeconds, 20.0f, 3.20f, 60.0f, 6.50f);
    }
    const float physicsFactor = std::clamp(
        std::sqrt(std::max(physicsRadiusM, 0.05f) / 1.25f), 0.82f, 1.18f);
    return std::clamp(radius * physicsFactor, 0.25f, 7.0f);
}

float haloRadiusForAge(float ageSeconds, float coreRadiusM) {
    if (ageSeconds <= 2.0f) return 0.0f;
    float radius = 1.20f;
    if (ageSeconds <= 8.0f) {
        radius = piecewiseRadius(ageSeconds, 2.0f, 1.20f, 8.0f, 2.40f);
    } else if (ageSeconds <= 20.0f) {
        radius = piecewiseRadius(ageSeconds, 8.0f, 2.40f, 20.0f, 5.50f);
    } else {
        radius = piecewiseRadius(ageSeconds, 20.0f, 5.50f, 60.0f, 12.0f);
    }
    return std::clamp(std::max(radius, coreRadiusM * 1.45f), 1.20f, 12.0f);
}

float targetSpacingForAge(float ageSeconds, float coreRadiusM) {
    float spacing = 0.45f;
    if (ageSeconds <= 2.0f) {
        spacing = piecewiseRadius(ageSeconds, 0.0f, 0.45f, 2.0f, 0.80f);
    } else if (ageSeconds <= 8.0f) {
        spacing = piecewiseRadius(ageSeconds, 2.0f, 0.75f, 8.0f, 1.30f);
    } else if (ageSeconds <= 20.0f) {
        spacing = piecewiseRadius(ageSeconds, 8.0f, 1.20f, 20.0f, 2.20f);
    } else {
        spacing = piecewiseRadius(ageSeconds, 20.0f, 2.0f, 60.0f, 4.0f);
    }
    return std::max(0.30f, std::min(spacing, coreRadiusM * 1.50f));
}

float jitterFractionForAge(float ageSeconds) {
    if (ageSeconds <= 2.0f) return 0.03f;
    if (ageSeconds <= 20.0f) {
        return piecewiseRadius(ageSeconds, 2.0f, 0.08f, 20.0f, 0.18f);
    }
    return piecewiseRadius(ageSeconds, 20.0f, 0.18f, 60.0f, 0.35f);
}

std::uint64_t mixId(std::uint64_t a,
                    std::uint64_t b,
                    std::uint32_t ordinal,
                    ContrailRenderLayer layer) {
    std::uint64_t value = a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6u) + (a >> 2u));
    value ^= static_cast<std::uint64_t>(ordinal) * 0xbf58476d1ce4e5b9ull;
    value ^= static_cast<std::uint64_t>(layer == ContrailRenderLayer::Halo ? 0x94d049bb133111ebull
                                                                          : 0x2545f4914f6cdd1dull);
    value ^= value >> 30u;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27u;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31u;
    return value;
}

std::uint8_t opacityBucket(float strength) {
    if (strength < 0.025f) return 0;
    if (strength < 0.055f) return 1;
    if (strength < 0.100f) return 2;
    return 3;
}

float candidatePriority(const ContrailRenderSample& sample) {
    float score = sample.opacityStrength * 1000.0f;
    score += std::max(0.0f, 60.0f - sample.ageSeconds) * 8.0f;
    if (sample.layer == ContrailRenderLayer::Core) score += 1200.0f;
    if (sample.ageSeconds <= 8.0f) score += 2000.0f;
    if (sample.layer == ContrailRenderLayer::Core && sample.ageSeconds <= 8.0f) {
        score += 5000.0f;
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
                            double t,
                            double& deviationM) {
    const double segmentLength = std::max(distance(p1, p2), 1.0e-6);
    engine::Vec3d tangent1 = multiply(subtract(p2, p0), 0.5);
    engine::Vec3d tangent2 = multiply(subtract(p3, p1), 0.5);
    const double tangentLimit = segmentLength * 1.25;
    if (length(tangent1) > tangentLimit) tangent1 = multiply(normalized(tangent1), tangentLimit);
    if (length(tangent2) > tangentLimit) tangent2 = multiply(normalized(tangent2), tangentLimit);

    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + t;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;
    engine::Vec3d curve = add(
        add(multiply(p1, h00), multiply(tangent1, h10)),
        add(multiply(p2, h01), multiply(tangent2, h11)));

    const engine::Vec3d linear = lerp(p1, p2, t);
    engine::Vec3d deviation = subtract(curve, linear);
    deviationM = length(deviation);
    const double maximumDeviation = segmentLength * 0.25;
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
    hashValue(hash, sample.radiusM);
    hashValue(hash, sample.opacityStrength);
    hashValue(hash, sample.ageSeconds);
    hashValue(hash, sample.opacityBucket);
    hashValue(hash, sample.textureVariant);
    const auto layer = static_cast<std::uint8_t>(sample.layer);
    hashValue(hash, layer);
}

void appendLayer(std::vector<Candidate>& candidates,
                 ContrailRenderPlannerStatistics& statistics,
                 const ContrailRenderInput& first,
                 const ContrailRenderInput& second,
                 const engine::Vec3d& centrelinePosition,
                 const engine::Vec3d& right,
                 const engine::Vec3d& up,
                 float ratio,
                 std::uint32_t ordinal,
                 ContrailRenderLayer layer,
                 const ContrailRenderPlannerSettings& settings) {
    const float age = mix(first.ageSeconds, second.ageSeconds, ratio);
    const float physicsRadius = mix(first.physicsRadiusM, second.physicsRadiusM, ratio);
    const float optical = mix(first.opticalDepth, second.opticalDepth, ratio);
    const float iceMass = mix(first.normalizedIceMass, second.normalizedIceMass, ratio);
    (void)iceMass;

    const float spawnRamp = clamp01(age / 0.35f);
    const float dryAgeFade = clamp01(1.0f - std::max(0.0f, age - 45.0f) / 25.0f);
    const float layerFactor = layer == ContrailRenderLayer::Core
        ? 1.0f
        : mix(0.22f, 0.38f, clamp01((age - 2.0f) / 30.0f));
    const float strength = optical * spawnRamp * dryAgeFade * layerFactor;
    if (!finite(strength) || strength < settings.minimumOpacityStrength) return;

    const float coreRadius = coreRadiusForAge(age, physicsRadius);
    const float radius = layer == ContrailRenderLayer::Core
        ? coreRadius
        : haloRadiusForAge(age, coreRadius);
    if (layer == ContrailRenderLayer::Halo && radius <= 0.0f) return;

    const std::uint64_t renderId = mixId(
        first.sourceParcelId, second.sourceParcelId, ordinal, layer);
    const float phase = static_cast<float>(
        age * 0.43 + static_cast<double>(first.engineIndex) * 1.731 +
        static_cast<double>((renderId >> 12u) & 0xffu) * 0.0025);

    engine::Vec3d position = centrelinePosition;
    if (layer == ContrailRenderLayer::Core) {
        const float amplitude = radius * jitterFractionForAge(age);
        position = add(position, multiply(right, amplitude * std::sin(phase)));
        position = add(position, multiply(up, amplitude * 0.28 * std::cos(phase * 0.73)));
    } else {
        const float growth = clamp01((age - 2.0f) / 35.0f);
        const float amplitude = radius * 0.35f * growth;
        position = add(position, multiply(right, amplitude * std::sin(phase * 0.61f)));
        position = add(position, multiply(up, amplitude * 0.24 * std::cos(phase * 0.47f)));
    }

    ContrailRenderSample sample;
    sample.renderId = renderId;
    sample.sourceParcelId = ratio < 0.5f ? first.sourceParcelId : second.sourceParcelId;
    sample.engineIndex = first.engineIndex;
    sample.localPositionM = position;
    sample.radiusM = radius;
    sample.opacityStrength = strength;
    sample.ageSeconds = age;
    sample.opacityBucket = opacityBucket(strength);
    if (layer == ContrailRenderLayer::Halo && sample.opacityBucket > 0) {
        --sample.opacityBucket;
    }
    sample.textureVariant = static_cast<std::uint8_t>(
        (renderId ^ (static_cast<std::uint64_t>(first.engineIndex) * 0x9e3779b9u)) & 1u);
    sample.layer = layer;
    sample.priority = candidatePriority(sample);
    candidates.push_back({sample});
    if (layer == ContrailRenderLayer::Core) {
        ++statistics.generatedCoreCount;
    } else {
        ++statistics.generatedHaloCount;
    }
}

bool candidateBefore(const Candidate& a, const Candidate& b) {
    if (a.sample.priority != b.sample.priority) return a.sample.priority > b.sample.priority;
    if (a.sample.ageSeconds != b.sample.ageSeconds) return a.sample.ageSeconds < b.sample.ageSeconds;
    if (a.sample.engineIndex != b.sample.engineIndex) return a.sample.engineIndex < b.sample.engineIndex;
    if (a.sample.renderId != b.sample.renderId) return a.sample.renderId < b.sample.renderId;
    return static_cast<std::uint8_t>(a.sample.layer) < static_cast<std::uint8_t>(b.sample.layer);
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
    candidates.reserve(statistics.validParcelCount * 12);

    for (std::size_t engineIndex = 0; engineIndex < streams.size(); ++engineIndex) {
        auto& stream = streams[engineIndex];
        if (stream.empty()) continue;
        std::stable_sort(stream.begin(), stream.end(), [](const auto& a, const auto& b) {
            if (a.ageSeconds != b.ageSeconds) return a.ageSeconds > b.ageSeconds;
            return a.sourceParcelId < b.sourceParcelId;
        });

        if (stream.size() == 1) {
            appendLayer(candidates,
                        statistics,
                        stream.front(),
                        stream.front(),
                        stream.front().localPositionM,
                        {1.0, 0.0, 0.0},
                        {0.0, 1.0, 0.0},
                        0.0f,
                        0,
                        ContrailRenderLayer::Core,
                        settings);
            appendLayer(candidates,
                        statistics,
                        stream.front(),
                        stream.front(),
                        stream.front().localPositionM,
                        {1.0, 0.0, 0.0},
                        {0.0, 1.0, 0.0},
                        0.0f,
                        0,
                        ContrailRenderLayer::Halo,
                        settings);
            continue;
        }

        bool startOfSubstream = true;
        for (std::size_t index = 0; index + 1 < stream.size(); ++index) {
            const auto& first = stream[index];
            const auto& second = stream[index + 1];
            const double gapM = distance(first.localPositionM, second.localPositionM);
            const float ageGap = std::abs(first.ageSeconds - second.ageSeconds);
            const bool continuous = finite(gapM) && gapM <= settings.maximumSegmentGapM &&
                                    ageGap <= settings.maximumAgeGapSeconds;
            if (!continuous) {
                ++statistics.streamBreakCount;
                startOfSubstream = true;
                continue;
            }

            const float meanAge = 0.5f * (first.ageSeconds + second.ageSeconds);
            const float meanPhysicsRadius = 0.5f * (first.physicsRadiusM + second.physicsRadiusM);
            const float coreRadius = coreRadiusForAge(meanAge, meanPhysicsRadius);
            const float targetSpacing = targetSpacingForAge(meanAge, coreRadius);
            const std::size_t subdivisions = std::clamp<std::size_t>(
                static_cast<std::size_t>(std::ceil(gapM / std::max(targetSpacing, 0.01f))),
                1,
                std::max<std::size_t>(1, settings.maximumSamplesPerSegment));

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
            const engine::Vec3d tangent = normalized(subtract(second.localPositionM,
                                                               first.localPositionM),
                                                      {0.0, 0.0, -1.0});
            engine::Vec3d right = normalized(cross(tangent, {0.0, 1.0, 0.0}), {1.0, 0.0, 0.0});
            engine::Vec3d up = normalized(cross(right, tangent), {0.0, 1.0, 0.0});

            const std::size_t firstStep = startOfSubstream ? 0 : 1;
            for (std::size_t step = firstStep; step <= subdivisions; ++step) {
                const float ratio = static_cast<float>(step) /
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
                    (index & 0xffffu) * 32u + step);
                appendLayer(candidates,
                            statistics,
                            first,
                            second,
                            position,
                            right,
                            up,
                            ratio,
                            ordinal,
                            ContrailRenderLayer::Core,
                            settings);
                if (mix(first.ageSeconds, second.ageSeconds, ratio) > 2.0f) {
                    appendLayer(candidates,
                                statistics,
                                first,
                                second,
                                position,
                                right,
                                up,
                                ratio,
                                ordinal,
                                ContrailRenderLayer::Halo,
                                settings);
                }
            }
            startOfSubstream = false;
        }
    }

    statistics.generatedSampleCount = candidates.size();
    const std::size_t capacity = settings.visibleCapacity;
    if (capacity == 0 || candidates.empty()) {
        statistics.capacityRejectedCount = candidates.size();
        return result;
    }

    std::vector<std::size_t> allIndices(candidates.size());
    std::iota(allIndices.begin(), allIndices.end(), 0);
    std::stable_sort(allIndices.begin(), allIndices.end(), [&](std::size_t a, std::size_t b) {
        return candidateBefore(candidates[a], candidates[b]);
    });

    std::vector<std::uint8_t> chosen(candidates.size(), 0);
    std::vector<std::size_t> selected;
    selected.reserve(std::min(capacity, candidates.size()));
    auto choose = [&](std::size_t index) {
        if (selected.size() >= capacity || chosen[index]) return false;
        chosen[index] = 1;
        selected.push_back(index);
        return true;
    };

    std::vector<std::uint32_t> activeEngines;
    for (std::size_t engineIndex = 0; engineIndex < streams.size(); ++engineIndex) {
        if (!streams[engineIndex].empty()) activeEngines.push_back(static_cast<std::uint32_t>(engineIndex));
    }

    const std::size_t youngCoreTarget = std::min(
        capacity,
        static_cast<std::size_t>(std::floor(static_cast<double>(capacity) * 0.35)));
    std::array<std::vector<std::size_t>, engine::kMaximumRecordedEngines> youngByEngine;
    std::array<std::vector<std::size_t>, engine::kMaximumRecordedEngines> allByEngine;
    for (const std::size_t index : allIndices) {
        const auto& sample = candidates[index].sample;
        allByEngine[sample.engineIndex].push_back(index);
        if (sample.layer == ContrailRenderLayer::Core && sample.ageSeconds <= 8.0f) {
            youngByEngine[sample.engineIndex].push_back(index);
        }
    }

    std::array<std::size_t, engine::kMaximumRecordedEngines> youngCursor {};
    while (selected.size() < youngCoreTarget) {
        bool progress = false;
        for (const std::uint32_t engineIndex : activeEngines) {
            auto& list = youngByEngine[engineIndex];
            auto& cursor = youngCursor[engineIndex];
            while (cursor < list.size() && chosen[list[cursor]]) ++cursor;
            if (cursor < list.size()) {
                progress = choose(list[cursor]) || progress;
                ++cursor;
            }
            if (selected.size() >= youngCoreTarget) break;
        }
        if (!progress) break;
    }

    if (!activeEngines.empty() && selected.size() < capacity) {
        const std::size_t twentyPercent = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::floor(static_cast<double>(capacity) * 0.20)));
        const std::size_t fairShare = std::max<std::size_t>(1, capacity / activeEngines.size());
        const std::size_t perEngineQuota = std::min(twentyPercent, fairShare);
        for (const std::uint32_t engineIndex : activeEngines) {
            std::size_t already = 0;
            for (const std::size_t index : selected) {
                if (candidates[index].sample.engineIndex == engineIndex) ++already;
            }
            for (const std::size_t index : allByEngine[engineIndex]) {
                if (already >= perEngineQuota || selected.size() >= capacity) break;
                if (choose(index)) ++already;
            }
        }
    }

    for (const std::size_t index : allIndices) {
        if (selected.size() >= capacity) break;
        choose(index);
    }

    result.samples.reserve(selected.size());
    for (const std::size_t index : selected) result.samples.push_back(candidates[index].sample);
    std::stable_sort(result.samples.begin(), result.samples.end(), [](const auto& a, const auto& b) {
        if (a.engineIndex != b.engineIndex) return a.engineIndex < b.engineIndex;
        if (a.ageSeconds != b.ageSeconds) return a.ageSeconds > b.ageSeconds;
        if (a.renderId != b.renderId) return a.renderId < b.renderId;
        return static_cast<std::uint8_t>(a.layer) < static_cast<std::uint8_t>(b.layer);
    });

    statistics.selectedSampleCount = result.samples.size();
    statistics.capacityRejectedCount = candidates.size() - result.samples.size();
    for (const auto& sample : result.samples) {
        if (sample.layer == ContrailRenderLayer::Core) {
            ++statistics.selectedCoreCount;
        } else {
            ++statistics.selectedHaloCount;
        }
    }

    std::array<const ContrailRenderSample*, engine::kMaximumRecordedEngines> previousSelected {};
    for (const auto& sample : result.samples) {
        const auto* previous = previousSelected[sample.engineIndex];
        if (previous && previous->layer == sample.layer) {
            statistics.maximumSelectedSpacingM = std::max(
                statistics.maximumSelectedSpacingM,
                distance(previous->localPositionM, sample.localPositionM));
        }
        previousSelected[sample.engineIndex] = &sample;
    }

    std::uint64_t hash = kFnvOffset;
    for (const auto& sample : result.samples) hashSample(hash, sample);
    statistics.deterministicHash = hash;
    return result;
}

}  // namespace ffatmo::render
