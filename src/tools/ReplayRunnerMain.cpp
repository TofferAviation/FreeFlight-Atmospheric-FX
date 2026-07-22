#include "diagnostics/ReplayFile.h"
#include "diagnostics/ReplayRunner.h"
#include "engine/ContrailSimulation.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr const char* kReplayRunnerBuild = "engine-v1-replay-runner-v2";

void printUsage() {
    std::cout << "FFAtmo Offline Replay Runner (" << kReplayRunnerBuild << ")\n\n"
              << "Usage:\n"
              << "  FFAtmoReplayRunner <recording.ffar> [--output <directory>] [--no-csv]\n\n"
              << "The runner validates and normalizes the replay, freezes physics during\n"
              << "pause/replay, survives X-Plane local-origin rebases, and executes the\n"
              << "first deterministic contrail formation and dry-air decay simulation.\n"
              << "No X-Plane installation is required to run this tool.\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 2;
    }

    std::filesystem::path replayPath;
    std::filesystem::path outputDirectory;
    bool writeCsv = true;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            printUsage();
            return 0;
        }
        if (argument == "--no-csv") {
            writeCsv = false;
            continue;
        }
        if (argument == "--output") {
            if (index + 1 >= argc) {
                std::cerr << "ERROR: --output requires a directory\n";
                return 2;
            }
            outputDirectory = argv[++index];
            continue;
        }
        if (!argument.empty() && argument[0] == '-') {
            std::cerr << "ERROR: unknown option: " << argument << '\n';
            return 2;
        }
        if (!replayPath.empty()) {
            std::cerr << "ERROR: only one replay file may be supplied\n";
            return 2;
        }
        replayPath = argument;
    }

    if (replayPath.empty()) {
        std::cerr << "ERROR: no replay file supplied\n";
        return 2;
    }
    if (!std::filesystem::exists(replayPath)) {
        std::cerr << "ERROR: replay file does not exist: " << replayPath.string() << '\n';
        return 2;
    }
    if (outputDirectory.empty()) {
        outputDirectory = replayPath.parent_path() / "FFAtmoReplayOutput";
    }

    const auto replay = ffatmo::diagnostics::readReplayFile(replayPath);
    if (!replay.ok) {
        std::cerr << "ERROR: replay validation failed: " << replay.error << '\n';
        return 1;
    }

    const auto normalized = ffatmo::diagnostics::runReplayAnalysis(replay);
    if (!normalized.summary.ok) {
        std::cerr << "ERROR: replay normalization failed: "
                  << normalized.summary.error << '\n';
        return 1;
    }

    const auto contrails = ffatmo::engine::simulateContrails(replay, normalized);
    if (!contrails.summary.ok) {
        std::cerr << "ERROR: contrail simulation failed: "
                  << contrails.summary.error << '\n';
        return 1;
    }

    const std::string stem = replayPath.stem().string();
    const auto normalizedSummaryPath = outputDirectory / (stem + "-runner-summary.txt");
    const auto normalizedCsvPath = outputDirectory / (stem + "-normalized.csv");
    const auto contrailSummaryPath = outputDirectory / (stem + "-contrail-summary.txt");
    const auto contrailCsvPath = outputDirectory / (stem + "-contrail-timeline.csv");

    std::string error;
    if (!ffatmo::diagnostics::writeReplayRunnerSummary(
            normalized, replay.metadata, normalizedSummaryPath, &error)) {
        std::cerr << "ERROR: " << error << '\n';
        return 1;
    }
    if (!ffatmo::engine::writeContrailSimulationSummary(
            contrails, replay.metadata, contrailSummaryPath, &error)) {
        std::cerr << "ERROR: " << error << '\n';
        return 1;
    }
    if (writeCsv && !ffatmo::diagnostics::writeNormalizedReplayCsv(
            normalized, normalizedCsvPath, &error)) {
        std::cerr << "ERROR: " << error << '\n';
        return 1;
    }
    if (writeCsv && !ffatmo::engine::writeContrailTimelineCsv(
            contrails, contrailCsvPath, &error)) {
        std::cerr << "ERROR: " << error << '\n';
        return 1;
    }

    const auto& replaySummary = normalized.summary;
    const auto& contrailSummary = contrails.summary;
    std::cout << "FFAtmo replay and contrail analysis completed\n"
              << "Build: " << kReplayRunnerBuild << '\n'
              << "Aircraft: " << replay.metadata.aircraftName
              << " (" << replay.metadata.aircraftIcao << ")\n"
              << "Snapshots: " << replaySummary.inputSnapshotCount << '\n'
              << "Physics time: " << replaySummary.integratedPhysicsTimeSeconds << " s\n"
              << "Origin rebases: " << replaySummary.localOriginRebaseCount << '\n'
              << "Normalized hash: 0x" << std::hex
              << replaySummary.deterministicHash << std::dec << '\n'
              << "Contrail parcels emitted: " << contrailSummary.emittedParcelCount << '\n'
              << "Contrail parcels expired: " << contrailSummary.expiredParcelCount << '\n'
              << "Peak active parcels: " << contrailSummary.peakActiveParcelCount << '\n'
              << "Maximum visible length: "
              << contrailSummary.maximumVisibleTrailLengthM << " m\n"
              << "Contrail hash: 0x" << std::hex
              << contrailSummary.deterministicHash << std::dec << '\n'
              << "Replay summary: " << normalizedSummaryPath.string() << '\n'
              << "Contrail summary: " << contrailSummaryPath.string() << '\n';
    if (writeCsv) {
        std::cout << "Normalized CSV: " << normalizedCsvPath.string() << '\n'
                  << "Contrail timeline CSV: " << contrailCsvPath.string() << '\n';
    }
    return 0;
}
