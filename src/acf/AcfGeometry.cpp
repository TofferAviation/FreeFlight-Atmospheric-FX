#include "acf/AcfGeometry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>

namespace ffatmo::acf {
namespace {

constexpr std::uint64_t kMaxFileBytes = 64ull * 1024ull * 1024ull;
constexpr std::uint64_t kMaxProperties = 1000000ull;
constexpr std::size_t kMaxPathBytes = 1024;
constexpr std::size_t kMaxValueBytes = 64ull * 1024ull;
constexpr double kFeetToMetres = 0.3048;
constexpr double kPoundsToKg = 0.45359237;
constexpr double kPi = 3.14159265358979323846;

struct PropertyValue {
    std::string value;
    std::uint32_t line = 0;
};

using PropertyStore = std::unordered_map<std::string, PropertyValue>;

void addDiagnostic(ParseResult& result,
                   Diagnostic::Severity severity,
                   std::string code,
                   std::string message,
                   std::uint32_t line = 0,
                   std::string path = {}) {
    result.diagnostics.push_back(
        Diagnostic{severity, std::move(code), std::move(message), line, std::move(path)});
}

std::string trimCarriageReturn(std::string value) {
    if (!value.empty() && value.back() == '\r') value.pop_back();
    return value;
}

std::optional<double> parseDouble(const PropertyStore& store,
                                  const std::string& path,
                                  ParseResult& result,
                                  bool required = false) {
    const auto it = store.find(path);
    if (it == store.end()) {
        if (required) {
            addDiagnostic(result, Diagnostic::Severity::Error, "missing_property",
                          "Required ACF property is missing", 0, path);
        }
        return std::nullopt;
    }

    try {
        std::size_t used = 0;
        const double value = std::stod(it->second.value, &used);
        if (used != it->second.value.size() || !std::isfinite(value)) {
            throw std::invalid_argument("not finite or trailing characters");
        }
        return value;
    } catch (...) {
        addDiagnostic(result, Diagnostic::Severity::Error, "invalid_number",
                      "ACF property is not a finite number", it->second.line, path);
        return std::nullopt;
    }
}

std::optional<int> parseInt(const PropertyStore& store,
                            const std::string& path,
                            ParseResult& result,
                            bool required = false) {
    const auto value = parseDouble(store, path, result, required);
    if (!value) return std::nullopt;
    if (*value < static_cast<double>(std::numeric_limits<int>::min()) ||
        *value > static_cast<double>(std::numeric_limits<int>::max())) {
        addDiagnostic(result, Diagnostic::Severity::Error, "integer_range",
                      "ACF integer property is outside the supported range", 0, path);
        return std::nullopt;
    }
    return static_cast<int>(std::llround(*value));
}

std::string parseString(const PropertyStore& store,
                        const std::string& path,
                        const std::string& fallback = {}) {
    const auto it = store.find(path);
    return it == store.end() ? fallback : it->second.value;
}

Vec3 add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 normalize(const Vec3& value) {
    const double length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 1.0e-9) return {0.0, 0.0, 1.0};
    return {value.x / length, value.y / length, value.z / length};
}

Vec3 rotateEngineLocal(const Vec3& local, double yawDeg, double pitchDeg) {
    const double yaw = yawDeg * kPi / 180.0;
    const double pitch = pitchDeg * kPi / 180.0;
    const Vec3 yawed {
        std::cos(yaw) * local.x + std::sin(yaw) * local.z,
        local.y,
        -std::sin(yaw) * local.x + std::cos(yaw) * local.z
    };
    return {
        yawed.x,
        std::cos(pitch) * yawed.y - std::sin(pitch) * yawed.z,
        std::sin(pitch) * yawed.y + std::cos(pitch) * yawed.z
    };
}

Vec3 sourcePointToBody(double xFt, double yFt, double zFt, double cgYFt, double cgZFt) {
    return {
        xFt * kFeetToMetres,
        (yFt - cgYFt) * kFeetToMetres,
        (zFt - cgZFt) * kFeetToMetres
    };
}

struct RawWing {
    int index = -1;
    bool left = false;
    double xFt = 0.0;
    double yFt = 0.0;
    double zFt = 0.0;
    double semiFt = 0.0;
    double rootChordFt = 0.0;
    double tipChordFt = 0.0;
    double sweepDeg = 0.0;
    double dihedralDeg = 0.0;
};

bool approximatelyMirrored(const RawWing& left, const RawWing& right) {
    const double spanScale = std::max({1.0, std::abs(left.semiFt), std::abs(right.semiFt)});
    return left.xFt <= 0.1 && right.xFt >= -0.1 &&
           std::abs(std::abs(left.xFt) - std::abs(right.xFt)) < 0.08 * spanScale + 0.35 &&
           std::abs(left.yFt - right.yFt) < 0.35 &&
           std::abs(left.zFt - right.zFt) < 0.35 &&
           std::abs(left.semiFt - right.semiFt) < 0.35 &&
           std::abs(left.rootChordFt - right.rootChordFt) < 0.35;
}

Vec3 analyticWingTip(const RawWing& wing, double cgYFt, double cgZFt) {
    const double direction = wing.left ? -1.0 : 1.0;
    const double dihedral = wing.dihedralDeg * kPi / 180.0;
    const double sweep = wing.sweepDeg * kPi / 180.0;
    const double lateralFt = std::cos(dihedral) * wing.semiFt;
    const double verticalFt = std::sin(dihedral) * wing.semiFt;
    const double aftFt = std::tan(sweep) * std::abs(lateralFt);
    return sourcePointToBody(
        wing.xFt + direction * lateralFt,
        wing.yFt + verticalFt,
        wing.zFt + aftFt,
        cgYFt,
        cgZFt);
}

std::vector<std::pair<RawWing, RawWing>> selectMainWingPairs(
    std::vector<std::pair<RawWing, RawWing>> pairs) {
    if (pairs.empty()) return {};

    auto startIt = std::max_element(
        pairs.begin(), pairs.end(),
        [](const auto& a, const auto& b) {
            const double scoreA =
                (std::abs(a.first.xFt) < 1.0 ? 2.0 : 1.0) *
                a.first.semiFt * std::max(0.0, a.first.rootChordFt);
            const double scoreB =
                (std::abs(b.first.xFt) < 1.0 ? 2.0 : 1.0) *
                b.first.semiFt * std::max(0.0, b.first.rootChordFt);
            return scoreA < scoreB;
        });

    std::vector<std::pair<RawWing, RawWing>> chain;
    chain.push_back(*startIt);
    pairs.erase(startIt);

    while (!pairs.empty()) {
        const RawWing& previous = chain.back().first;
        auto best = pairs.end();
        double bestScore = std::numeric_limits<double>::max();

        for (auto it = pairs.begin(); it != pairs.end(); ++it) {
            const RawWing& candidate = it->first;
            const double outward = std::abs(candidate.xFt) - std::abs(previous.xFt);
            if (outward <= 0.05 || outward > previous.semiFt * 1.60 + 4.0) continue;

            const Vec3 predicted = analyticWingTip(previous, 0.0, 0.0);
            const Vec3 candidateSource {
                candidate.xFt * kFeetToMetres,
                candidate.yFt * kFeetToMetres,
                candidate.zFt * kFeetToMetres
            };
            const double dx = predicted.x - candidateSource.x;
            const double dy = predicted.y - candidateSource.y;
            const double dz = predicted.z - candidateSource.z;
            const double distanceM = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double chordPenalty =
                std::abs(previous.tipChordFt - candidate.rootChordFt) * kFeetToMetres;
            const double score = distanceM + chordPenalty * 0.50;

            if (score < bestScore && score < std::max(5.0, previous.semiFt * kFeetToMetres * 0.70)) {
                best = it;
                bestScore = score;
            }
        }

        if (best == pairs.end()) break;
        chain.push_back(*best);
        pairs.erase(best);
    }

    return chain;
}

std::string severityText(Diagnostic::Severity severity) {
    switch (severity) {
        case Diagnostic::Severity::Info: return "INFO";
        case Diagnostic::Severity::Warning: return "WARNING";
        case Diagnostic::Severity::Error: return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace

ParseResult parseAcfText(const std::string& text, const std::string& sourceName) {
    const auto started = std::chrono::steady_clock::now();
    ParseResult result;
    result.sourceBytes = static_cast<std::uint64_t>(text.size());
    result.profile.sourcePath = sourceName;

    if (text.size() > kMaxFileBytes) {
        addDiagnostic(result, Diagnostic::Severity::Error, "file_too_large",
                      "ACF file exceeds the 64 MB safety limit");
        return result;
    }

    PropertyStore properties;
    std::istringstream input(text);
    std::string line;
    std::vector<std::string> headerLines;
    std::uint32_t lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;
        line = trimCarriageReturn(std::move(line));
        if (headerLines.size() < 3) {
            if (!line.empty()) headerLines.push_back(line);
            if (headerLines.size() <= 3) continue;
        }

        if (line == "PROPERTIES_END") break;
        if (line.empty() || line == "PROPERTIES_BEGIN") continue;
        if (line.rfind("P ", 0) != 0) {
            addDiagnostic(result, Diagnostic::Severity::Warning, "unknown_line",
                          "Ignored non-property ACF line", lineNumber);
            continue;
        }

        const std::size_t pathStart = 2;
        const std::size_t separator = line.find(' ', pathStart);
        if (separator == std::string::npos) {
            addDiagnostic(result, Diagnostic::Severity::Warning, "malformed_property",
                          "Ignored property without a value", lineNumber);
            continue;
        }

        std::string path = line.substr(pathStart, separator - pathStart);
        std::string value = line.substr(separator + 1);
        if (path.size() > kMaxPathBytes || value.size() > kMaxValueBytes) {
            addDiagnostic(result, Diagnostic::Severity::Error, "record_too_large",
                          "ACF property exceeds a parser safety limit", lineNumber, path);
            continue;
        }
        if (properties.size() >= kMaxProperties) {
            addDiagnostic(result, Diagnostic::Severity::Error, "too_many_properties",
                          "ACF file exceeds the property-count safety limit", lineNumber);
            break;
        }

        const auto [it, inserted] =
            properties.emplace(std::move(path), PropertyValue{std::move(value), lineNumber});
        if (!inserted) {
            addDiagnostic(result, Diagnostic::Severity::Warning, "duplicate_property",
                          "Duplicate ACF property; the first value was retained",
                          lineNumber, it->first);
        }
    }

    result.propertyCount = static_cast<std::uint64_t>(properties.size());
    if (headerLines.size() < 3 || headerLines[0] != "I" ||
        headerLines[1] != "1200 Version" || headerLines[2] != "ACF") {
        addDiagnostic(result, Diagnostic::Severity::Error, "unsupported_header",
                      "Expected an X-Plane ACF file with header I / 1200 Version / ACF");
        return result;
    }
    result.profile.acfFormatVersion = 1200;

    result.profile.aircraftName = parseString(properties, "acf/_name", "Unnamed aircraft");
    result.profile.aircraftIcao = parseString(properties, "acf/_ICAO");
    result.profile.cgYSourceFt = parseDouble(properties, "acf/_cgY", result).value_or(0.0);
    result.profile.cgZSourceFt = parseDouble(properties, "acf/_cgZ", result).value_or(0.0);
    result.profile.emptyMassKg =
        parseDouble(properties, "acf/_m_empty", result).value_or(0.0) * kPoundsToKg;
    result.profile.maximumMassKg =
        parseDouble(properties, "acf/_m_max", result).value_or(0.0) * kPoundsToKg;

    const int engineCapacity =
        std::clamp(parseInt(properties, "_engn/count", result).value_or(16), 0, 64);
    for (int index = 0; index < engineCapacity; ++index) {
        const std::string prefix = "_engn/" + std::to_string(index) + "/";
        const std::string type = parseString(properties, prefix + "_type");
        if (type.empty() || type == "NONE" || type == "INVALID") continue;

        const auto x = parseDouble(properties, prefix + "_part_x", result);
        const auto y = parseDouble(properties, prefix + "_part_y", result);
        const auto z = parseDouble(properties, prefix + "_part_z", result);
        if (!x || !y || !z) {
            addDiagnostic(result, Diagnostic::Severity::Warning, "engine_incomplete",
                          "Active engine lacks a complete centre position", 0, prefix);
            continue;
        }

        EngineGeometry engine;
        engine.sourceIndex = index;
        engine.type = type;
        engine.yawDeg = parseDouble(properties, prefix + "_part_psi", result).value_or(0.0);
        engine.pitchDeg = parseDouble(properties, prefix + "_part_the", result).value_or(0.0);
        engine.maximumThrustN =
            parseDouble(properties, prefix + "_thrust_max_limit", result).value_or(0.0);
        engine.centreBodyM = sourcePointToBody(
            *x, *y, *z, result.profile.cgYSourceFt, result.profile.cgZSourceFt);

        const Vec3 exhaustLocalM {
            parseDouble(properties, prefix + "_exhaust_os_xyz/0", result).value_or(0.0) *
                kFeetToMetres,
            parseDouble(properties, prefix + "_exhaust_os_xyz/1", result).value_or(0.0) *
                kFeetToMetres,
            parseDouble(properties, prefix + "_exhaust_os_xyz/2", result).value_or(0.0) *
                kFeetToMetres
        };
        engine.exhaustOriginBodyM =
            add(engine.centreBodyM, rotateEngineLocal(exhaustLocalM, engine.yawDeg, engine.pitchDeg));
        engine.exhaustDirectionBody =
            normalize(rotateEngineLocal({0.0, 0.0, 1.0}, engine.yawDeg, engine.pitchDeg));

        result.profile.engines.push_back(std::move(engine));
    }

    const int wingCapacity =
        std::clamp(parseInt(properties, "_wing/count", result).value_or(0), 0, 512);
    std::vector<RawWing> rawWings;
    rawWings.reserve(static_cast<std::size_t>(wingCapacity));

    for (int index = 0; index < wingCapacity; ++index) {
        const std::string prefix = "_wing/" + std::to_string(index) + "/";
        const auto semi = parseDouble(properties, prefix + "_semilen_SEG", result);
        const auto rootChord = parseDouble(properties, prefix + "_Croot", result);
        const auto tipChord = parseDouble(properties, prefix + "_Ctip", result);
        const auto x = parseDouble(properties, prefix + "_part_x", result);
        const auto y = parseDouble(properties, prefix + "_part_y", result);
        const auto z = parseDouble(properties, prefix + "_part_z", result);
        if (!semi || !rootChord || !tipChord || !x || !y || !z ||
            *semi <= 0.01 || *rootChord <= 0.01 || *tipChord <= 0.0) {
            continue;
        }

        RawWing wing;
        wing.index = index;
        wing.left = *x < -0.01 || (std::abs(*x) <= 0.01 && (index % 2 == 0));
        wing.xFt = *x;
        wing.yFt = *y;
        wing.zFt = *z;
        wing.semiFt = *semi;
        wing.rootChordFt = *rootChord;
        wing.tipChordFt = *tipChord;
        wing.sweepDeg =
            parseDouble(properties, prefix + "_sweep_design", result).value_or(0.0);
        wing.dihedralDeg =
            parseDouble(properties, prefix + "_dihed_design", result).value_or(0.0);
        rawWings.push_back(wing);
    }

    std::vector<std::pair<RawWing, RawWing>> mirroredPairs;
    for (const RawWing& left : rawWings) {
        if (!left.left) continue;
        auto best = rawWings.end();
        double bestError = std::numeric_limits<double>::max();
        for (auto it = rawWings.begin(); it != rawWings.end(); ++it) {
            if (it->left || !approximatelyMirrored(left, *it)) continue;
            const double error = std::abs(std::abs(left.xFt) - std::abs(it->xFt)) +
                                 std::abs(left.yFt - it->yFt) +
                                 std::abs(left.zFt - it->zFt);
            if (error < bestError) {
                best = it;
                bestError = error;
            }
        }
        if (best != rawWings.end()) mirroredPairs.emplace_back(left, *best);
    }

    const auto mainPairs = selectMainWingPairs(std::move(mirroredPairs));
    if (mainPairs.empty()) {
        addDiagnostic(result, Diagnostic::Severity::Warning, "main_wing_missing",
                      "No mirrored main-wing chain could be derived");
    } else {
        for (std::size_t pairIndex = 0; pairIndex < mainPairs.size(); ++pairIndex) {
            const RawWing& left = mainPairs[pairIndex].first;
            const RawWing& right = mainPairs[pairIndex].second;

            const Vec3 leftRoot = sourcePointToBody(
                left.xFt, left.yFt, left.zFt,
                result.profile.cgYSourceFt, result.profile.cgZSourceFt);
            const Vec3 rightRoot = sourcePointToBody(
                right.xFt, right.yFt, right.zFt,
                result.profile.cgYSourceFt, result.profile.cgZSourceFt);

            Vec3 leftTip = analyticWingTip(
                left, result.profile.cgYSourceFt, result.profile.cgZSourceFt);
            Vec3 rightTip = analyticWingTip(
                right, result.profile.cgYSourceFt, result.profile.cgZSourceFt);
            if (pairIndex + 1 < mainPairs.size()) {
                const RawWing& nextLeft = mainPairs[pairIndex + 1].first;
                const RawWing& nextRight = mainPairs[pairIndex + 1].second;
                leftTip = sourcePointToBody(
                    nextLeft.xFt, nextLeft.yFt, nextLeft.zFt,
                    result.profile.cgYSourceFt, result.profile.cgZSourceFt);
                rightTip = sourcePointToBody(
                    nextRight.xFt, nextRight.yFt, nextRight.zFt,
                    result.profile.cgYSourceFt, result.profile.cgZSourceFt);
            }

            WingSegmentGeometry leftSegment;
            leftSegment.sourceIndex = left.index;
            leftSegment.leftSide = true;
            leftSegment.rootBodyM = leftRoot;
            leftSegment.tipBodyM = leftTip;
            leftSegment.semiLengthM = left.semiFt * kFeetToMetres;
            leftSegment.rootChordM = left.rootChordFt * kFeetToMetres;
            leftSegment.tipChordM = left.tipChordFt * kFeetToMetres;
            leftSegment.sweepDeg = left.sweepDeg;
            leftSegment.dihedralDeg = left.dihedralDeg;
            result.profile.mainWingSegments.push_back(leftSegment);

            WingSegmentGeometry rightSegment;
            rightSegment.sourceIndex = right.index;
            rightSegment.leftSide = false;
            rightSegment.rootBodyM = rightRoot;
            rightSegment.tipBodyM = rightTip;
            rightSegment.semiLengthM = right.semiFt * kFeetToMetres;
            rightSegment.rootChordM = right.rootChordFt * kFeetToMetres;
            rightSegment.tipChordM = right.tipChordFt * kFeetToMetres;
            rightSegment.sweepDeg = right.sweepDeg;
            rightSegment.dihedralDeg = right.dihedralDeg;
            result.profile.mainWingSegments.push_back(rightSegment);

            if (pairIndex + 1 == mainPairs.size()) {
                result.profile.leftWingtipBodyM = leftTip;
                result.profile.rightWingtipBodyM = rightTip;
                result.profile.hasLeftWingtip = true;
                result.profile.hasRightWingtip = true;
            }
        }
    }

    if (result.profile.engines.empty()) {
        addDiagnostic(result, Diagnostic::Severity::Warning, "engines_missing",
                      "No active engine geometry was extracted");
    }

    const bool hasError = std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity == Diagnostic::Severity::Error;
        });
    result.ok = !hasError && (!result.profile.engines.empty() ||
                              !result.profile.mainWingSegments.empty());

    const auto finished = std::chrono::steady_clock::now();
    result.parseMilliseconds =
        std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

ParseResult parseAcfFile(const std::filesystem::path& path) {
    ParseResult result;
    result.profile.sourcePath = path.string();

    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        addDiagnostic(result, Diagnostic::Severity::Error, "file_stat_failed",
                      "Could not query the ACF file size: " + error.message());
        return result;
    }
    if (size > kMaxFileBytes) {
        addDiagnostic(result, Diagnostic::Severity::Error, "file_too_large",
                      "ACF file exceeds the 64 MB safety limit");
        return result;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        addDiagnostic(result, Diagnostic::Severity::Error, "file_open_failed",
                      "Could not open the ACF file");
        return result;
    }

    std::string text;
    text.resize(static_cast<std::size_t>(size));
    if (!text.empty()) stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream && !text.empty()) {
        addDiagnostic(result, Diagnostic::Severity::Error, "file_read_failed",
                      "Could not read the complete ACF file");
        return result;
    }
    return parseAcfText(text, path.string());
}

bool writeValidationReport(const ParseResult& result,
                           const std::filesystem::path& outputPath,
                           std::string* error) {
    std::error_code directoryError;
    const auto parent = outputPath.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, directoryError);
    if (directoryError) {
        if (error) *error = directoryError.message();
        return false;
    }

    std::ofstream out(outputPath, std::ios::trunc);
    if (!out) {
        if (error) *error = "Could not open report output";
        return false;
    }

    out << "FFAtmo Geometry Debug Report\n";
    out << "status=" << (result.ok ? "OK" : "FAILED") << "\n";
    out << "source=" << result.profile.sourcePath << "\n";
    out << "acf_version=" << result.profile.acfFormatVersion << "\n";
    out << "aircraft_name=" << result.profile.aircraftName << "\n";
    out << "aircraft_icao=" << result.profile.aircraftIcao << "\n";
    out << "source_bytes=" << result.sourceBytes << "\n";
    out << "property_count=" << result.propertyCount << "\n";
    out << "parse_ms=" << std::fixed << std::setprecision(3)
        << result.parseMilliseconds << "\n";
    out << "cg_y_source_ft=" << result.profile.cgYSourceFt << "\n";
    out << "cg_z_source_ft=" << result.profile.cgZSourceFt << "\n";
    out << "empty_mass_kg=" << result.profile.emptyMassKg << "\n";
    out << "maximum_mass_kg=" << result.profile.maximumMassKg << "\n";
    out << "engine_count=" << result.profile.engines.size() << "\n";

    for (const auto& engine : result.profile.engines) {
        out << "engine[" << engine.sourceIndex << "].type=" << engine.type << "\n";
        out << "engine[" << engine.sourceIndex << "].centre_m="
            << engine.centreBodyM.x << "," << engine.centreBodyM.y << ","
            << engine.centreBodyM.z << "\n";
        out << "engine[" << engine.sourceIndex << "].exhaust_m="
            << engine.exhaustOriginBodyM.x << "," << engine.exhaustOriginBodyM.y << ","
            << engine.exhaustOriginBodyM.z << "\n";
        out << "engine[" << engine.sourceIndex << "].direction="
            << engine.exhaustDirectionBody.x << "," << engine.exhaustDirectionBody.y << ","
            << engine.exhaustDirectionBody.z << "\n";
        out << "engine[" << engine.sourceIndex << "].max_thrust_n="
            << engine.maximumThrustN << "\n";
    }

    out << "main_wing_segment_count=" << result.profile.mainWingSegments.size() << "\n";
    for (const auto& wing : result.profile.mainWingSegments) {
        out << "wing[" << wing.sourceIndex << "].side="
            << (wing.leftSide ? "left" : "right") << "\n";
        out << "wing[" << wing.sourceIndex << "].root_m="
            << wing.rootBodyM.x << "," << wing.rootBodyM.y << "," << wing.rootBodyM.z << "\n";
        out << "wing[" << wing.sourceIndex << "].tip_m="
            << wing.tipBodyM.x << "," << wing.tipBodyM.y << "," << wing.tipBodyM.z << "\n";
    }

    if (result.profile.hasLeftWingtip) {
        out << "left_wingtip_m=" << result.profile.leftWingtipBodyM.x << ","
            << result.profile.leftWingtipBodyM.y << ","
            << result.profile.leftWingtipBodyM.z << "\n";
    }
    if (result.profile.hasRightWingtip) {
        out << "right_wingtip_m=" << result.profile.rightWingtipBodyM.x << ","
            << result.profile.rightWingtipBodyM.y << ","
            << result.profile.rightWingtipBodyM.z << "\n";
    }

    out << "diagnostic_count=" << result.diagnostics.size() << "\n";
    for (const auto& diagnostic : result.diagnostics) {
        out << severityText(diagnostic.severity) << " " << diagnostic.code;
        if (diagnostic.sourceLine != 0) out << " line=" << diagnostic.sourceLine;
        if (!diagnostic.propertyPath.empty()) out << " path=" << diagnostic.propertyPath;
        out << " message=" << diagnostic.message << "\n";
    }

    if (!out.good()) {
        if (error) *error = "Could not finish writing the validation report";
        return false;
    }
    return true;
}

class AcfProfileService::Impl {
public:
    Impl() : worker_([this] { run(); }) {}

    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    void request(const std::filesystem::path& path) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingPath_ = path;
            ++requestedGeneration_;
        }
        condition_.notify_one();
    }

    std::shared_ptr<const ParseResult> latest() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_;
    }

    bool busy() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return busy_ || completedGeneration_ < requestedGeneration_;
    }

    std::uint64_t requestedGeneration() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return requestedGeneration_;
    }

    std::uint64_t completedGeneration() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return completedGeneration_;
    }

private:
    void run() {
        std::uint64_t observed = 0;
        while (true) {
            std::filesystem::path path;
            std::uint64_t generation = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [&] {
                    return stopping_ || requestedGeneration_ > observed;
                });
                if (stopping_) return;
                path = pendingPath_;
                generation = requestedGeneration_;
                busy_ = true;
            }

            auto parsed = std::make_shared<ParseResult>(parseAcfFile(path));

            {
                std::lock_guard<std::mutex> lock(mutex_);
                observed = generation;
                if (generation == requestedGeneration_) {
                    latest_ = std::move(parsed);
                    completedGeneration_ = generation;
                }
                busy_ = false;
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    bool stopping_ = false;
    bool busy_ = false;
    std::filesystem::path pendingPath_;
    std::uint64_t requestedGeneration_ = 0;
    std::uint64_t completedGeneration_ = 0;
    std::shared_ptr<const ParseResult> latest_;
};

AcfProfileService::AcfProfileService() : impl_(std::make_unique<Impl>()) {}
AcfProfileService::~AcfProfileService() = default;

void AcfProfileService::request(const std::filesystem::path& path) {
    impl_->request(path);
}

std::shared_ptr<const ParseResult> AcfProfileService::latest() const {
    return impl_->latest();
}

bool AcfProfileService::busy() const {
    return impl_->busy();
}

std::uint64_t AcfProfileService::requestedGeneration() const {
    return impl_->requestedGeneration();
}

std::uint64_t AcfProfileService::completedGeneration() const {
    return impl_->completedGeneration();
}

}  // namespace ffatmo::acf
