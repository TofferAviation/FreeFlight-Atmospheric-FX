#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ffatmo::acf {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Diagnostic {
    enum class Severity { Info, Warning, Error };
    Severity severity = Severity::Info;
    std::string code;
    std::string message;
    std::uint32_t sourceLine = 0;
    std::string propertyPath;
};

struct EngineGeometry {
    int sourceIndex = -1;
    std::string type;
    Vec3 centreBodyM;
    Vec3 exhaustOriginBodyM;
    Vec3 exhaustDirectionBody;
    double yawDeg = 0.0;
    double pitchDeg = 0.0;
    double maximumThrustN = 0.0;
};

struct WingSegmentGeometry {
    int sourceIndex = -1;
    bool leftSide = false;
    Vec3 rootBodyM;
    Vec3 tipBodyM;
    double semiLengthM = 0.0;
    double rootChordM = 0.0;
    double tipChordM = 0.0;
    double sweepDeg = 0.0;
    double dihedralDeg = 0.0;
};

struct AircraftGeometryProfile {
    int acfFormatVersion = 0;
    std::string sourcePath;
    std::string aircraftName;
    std::string aircraftIcao;
    double cgYSourceFt = 0.0;
    double cgZSourceFt = 0.0;
    double emptyMassKg = 0.0;
    double maximumMassKg = 0.0;
    std::vector<EngineGeometry> engines;
    std::vector<WingSegmentGeometry> mainWingSegments;
    Vec3 leftWingtipBodyM;
    Vec3 rightWingtipBodyM;
    bool hasLeftWingtip = false;
    bool hasRightWingtip = false;
};

struct ParseResult {
    bool ok = false;
    AircraftGeometryProfile profile;
    std::vector<Diagnostic> diagnostics;
    std::uint64_t sourceBytes = 0;
    std::uint64_t propertyCount = 0;
    double parseMilliseconds = 0.0;
};

ParseResult parseAcfText(const std::string& text, const std::string& sourceName);
ParseResult parseAcfFile(const std::filesystem::path& path);
bool writeValidationReport(const ParseResult& result,
                           const std::filesystem::path& outputPath,
                           std::string* error = nullptr);

class AcfProfileService {
public:
    AcfProfileService();
    ~AcfProfileService();

    AcfProfileService(const AcfProfileService&) = delete;
    AcfProfileService& operator=(const AcfProfileService&) = delete;

    void request(const std::filesystem::path& path);
    std::shared_ptr<const ParseResult> latest() const;
    bool busy() const;
    std::uint64_t requestedGeneration() const;
    std::uint64_t completedGeneration() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ffatmo::acf
