#include "diagnostics/ReplayFile.h"
#include "diagnostics/ReplayRunner.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr const char* kReplayRunnerBuild = "engine-v1-replay-runner-v1";

void printUsage() {
    std::cout << "FFAtmo Offline Replay Runner (" << kReplayRunnerBuild << ")\n\n"
              << "Usage:\n"
              << "  FFAtmoReplayRunner <recording.ffar> [--output <directory>] [--no-csv]\n\n"
              << "The runner validates the replay, normalizes aircraft/environment state,\n"
              << "detects X-Plane local-origin rebases, freezes physics during pause/replay,\n"
              << "and writes a deterministic summary and optional CSV.\n";
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

    const auto result = ffatmo::diagnostics::runReplayAnalysis(replay);
    const std::string stem = replayPath.stem().string();
    const auto summaryPath = outputDirectory / (stem + "-runner-summary.txt");
    const auto csvPath = outputDirectory / (stem + "-normalized.csv");

    std::string error;
    if (!ffatmo::diagnostics::writeReplayRunnerSummary(result, replay.metadata, summaryPath, &error)) {
        std::cerr << "ERROR: " << error << '\n';
        return 1;
    }
    if (writeCsv && !ffatmo::diagnostics::writeNormalizedReplayCsv(result, csvPath, &error)) {
        std::cerr << "ERROR: " << error << '\n';
        return 1;
    }

    const auto& summary = result.summary;
    std::cout << "FFAtmo replay analysis " << (summary.ok ? "completed" : "failed") << '\n'
              << "Build: " << kReplayRunnerBuild << '\n'
              << "Aircraft: " << replay.metadata.aircraftName
              << " (" << replay.metadata.aircraftIcao << ")\n"
              << "Snapshots: " << summary.inputSnapshotCount << '\n'
              << "Physics time: " << summary.integratedPhysicsTimeSeconds << " s\n"
              << "Pause samples: " << summary.pausedSampleCount << '\n'
              << "Replay samples: " << summary.replaySampleCount << '\n'
              << "Origin rebases: " << summary.localOriginRebaseCount << '\n'
              << "Deterministic hash: 0x" << std::hex << summary.deterministicHash << std::dec << '\n'
              << "Summary: " << summaryPath.string() << '\n';
    if (writeCsv) std::cout << "CSV: " << csvPath.string() << '\n';
    if (!summary.ok) {
        std::cerr << "ERROR: " << summary.error << '\n';
        return 1;
    }
    return 0;
}
