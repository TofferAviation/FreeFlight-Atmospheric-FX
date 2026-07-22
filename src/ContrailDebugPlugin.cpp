#include "ContrailDebugOverlay.h"
#include "ContrailWorldRenderer.h"
#include "acf/AcfGeometry.h"
#include "diagnostics/LiveSnapshotNormalizer.h"
#include "engine/LiveContrailEngine.h"
#include "host/XPlaneSnapshotSource.h"
#include "render/ContrailRenderPlanner.h"

#include "XPLMDataAccess.h"
#include "XPLMDefs.h"
#include "XPLMMenus.h"
#include "XPLMPlanes.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>

namespace ffatmo {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kMaximumRenderInputParcels = 6000;
constexpr float kPreviewMinimumAglM = 120.0f;
constexpr float kPreviewMinimumTrueAirspeedMps = 65.0f;

class ContrailDebugRuntime {
public:
    enum class AtmosphereMode {
        Live,
        DryPreview,
        PersistentPreview
    };

    ContrailDebugRuntime() {
        engine::ContrailSimulationSettings settings;
        settings.maximumActiveParcels = 12000;
        liveEngine_.setSettings(settings);
        renderPlannerSettings_.visibleCapacity = ContrailWorldRenderer::kVisibleCapacity;
        renderPlannerSettings_.maximumSamplesPerSegment = 16;
    }

    bool start() {
        pluginRoot_ = findPluginRoot();
        reportPath_ = pluginRoot_ / "reports" / "contrail_visual_debug.txt";
        profileService_ = std::make_unique<acf::AcfProfileService>();
        createCommandsAndMenu();
        log("Started. Contrail Renderer v4 uses deterministic curved render planning and eight neutral-white assets.\n");
        return true;
    }

    bool enable() {
        if (!snapshotSource_.resolve()) {
            log("Required simulator snapshot DataRefs are unavailable.\n");
            return false;
        }
        if (!overlay_.start()) {
            log("Could not register the Vulkan-safe status overlay.\n");
            return false;
        }
        if (!worldRenderer_.start(pluginRoot_ / "assets")) {
            log("Could not start Renderer v4. Check the eight assets in the assets folder.\n");
            overlay_.stop();
            return false;
        }

        visualEnabled_ = true;
        overlay_.setEnabled(true);
        worldRenderer_.setEnabled(true);
        resetAllState();
        requestCurrentAircraft();
        XPLMRegisterFlightLoopCallback(flightLoopCallback, -1.0f, this);
        enabled_ = true;
        return true;
    }

    void disable() {
        if (enabled_) {
            XPLMUnregisterFlightLoopCallback(flightLoopCallback, this);
            enabled_ = false;
        }
        worldRenderer_.stop();
        overlay_.stop();
        resetAllState();
    }

    void stop() {
        disable();
        destroyCommandsAndMenu();
        profileService_.reset();
    }

    void aircraftChanged() {
        aircraftDirty_ = true;
    }

private:
    static void log(const std::string& text) {
        XPLMDebugString(("[FFAtmo Contrail Debug] " + text).c_str());
    }

    static std::filesystem::path findPluginRoot() {
        char pluginPath[2048] {};
        XPLMGetPluginInfo(XPLMGetMyID(), nullptr, pluginPath, nullptr, nullptr);
        return std::filesystem::path(pluginPath).parent_path().parent_path();
    }

    static std::filesystem::path xPlaneRoot() {
        char systemPath[2048] {};
        XPLMGetSystemPath(systemPath);
        return std::filesystem::path(systemPath);
    }

    static std::filesystem::path currentAircraftPath() {
        char fileName[512] {};
        char pathBuffer[2048] {};
        XPLMGetNthAircraftModel(0, fileName, pathBuffer);
        std::filesystem::path path = pathBuffer[0]
            ? std::filesystem::path(pathBuffer)
            : std::filesystem::path(fileName);
        if (path.empty() || path.extension() != ".acf") return {};
        if (path.is_relative()) path = xPlaneRoot() / path;
        std::error_code error;
        const auto canonical = std::filesystem::weakly_canonical(path, error);
        return error ? path.lexically_normal() : canonical;
    }

    static const char* modeName(AtmosphereMode mode) {
        switch (mode) {
            case AtmosphereMode::Live: return "LIVE WEATHER";
            case AtmosphereMode::DryPreview: return "FORCED DRY PREVIEW";
            case AtmosphereMode::PersistentPreview: return "FORCED PERSISTENT PREVIEW";
        }
        return "UNKNOWN";
    }

    static engine::Vec3d bodyOffsetToLocal(const engine::Vec3d& body,
                                            float headingDeg,
                                            float pitchDeg,
                                            float rollDeg) {
        const double heading = static_cast<double>(headingDeg) * kPi / 180.0;
        const double pitch = static_cast<double>(pitchDeg) * kPi / 180.0;
        const double roll = static_cast<double>(rollDeg) * kPi / 180.0;
        const double cosRoll = std::cos(roll);
        const double sinRoll = std::sin(roll);
        const double cosPitch = std::cos(pitch);
        const double sinPitch = std::sin(pitch);
        const double cosHeading = std::cos(heading);
        const double sinHeading = std::sin(heading);

        const double rollX = cosRoll * body.x + sinRoll * body.y;
        const double rollY = -sinRoll * body.x + cosRoll * body.y;
        const double rollZ = body.z;
        const double pitchX = rollX;
        const double pitchY = cosPitch * rollY - sinPitch * rollZ;
        const double pitchZ = sinPitch * rollY + cosPitch * rollZ;
        return {
            cosHeading * pitchX - sinHeading * pitchZ,
            pitchY,
            sinHeading * pitchX + cosHeading * pitchZ
        };
    }

    void resetAllState() {
        normalizer_.reset();
        liveEngine_.reset();
        sequenceNumber_ = 0;
        latestSnapshot_ = {};
        latestNormalized_ = {};
        latestTimeline_ = {};
        latestRenderPlan_ = {};
        latestPlannerTimeMs_ = 0.0;
        maximumPlannerTimeMs_ = 0.0;
        previewGateOpen_ = false;
        worldRenderer_.update({});
        overlay_.setFrame({}, {}, {});
    }

    void requestCurrentAircraft() {
        aircraftDirty_ = false;
        geometryReady_ = false;
        geometryStatus_ = "ACF PARSING";
        engineExhaustBodyOffsets_.clear();
        liveEngine_.setEngineExhaustBodyOffsets({});
        normalizer_.reset();
        liveEngine_.reset();
        latestRenderPlan_ = {};
        worldRenderer_.update({});
        sequenceNumber_ = 0;

        if (!profileService_) return;
        const auto path = currentAircraftPath();
        if (path.empty()) {
            geometryStatus_ = "WAITING FOR ACF";
            log("Aircraft ACF is not available yet; waiting for XPLM_MSG_PLANE_LOADED.\n");
            return;
        }
        activeAircraftPath_ = path;
        profileService_->request(path);
        expectedGeneration_ = profileService_->requestedGeneration();
        log("Parsing live exhaust geometry: " + path.string() + "\n");
    }

    void pollCompletedProfile() {
        if (!profileService_ ||
            profileService_->completedGeneration() < expectedGeneration_ ||
            deliveredGeneration_ >= expectedGeneration_) {
            return;
        }
        const auto result = profileService_->latest();
        if (!result) return;
        deliveredGeneration_ = profileService_->completedGeneration();
        currentGeometry_ = result;

        if (!result->ok || result->profile.engines.empty()) {
            geometryReady_ = false;
            geometryStatus_ = "ACF FALLBACK OFFSETS";
            log("ACF geometry unavailable; using symmetric fallback exhaust offsets.\n");
            return;
        }

        engineExhaustBodyOffsets_.clear();
        engineExhaustBodyOffsets_.reserve(result->profile.engines.size());
        for (const auto& engine : result->profile.engines) {
            engineExhaustBodyOffsets_.push_back({
                engine.exhaustOriginBodyM.x,
                engine.exhaustOriginBodyM.y,
                engine.exhaustOriginBodyM.z
            });
        }
        liveEngine_.setEngineExhaustBodyOffsets(engineExhaustBodyOffsets_);
        liveEngine_.reset();
        latestRenderPlan_ = {};
        worldRenderer_.update({});
        geometryReady_ = true;
        geometryStatus_ = "ACF EXHAUSTS: " +
            std::to_string(engineExhaustBodyOffsets_.size());
        log("Live exhaust geometry ready for " + result->profile.aircraftName +
            ": " + std::to_string(engineExhaustBodyOffsets_.size()) + " engines.\n");
        XPLMSpeakString("FF Atmo Renderer v4 geometry ready");
    }

    bool applyAtmosphereMode(engine::SimulatorSnapshot& snapshot,
                             diagnostics::NormalizedReplaySample& normalized) const {
        if (atmosphereMode_ == AtmosphereMode::Live) return true;

        const bool gateOpen = snapshot.heightAglM >= kPreviewMinimumAglM &&
                              snapshot.trueAirspeedMps >= kPreviewMinimumTrueAirspeedMps;
        if (!gateOpen) {
            for (auto& engine : snapshot.engines) {
                engine.running = 0;
                engine.n1Percent = 0.0f;
                engine.n2Percent = 0.0f;
                engine.fuelFlowKgps = 0.0f;
                engine.thrustN = 0.0f;
                engine.exhaustVelocityMps = 0.0f;
            }
            return false;
        }

        const bool persistent = atmosphereMode_ == AtmosphereMode::PersistentPreview;
        normalized.altitudeMslM = std::max(normalized.altitudeMslM, 9000.0);
        normalized.temperatureK = persistent ? 232.0f : 234.0f;
        normalized.dewPointK = persistent ? 231.0f : 226.0f;
        normalized.relativeHumidityIcePercent = persistent ? 112.0f : 72.0f;
        snapshot.atmosphere.temperatureK = normalized.temperatureK;
        snapshot.atmosphere.staticPressurePa = 30000.0f;
        snapshot.atmosphere.densityKgM3 = 0.45f;

        const std::uint32_t desiredEngines = static_cast<std::uint32_t>(
            std::clamp<std::size_t>(
                engineExhaustBodyOffsets_.empty() ? 2 : engineExhaustBodyOffsets_.size(),
                1,
                engine::kMaximumRecordedEngines));
        snapshot.engineCount = std::max(snapshot.engineCount, desiredEngines);
        snapshot.engineCount = std::min<std::uint32_t>(
            snapshot.engineCount, static_cast<std::uint32_t>(snapshot.engines.size()));
        for (std::uint32_t index = 0; index < snapshot.engineCount; ++index) {
            auto& engine = snapshot.engines[index];
            engine.running = 1;
            engine.n1Percent = std::max(engine.n1Percent, 85.0f);
            engine.n2Percent = std::max(engine.n2Percent, 85.0f);
            engine.fuelFlowKgps = std::max(engine.fuelFlowKgps, 1.10f);
            engine.thrustN = std::max(engine.thrustN, 22000.0f);
            engine.exhaustVelocityMps = std::max(engine.exhaustVelocityMps, 390.0f);
        }
        return true;
    }

    std::vector<render::ContrailRenderInput> buildRenderInputs(
        const engine::SimulatorSnapshot& snapshot,
        const diagnostics::NormalizedReplaySample& normalized) const {
        const auto& parcels = liveEngine_.parcels();
        const std::size_t start = parcels.size() > kMaximumRenderInputParcels
            ? parcels.size() - kMaximumRenderInputParcels
            : 0;
        std::vector<render::ContrailRenderInput> inputs;
        inputs.reserve(parcels.size() - start);
        for (std::size_t index = start; index < parcels.size(); ++index) {
            const auto& parcel = parcels[index];
            const double deltaEast = parcel.worldPositionM.x - normalized.worldEastM;
            const double deltaUp = parcel.worldPositionM.y - normalized.worldUpM;
            const double deltaNorth = parcel.worldPositionM.z - normalized.worldNorthM;
            render::ContrailRenderInput item;
            item.sourceParcelId = parcel.id;
            item.engineIndex = parcel.engineIndex;
            item.localPositionM = {
                snapshot.localPositionM.x + deltaEast,
                snapshot.localPositionM.y + deltaUp,
                snapshot.localPositionM.z - deltaNorth
            };
            item.physicsRadiusM = parcel.radiusM;
            item.opticalDepth = parcel.opticalDepth;
            item.normalizedIceMass = parcel.normalizedIceMass;
            item.ageSeconds = parcel.ageSeconds;
            inputs.push_back(item);
        }
        return inputs;
    }

    std::vector<ContrailDebugRenderSource> buildRenderSources(
        const engine::SimulatorSnapshot& snapshot) const {
        std::vector<engine::Vec3d> offsets = engineExhaustBodyOffsets_;
        if (offsets.empty()) offsets = {{-5.0, -1.0, 1.0}, {5.0, -1.0, 1.0}};
        std::vector<ContrailDebugRenderSource> sources;
        sources.reserve(offsets.size());
        for (std::size_t index = 0; index < offsets.size(); ++index) {
            const engine::Vec3d localOffset = bodyOffsetToLocal(
                offsets[index],
                snapshot.headingDegTrue,
                snapshot.pitchDeg,
                snapshot.rollDeg);
            sources.push_back({
                {
                    snapshot.localPositionM.x + localOffset.x,
                    snapshot.localPositionM.y + localOffset.y,
                    snapshot.localPositionM.z + localOffset.z
                },
                static_cast<std::uint32_t>(index)
            });
        }
        return sources;
    }

    ContrailDebugOverlayStatus buildOverlayStatus() const {
        ContrailDebugOverlayStatus status;
        status.aircraftIcao = snapshotSource_.aircraftIcao();
        status.mode = modeName(atmosphereMode_);
        status.geometryStatus = geometryStatus_;
        status.rendererStatus = worldRenderer_.ready() ? "WORLD V4 READY" : "LOADING V4 ASSETS";
        status.activeParcels = liveEngine_.parcels().size();
        status.emittedParcels = liveEngine_.summary().emittedParcelCount;
        status.expiredParcels = liveEngine_.summary().expiredParcelCount;
        status.peakParcels = liveEngine_.summary().peakActiveParcelCount;
        status.originRebases = normalizer_.localOriginRebaseCount();
        status.visibleInstances = worldRenderer_.visibleInstanceCount();
        status.formationPotential = latestTimeline_.formationPotential;
        status.relativeHumidityIcePercent = latestNormalized_.relativeHumidityIcePercent;
        status.temperatureK = latestNormalized_.temperatureK;
        status.physicsFrozen = latestTimeline_.physicsFrozen;
        status.simulationEnabled = simulationEnabled_;
        status.rendererReady = worldRenderer_.ready();
        status.previewGateOpen = previewGateOpen_;
        return status;
    }

    void exportReport() const {
        std::error_code directoryError;
        std::filesystem::create_directories(reportPath_.parent_path(), directoryError);
        std::ofstream stream(reportPath_, std::ios::trunc);
        if (!stream) {
            log("Could not open the live visual-debug report.\n");
            return;
        }

        const auto& summary = liveEngine_.summary();
        const auto& planner = latestRenderPlan_.statistics;
        stream << "FFAtmo World Contrail Visual Debug Report v4\n"
               << "status=" << (summary.ok ? "OK" : "ERROR") << '\n'
               << "error=" << summary.error << '\n'
               << "aircraft_name=" << snapshotSource_.aircraftName() << '\n'
               << "aircraft_icao=" << snapshotSource_.aircraftIcao() << '\n'
               << "aircraft_path=" << activeAircraftPath_.string() << '\n'
               << "atmosphere_mode=" << modeName(atmosphereMode_) << '\n'
               << "preview_gate_open=" << (previewGateOpen_ ? 1 : 0) << '\n'
               << "preview_minimum_agl_m=" << kPreviewMinimumAglM << '\n'
               << "preview_minimum_tas_mps=" << kPreviewMinimumTrueAirspeedMps << '\n'
               << "geometry_status=" << geometryStatus_ << '\n'
               << "geometry_engine_count=" << engineExhaustBodyOffsets_.size() << '\n'
               << "world_renderer_ready=" << (worldRenderer_.ready() ? 1 : 0) << '\n'
               << "world_renderer_loaded_objects=" << worldRenderer_.loadedObjectCount() << '\n'
               << "world_renderer_visible_instances=" << worldRenderer_.visibleInstanceCount() << '\n'
               << "world_renderer_capacity=" << ContrailWorldRenderer::kVisibleCapacity << '\n'
               << "world_renderer_pooled_instances="
               << ContrailWorldRenderer::kAssetCount * ContrailWorldRenderer::kInstancesPerAsset << '\n'
               << "world_renderer_pool_capacity_drop_count="
               << worldRenderer_.poolCapacityDropCount() << '\n'
               << "simulation_enabled=" << (simulationEnabled_ ? 1 : 0) << '\n'
               << "visuals_enabled=" << (visualEnabled_ ? 1 : 0) << '\n'
               << "input_sample_count=" << summary.inputSampleCount << '\n'
               << "active_parcel_count=" << liveEngine_.parcels().size() << '\n'
               << "emitted_parcel_count=" << summary.emittedParcelCount << '\n'
               << "expired_parcel_count=" << summary.expiredParcelCount << '\n'
               << "peak_active_parcel_count=" << summary.peakActiveParcelCount << '\n'
               << "capacity_drop_count=" << summary.capacityDropCount << '\n'
               << "local_origin_rebase_count=" << normalizer_.localOriginRebaseCount() << '\n'
               << "render_planner_input_count=" << planner.inputParcelCount << '\n'
               << "render_planner_valid_count=" << planner.validParcelCount << '\n'
               << "render_planner_generated_count=" << planner.generatedSampleCount << '\n'
               << "render_planner_selected_count=" << planner.selectedSampleCount << '\n'
               << "render_planner_selected_core_count=" << planner.selectedCoreCount << '\n'
               << "render_planner_selected_halo_count=" << planner.selectedHaloCount << '\n'
               << "render_planner_stream_break_count=" << planner.streamBreakCount << '\n'
               << "render_planner_capacity_rejected_count=" << planner.capacityRejectedCount << '\n'
               << std::fixed << std::setprecision(6)
               << "integrated_physics_time_seconds="
               << normalizer_.integratedPhysicsTimeSeconds() << '\n'
               << "current_formation_potential=" << latestTimeline_.formationPotential << '\n'
               << "current_relative_humidity_ice_percent="
               << latestNormalized_.relativeHumidityIcePercent << '\n'
               << "current_temperature_k=" << latestNormalized_.temperatureK << '\n'
               << "maximum_visible_trail_length_m="
               << summary.maximumVisibleTrailLengthM << '\n'
               << "maximum_parcel_age_seconds=" << summary.maximumParcelAgeSeconds << '\n'
               << "render_planner_latest_time_ms=" << latestPlannerTimeMs_ << '\n'
               << "render_planner_maximum_time_ms=" << maximumPlannerTimeMs_ << '\n'
               << "render_planner_maximum_spacing_m=" << planner.maximumSelectedSpacingM << '\n'
               << "render_planner_maximum_curve_deviation_m="
               << planner.maximumCurveDeviationM << '\n'
               << "maximum_billboard_error_deg="
               << worldRenderer_.maximumBillboardErrorDeg() << '\n'
               << std::hex << std::setfill('0')
               << "render_planner_hash=0x" << std::setw(16)
               << planner.deterministicHash << '\n'
               << "deterministic_hash=0x" << std::setw(16)
               << summary.deterministicHash << '\n';

        log("Renderer v4 report written to: " + reportPath_.string() + "\n");
        XPLMSpeakString("FF Atmo Renderer v4 report exported");
    }

    void cycleAtmosphereMode() {
        switch (atmosphereMode_) {
            case AtmosphereMode::Live:
                atmosphereMode_ = AtmosphereMode::DryPreview;
                break;
            case AtmosphereMode::DryPreview:
                atmosphereMode_ = AtmosphereMode::PersistentPreview;
                break;
            case AtmosphereMode::PersistentPreview:
                atmosphereMode_ = AtmosphereMode::Live;
                break;
        }
        liveEngine_.reset();
        latestRenderPlan_ = {};
        worldRenderer_.update({});
        log(std::string("Atmosphere mode: ") + modeName(atmosphereMode_) + "\n");
        XPLMSpeakString(modeName(atmosphereMode_));
    }

    float update(float elapsedSinceLastCall) {
        if (aircraftDirty_) requestCurrentAircraft();
        pollCompletedProfile();

        const float deltaSeconds = std::clamp(elapsedSinceLastCall, 0.0f, 0.25f);
        latestSnapshot_ = snapshotSource_.capture(
            ++sequenceNumber_, deltaSeconds, engine::LifecycleNone);
        latestNormalized_ = normalizer_.normalize(latestSnapshot_);
        if (latestNormalized_.localOriginRebased) {
            log("X-Plane local origin shifted; engine-world contrail continuity retained.\n");
        }

        engine::SimulatorSnapshot simulationSnapshot = latestSnapshot_;
        diagnostics::NormalizedReplaySample simulationNormalized = latestNormalized_;
        previewGateOpen_ = applyAtmosphereMode(simulationSnapshot, simulationNormalized);
        latestNormalized_.temperatureK = simulationNormalized.temperatureK;
        latestNormalized_.dewPointK = simulationNormalized.dewPointK;
        latestNormalized_.relativeHumidityIcePercent =
            simulationNormalized.relativeHumidityIcePercent;
        latestNormalized_.altitudeMslM = simulationNormalized.altitudeMslM;

        if (simulationEnabled_) {
            latestTimeline_ = liveEngine_.step(simulationSnapshot, simulationNormalized);
        } else {
            latestTimeline_.sequenceNumber = simulationNormalized.sequenceNumber;
            latestTimeline_.simulationTimeSeconds = simulationNormalized.simulationTimeSeconds;
            latestTimeline_.physicsDeltaSeconds = 0.0;
            latestTimeline_.relativeHumidityIcePercent =
                simulationNormalized.relativeHumidityIcePercent;
            latestTimeline_.temperatureK = simulationNormalized.temperatureK;
            latestTimeline_.physicsFrozen = true;
            latestTimeline_.activeParcelCount = liveEngine_.parcels().size();
        }

        const auto inputs = buildRenderInputs(latestSnapshot_, simulationNormalized);
        const auto plannerStart = std::chrono::steady_clock::now();
        latestRenderPlan_ = render::planContrailRenderSamples(inputs, renderPlannerSettings_);
        const auto plannerEnd = std::chrono::steady_clock::now();
        latestPlannerTimeMs_ = std::chrono::duration<double, std::milli>(
            plannerEnd - plannerStart).count();
        maximumPlannerTimeMs_ = std::max(maximumPlannerTimeMs_, latestPlannerTimeMs_);

        worldRenderer_.update(latestRenderPlan_.samples);
        overlay_.setFrame({}, buildRenderSources(latestSnapshot_), buildOverlayStatus());
        return -1.0f;
    }

    static float flightLoopCallback(float elapsedSinceLastCall,
                                    float,
                                    int,
                                    void* refcon) {
        return static_cast<ContrailDebugRuntime*>(refcon)->update(elapsedSinceLastCall);
    }

    static int commandHandler(XPLMCommandRef command,
                              XPLMCommandPhase phase,
                              void* refcon) {
        if (phase != xplm_CommandBegin) return 1;
        auto* self = static_cast<ContrailDebugRuntime*>(refcon);
        if (command == self->toggleOverlayCommand_) {
            self->visualEnabled_ = !self->visualEnabled_;
            self->overlay_.setEnabled(self->visualEnabled_);
            self->worldRenderer_.setEnabled(self->visualEnabled_);
            log(std::string("Renderer v4 visuals ") +
                (self->visualEnabled_ ? "enabled.\n" : "disabled.\n"));
        } else if (command == self->toggleSimulationCommand_) {
            self->simulationEnabled_ = !self->simulationEnabled_;
            log(std::string("Live contrail simulation ") +
                (self->simulationEnabled_ ? "enabled.\n" : "disabled.\n"));
        } else if (command == self->cycleAtmosphereCommand_) {
            self->cycleAtmosphereMode();
        } else if (command == self->resetTrailCommand_) {
            self->liveEngine_.reset();
            self->latestRenderPlan_ = {};
            self->worldRenderer_.update({});
            log("Live contrail trail reset.\n");
        } else if (command == self->reloadGeometryCommand_) {
            self->requestCurrentAircraft();
        } else if (command == self->exportReportCommand_) {
            self->exportReport();
        }
        return 1;
    }

    void createCommandsAndMenu() {
        toggleOverlayCommand_ = XPLMCreateCommand(
            "ffatmo_contrail_debug/toggle_overlay",
            "Toggle FFAtmo Renderer v4 visuals and status overlay");
        toggleSimulationCommand_ = XPLMCreateCommand(
            "ffatmo_contrail_debug/toggle_simulation",
            "Enable or disable live contrail physics");
        cycleAtmosphereCommand_ = XPLMCreateCommand(
            "ffatmo_contrail_debug/cycle_atmosphere_mode",
            "Cycle live, forced dry, and forced persistent atmosphere modes");
        resetTrailCommand_ = XPLMCreateCommand(
            "ffatmo_contrail_debug/reset_trail",
            "Reset the live contrail trail");
        reloadGeometryCommand_ = XPLMCreateCommand(
            "ffatmo_contrail_debug/reload_geometry",
            "Reload current aircraft exhaust geometry");
        exportReportCommand_ = XPLMCreateCommand(
            "ffatmo_contrail_debug/export_report",
            "Export the live contrail visual-debug report");

        for (XPLMCommandRef command : {
            toggleOverlayCommand_,
            toggleSimulationCommand_,
            cycleAtmosphereCommand_,
            resetTrailCommand_,
            reloadGeometryCommand_,
            exportReportCommand_}) {
            XPLMRegisterCommandHandler(command, commandHandler, 1, this);
        }

        const int parentItem = XPLMAppendMenuItem(
            XPLMFindPluginsMenu(), "FFAtmo Contrail Visual Debug", nullptr, 0);
        menu_ = XPLMCreateMenu(
            "FFAtmo Contrail Visual Debug",
            XPLMFindPluginsMenu(),
            parentItem,
            nullptr,
            nullptr);
        XPLMAppendMenuItemWithCommand(
            menu_, "Renderer v4 Visuals + Status: ON / OFF", toggleOverlayCommand_);
        XPLMAppendMenuItemWithCommand(
            menu_, "Simulation: ON / OFF", toggleSimulationCommand_);
        XPLMAppendMenuItemWithCommand(
            menu_, "Cycle Atmosphere: LIVE / DRY / PERSISTENT", cycleAtmosphereCommand_);
        XPLMAppendMenuItemWithCommand(menu_, "Reset Trail", resetTrailCommand_);
        XPLMAppendMenuSeparator(menu_);
        XPLMAppendMenuItemWithCommand(
            menu_, "Reload Current Aircraft Geometry", reloadGeometryCommand_);
        XPLMAppendMenuItemWithCommand(
            menu_, "Export Live Debug Report", exportReportCommand_);
    }

    void destroyCommandsAndMenu() {
        if (menu_) {
            XPLMDestroyMenu(menu_);
            menu_ = nullptr;
        }
        for (XPLMCommandRef command : {
            toggleOverlayCommand_,
            toggleSimulationCommand_,
            cycleAtmosphereCommand_,
            resetTrailCommand_,
            reloadGeometryCommand_,
            exportReportCommand_}) {
            if (command) XPLMUnregisterCommandHandler(command, commandHandler, 1, this);
        }
    }

    std::filesystem::path pluginRoot_;
    std::filesystem::path reportPath_;
    std::filesystem::path activeAircraftPath_;
    std::unique_ptr<acf::AcfProfileService> profileService_;
    std::shared_ptr<const acf::ParseResult> currentGeometry_;
    std::vector<engine::Vec3d> engineExhaustBodyOffsets_;
    host::XPlaneSnapshotSource snapshotSource_;
    diagnostics::LiveSnapshotNormalizer normalizer_;
    engine::LiveContrailEngine liveEngine_;
    render::ContrailRenderPlannerSettings renderPlannerSettings_;
    render::ContrailRenderPlan latestRenderPlan_;
    ContrailWorldRenderer worldRenderer_;
    ContrailDebugOverlay overlay_;

    engine::SimulatorSnapshot latestSnapshot_ {};
    diagnostics::NormalizedReplaySample latestNormalized_ {};
    engine::ContrailTimelineSample latestTimeline_ {};
    std::string geometryStatus_ = "WAITING FOR ACF";
    AtmosphereMode atmosphereMode_ = AtmosphereMode::DryPreview;

    XPLMMenuID menu_ = nullptr;
    XPLMCommandRef toggleOverlayCommand_ = nullptr;
    XPLMCommandRef toggleSimulationCommand_ = nullptr;
    XPLMCommandRef cycleAtmosphereCommand_ = nullptr;
    XPLMCommandRef resetTrailCommand_ = nullptr;
    XPLMCommandRef reloadGeometryCommand_ = nullptr;
    XPLMCommandRef exportReportCommand_ = nullptr;

    bool enabled_ = false;
    bool visualEnabled_ = true;
    bool simulationEnabled_ = true;
    bool aircraftDirty_ = false;
    bool geometryReady_ = false;
    bool previewGateOpen_ = false;
    double latestPlannerTimeMs_ = 0.0;
    double maximumPlannerTimeMs_ = 0.0;
    std::uint64_t sequenceNumber_ = 0;
    std::uint64_t expectedGeneration_ = 0;
    std::uint64_t deliveredGeneration_ = 0;
};

ContrailDebugRuntime gRuntime;

}  // namespace
}  // namespace ffatmo

PLUGIN_API int XPluginStart(char* outName,
                            char* outSignature,
                            char* outDescription) {
    std::snprintf(outName, 256, "%s", "FFAtmo Contrail Visual Debug");
    std::snprintf(outSignature, 256, "%s", "com.freeflight.ffatmo.contraildebug");
    std::snprintf(
        outDescription,
        256,
        "%s",
        "Contrail Renderer v4 with deterministic curved planning and camera-facing world assets");
    return ffatmo::gRuntime.start() ? 1 : 0;
}

PLUGIN_API void XPluginStop() {
    ffatmo::gRuntime.stop();
}

PLUGIN_API int XPluginEnable() {
    return ffatmo::gRuntime.enable() ? 1 : 0;
}

PLUGIN_API void XPluginDisable() {
    ffatmo::gRuntime.disable();
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from,
                                      int message,
                                      void* parameter) {
    if (from == XPLM_PLUGIN_XPLANE &&
        message == XPLM_MSG_PLANE_LOADED &&
        reinterpret_cast<intptr_t>(parameter) == 0) {
        ffatmo::gRuntime.aircraftChanged();
    }
}
