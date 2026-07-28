from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEST = ROOT / "tests/LiveContrailDebugTests.cpp"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    source = TEST.read_text(encoding="utf-8")

    source = replace_once(
        source,
        '#include "engine/WakeFluidSolver.h"\n',
        '#include "engine/WakeFluidSolver.h"\n#include "render/VortexSheetRenderField.h"\n',
        "v6.9 render-field include",
    )

    anchor = '''    require(observedRollup,
            "live parcels receive finite-core vortex displacement");

    std::cout << "FFAtmo Wake Fluid Simulation v1 and B738 nucleation tests passed\\n";
'''

    replacement = '''    require(observedRollup,
            "live parcels receive finite-core vortex displacement");

    bool observedVortexSheet = false;
    bool observedSecondaryWake = false;
    bool observedCrossSectionSeparation = false;
    for (const auto& parcel : geometryEngine.parcels()) {
        if (!parcel.vortexSheet.initialized) continue;
        observedVortexSheet = true;

        const auto* inner = engine::wakeSheetMarker(
            parcel.vortexSheet, engine::WakeSheetLane::Inner);
        const auto* outer = engine::wakeSheetMarker(
            parcel.vortexSheet, engine::WakeSheetLane::Outer);
        const auto* secondary = engine::wakeSheetMarker(
            parcel.vortexSheet, engine::WakeSheetLane::Secondary);
        require(inner != nullptr && outer != nullptr && secondary != nullptr,
                "v6.9 wake section exposes stable material lanes");

        const double dx = outer->worldPositionM.x - inner->worldPositionM.x;
        const double dy = outer->worldPositionM.y - inner->worldPositionM.y;
        const double dz = outer->worldPositionM.z - inner->worldPositionM.z;
        const double separation = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (separation > 0.50) observedCrossSectionSeparation = true;
        if (secondary->activeForRendering) observedSecondaryWake = true;
    }
    require(observedVortexSheet,
            "live engine initializes v6.9 persistent vortex-sheet sections");
    require(observedCrossSectionSeparation,
            "inner and outer material markers remain a finite wake cross-section");
    require(observedSecondaryWake,
            "secondary material lane becomes visible after organised roll-up onset");

    const auto renderField = render::buildVortexSheetRenderField(geometryEngine.parcels());
    require(renderField.metrics.initializedSectionCount > 0,
            "v6.9 render field consumes initialized wake sections");
    require(renderField.metrics.renderPointCount > renderField.metrics.initializedSectionCount,
            "render field contains multiple persistent lanes per wake section");
    require(renderField.metrics.pointCountByLane[
                static_cast<std::size_t>(engine::WakeSheetLane::Inner)] > 0 &&
            renderField.metrics.pointCountByLane[
                static_cast<std::size_t>(engine::WakeSheetLane::Core)] > 0 &&
            renderField.metrics.pointCountByLane[
                static_cast<std::size_t>(engine::WakeSheetLane::Outer)] > 0,
            "inner/core/outer lanes survive into final render geometry");
    require(renderField.metrics.maximumLaneGapM > 0.0,
            "render field measures actual longitudinal lane continuity");

    bool observedFiniteCurvedTangent = false;
    for (const auto& point : renderField.points) {
        const double tangentLength = std::sqrt(
            point.worldTangent.x * point.worldTangent.x +
            point.worldTangent.y * point.worldTangent.y +
            point.worldTangent.z * point.worldTangent.z);
        require(std::isfinite(tangentLength) && tangentLength > 0.99 && tangentLength < 1.01,
                "render tangents are finite normalized vectors");
        if (std::abs(point.worldTangent.y) > 1.0e-4 ||
            std::abs(point.worldTangent.z) > 1.0e-4) {
            observedFiniteCurvedTangent = true;
        }
    }
    require(observedFiniteCurvedTangent,
            "v6.9 geometry tangents follow displaced marker lanes rather than a fixed centreline");

    std::cout << "FFAtmo Wake Fluid Simulation v1 and v6.9 vortex-sheet tests passed\\n";
'''

    source = replace_once(source, anchor, replacement, "v6.9 deterministic tests")
    TEST.write_text(source, encoding="utf-8")
    print("Applied v6.9 vortex-sheet deterministic tests")


if __name__ == "__main__":
    main()
