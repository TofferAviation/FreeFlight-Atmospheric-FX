from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIVE = ROOT / "src/engine/LiveContrailEngine.cpp"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    source = LIVE.read_text(encoding="utf-8")

    emission_anchor = """                parcel.wakeFluid = initializeWakeFluidState(
                    wakeAircraft_,
                    wakeEnvironment,
                    wakeRight,
                    wakeUp,
                    static_cast<float>(bodyOffset.x),
                    static_cast<float>(bodyOffset.y),
                    wakeFluidSettings_);
                parcel.vortexRightWorld = parcel.wakeFluid.rightWorld;
"""
    emission_replacement = """                parcel.wakeFluid = initializeWakeFluidState(
                    wakeAircraft_,
                    wakeEnvironment,
                    wakeRight,
                    wakeUp,
                    static_cast<float>(bodyOffset.x),
                    static_cast<float>(bodyOffset.y),
                    wakeFluidSettings_);

                // v6.9: seed a persistent material cross-section at emission.
                // Each marker owns an independent WakeFluidState and therefore
                // samples the finite-core pair at a different point in the wake.
                WakeVortexSheetSettings sheetSettings;
                sheetSettings.plumeHalfWidthM = std::max(
                    settings_.initialParcelRadiusM, 0.25f);
                parcel.vortexSheet = initializeWakeVortexSheet(
                    parcel.id,
                    parcel.worldPositionM,
                    wakeAircraft_,
                    wakeEnvironment,
                    wakeRight,
                    wakeUp,
                    static_cast<float>(bodyOffset.x),
                    static_cast<float>(bodyOffset.y),
                    wakeFluidSettings_,
                    sheetSettings);

                parcel.vortexRightWorld = parcel.wakeFluid.rightWorld;
"""
    source = replace_once(
        source, emission_anchor, emission_replacement, "v6.9 emission integration")

    wind_anchor = """        const double windEastMps = snapshot.atmosphere.windLocalMps.x;
        const double windUpMps = snapshot.atmosphere.windLocalMps.y;
        const double windNorthMps = -snapshot.atmosphere.windLocalMps.z;

        for (auto& parcel : parcels_) {
"""
    wind_replacement = """        const double windEastMps = snapshot.atmosphere.windLocalMps.x;
        const double windUpMps = snapshot.atmosphere.windLocalMps.y;
        const double windNorthMps = -snapshot.atmosphere.windLocalMps.z;
        const Vec3d ambientWakeVelocityWorld {
            windEastMps,
            windUpMps,
            windNorthMps
        };

        for (auto& parcel : parcels_) {
"""
    source = replace_once(
        source, wind_anchor, wind_replacement, "v6.9 ambient velocity integration")

    advance_anchor = """            parcel.worldPositionM.x += windEastMps * deltaSeconds;
            parcel.worldPositionM.y += windUpMps * deltaSeconds;
            parcel.worldPositionM.z += windNorthMps * deltaSeconds;

            const WakeFluidStepResult wakeStep = advanceWakeFluidState(
"""
    advance_replacement = """            parcel.worldPositionM.x += windEastMps * deltaSeconds;
            parcel.worldPositionM.y += windUpMps * deltaSeconds;
            parcel.worldPositionM.z += windNorthMps * deltaSeconds;

            // v6.9 physical path. The sheet is advanced independently of the
            // legacy centreline parcel so later rendering can consume final
            // marker positions directly instead of inventing render-time curls.
            WakeVortexSheetSettings sheetSettings;
            sheetSettings.plumeHalfWidthM = std::max(
                settings_.initialParcelRadiusM, 0.25f);
            const WakeVortexSheetStepResult sheetStep = advanceWakeVortexSheet(
                parcel.vortexSheet,
                parcel.ageSeconds,
                deltaSeconds,
                ambientWakeVelocityWorld,
                wakeEnvironment,
                wakeFluidSettings_,
                sheetSettings);
            if (!sheetStep.finite) {
                encounteredNonFinite_ = true;
            }

            const WakeFluidStepResult wakeStep = advanceWakeFluidState(
"""
    source = replace_once(
        source, advance_anchor, advance_replacement, "v6.9 sheet advance integration")

    LIVE.write_text(source, encoding="utf-8")
    print("Applied v6.9 persistent Lagrangian vortex-sheet runtime integration")


if __name__ == "__main__":
    main()
