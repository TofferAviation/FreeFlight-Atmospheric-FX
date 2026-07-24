param(
    [string[]]$Paths = @(
        "assets/FFAtmo_particles.pss",
        "package/FFAtmo/assets/FFAtmo_particles.pss"
    )
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Replace-Scoped {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$ScopeLabel,
        [Parameter(Mandatory = $true)][string]$Old,
        [Parameter(Mandatory = $true)][string]$New,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $match = [regex]::Match($Text, $Pattern)
    if (-not $match.Success) {
        throw "Could not find $ScopeLabel while applying '$Label'."
    }

    $block = $match.Value
    $oldCount = ([regex]::Matches($block, [regex]::Escape($Old))).Count
    $newCount = ([regex]::Matches($block, [regex]::Escape($New))).Count
    if ($oldCount -eq 0) {
        if ($newCount -eq 1) {
            Write-Host "Calibration '$Label' is already applied in $ScopeLabel."
            return $Text
        }
        throw "Calibration '$Label' could not find the original value in $ScopeLabel."
    }
    if ($oldCount -ne 1) {
        throw "Calibration '$Label' expected one match in $ScopeLabel, found $oldCount."
    }

    $updated = $block.Replace($Old, $New)
    Write-Host "Calibration '$Label' applied in $ScopeLabel."
    return $Text.Substring(0, $match.Index) + $updated + $Text.Substring($match.Index + $match.Length)
}

function Replace-InParticle {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$ParticleName,
        [Parameter(Mandatory = $true)][string]$Old,
        [Parameter(Mandatory = $true)][string]$New,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $pattern = "(?ms)(^PARTICLE\r?\n NAME " + [regex]::Escape($ParticleName) + "\r?\n.*?^END_PARTICLE\r?\n)"
    return Replace-Scoped $Text $pattern "particle $ParticleName" $Old $New $Label
}

function Replace-InSubEmitter {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$EmitterName,
        [Parameter(Mandatory = $true)][int]$ParticleType,
        [Parameter(Mandatory = $true)][string]$Old,
        [Parameter(Mandatory = $true)][string]$New,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $emitterPattern = "(?ms)(^EMITTER\r?\n NAME " + [regex]::Escape($EmitterName) + "\r?\n.*?)(?=^EMITTER\r?\n|\z)"
    $emitterMatch = [regex]::Match($Text, $emitterPattern)
    if (-not $emitterMatch.Success) {
        throw "Emitter '$EmitterName' was not found while applying '$Label'."
    }

    $emitterBlock = $emitterMatch.Value
    $subPattern = "(?ms)(^SUB_EMITTER\r?\n PARTICLE_TYPE " + $ParticleType + "\r?\n.*?)(?=^SUB_EMITTER\r?\n|^DATAREFS|\z)"
    $subMatch = [regex]::Match($emitterBlock, $subPattern)
    if (-not $subMatch.Success) {
        throw "Particle type $ParticleType was not found in $EmitterName while applying '$Label'."
    }

    $subBlock = $subMatch.Value
    $oldCount = ([regex]::Matches($subBlock, [regex]::Escape($Old))).Count
    $newCount = ([regex]::Matches($subBlock, [regex]::Escape($New))).Count
    if ($oldCount -eq 0) {
        if ($newCount -eq 1) {
            Write-Host "Calibration '$Label' is already applied in $EmitterName particle type $ParticleType."
            return $Text
        }
        throw "Calibration '$Label' could not find the original value in $EmitterName particle type $ParticleType."
    }
    if ($oldCount -ne 1) {
        throw "Calibration '$Label' expected one match in $EmitterName particle type $ParticleType, found $oldCount."
    }

    $updatedSub = $subBlock.Replace($Old, $New)
    $updatedEmitter = $emitterBlock.Substring(0, $subMatch.Index) + $updatedSub + $emitterBlock.Substring($subMatch.Index + $subMatch.Length)
    Write-Host "Calibration '$Label' applied in $EmitterName particle type $ParticleType."
    return $Text.Substring(0, $emitterMatch.Index) + $updatedEmitter + $Text.Substring($emitterMatch.Index + $emitterMatch.Length)
}

foreach ($path in $Paths) {
    if (-not (Test-Path $path)) {
        throw "Particle definition not found: $path"
    }

    $text = Get-Content $path -Raw
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }

    # The young engine cores must stay narrower than the 9.0 m engine spacing.
    $text = Replace-InParticle $text "ffatmo_contrail_core" `
        " MAX_PARTICLES 6000" " MAX_PARTICLES 12000" "core particle budget"
    $text = Replace-InParticle $text "ffatmo_contrail_core" `
        ("`t0.000000`t1.200000${nl}`t0.180000`t2.400000${nl}`t1.000000`t6.000000") `
        ("`t0.000000`t0.800000${nl}`t0.140000`t1.250000${nl}`t0.420000`t1.650000${nl}`t0.720000`t1.150000${nl}`t1.000000`t0.650000") `
        "narrow separated core size"
    $text = Replace-InParticle $text "ffatmo_contrail_core" `
        ("`t0.000000`t0.000000${nl}`t0.050000`t0.180000${nl}`t0.650000`t0.120000${nl}`t1.000000`t0.000000") `
        ("`t0.000000`t0.000000${nl}`t0.035000`t0.090000${nl}`t0.140000`t0.180000${nl}`t0.500000`t0.150000${nl}`t0.800000`t0.050000${nl}`t1.000000`t0.000000") `
        "soft separated core opacity"

    # These particles are now short wisps emitted by moving wake parcels, not giant self-propelled loops.
    $text = Replace-InParticle $text "ffatmo_primary_wake" `
        " MAX_PARTICLES 8000" " MAX_PARTICLES 10000" "primary wake budget"
    $text = Replace-InParticle $text "ffatmo_primary_wake" `
        ("`t0.000000`t1.500000${nl}`t0.180000`t4.000000${nl}`t0.520000`t10.000000${nl}`t1.000000`t20.000000") `
        ("`t0.000000`t0.900000${nl}`t0.160000`t1.700000${nl}`t0.420000`t2.200000${nl}`t0.720000`t1.150000${nl}`t1.000000`t0.350000") `
        "compact solver wake size"
    $text = Replace-InParticle $text "ffatmo_primary_wake" `
        ("`t0.000000`t0.000000${nl}`t0.060000`t0.000000${nl}`t0.160000`t0.180000${nl}`t0.300000`t0.280000${nl}`t0.650000`t0.100000${nl}`t1.000000`t0.000000") `
        ("`t0.000000`t0.000000${nl}`t0.040000`t0.060000${nl}`t0.140000`t0.150000${nl}`t0.360000`t0.180000${nl}`t0.680000`t0.060000${nl}`t1.000000`t0.000000") `
        "compact solver wake fade"

    $text = Replace-InParticle $text "ffatmo_wingtip_vortex" `
        " MAX_PARTICLES 4000" " MAX_PARTICLES 6000" "wingtip vortex budget"
    $text = Replace-InParticle $text "ffatmo_wingtip_vortex" `
        ("`t0.000000`t0.250000${nl}`t0.250000`t2.500000${nl}`t0.650000`t8.000000${nl}`t1.000000`t18.000000") `
        ("`t0.000000`t0.750000${nl}`t0.180000`t1.350000${nl}`t0.480000`t1.600000${nl}`t0.760000`t0.700000${nl}`t1.000000`t0.200000") `
        "small wingtip vortex size"
    $text = Replace-InParticle $text "ffatmo_wingtip_vortex" `
        ("`t0.000000`t0.220000${nl}`t0.220000`t0.135000${nl}`t0.700000`t0.045000${nl}`t1.000000`t0.000000") `
        ("`t0.000000`t0.000000${nl}`t0.050000`t0.130000${nl}`t0.250000`t0.110000${nl}`t0.650000`t0.035000${nl}`t1.000000`t0.000000") `
        "small wingtip vortex fade"

    foreach ($emitter in @("FFATMO_ENGINE_LEFT", "FFATMO_ENGINE_RIGHT")) {
        $text = Replace-InSubEmitter $text $emitter 0 `
            "`t1.000000`t145.000000`t165.000000" `
            "`t1.000000`t260.000000`t320.000000" `
            "engine core density"
        $text = Replace-InSubEmitter $text $emitter 0 `
            "`t1.000000`t0.650000`t1.000000" `
            "`t1.000000`t0.550000`t0.800000" `
            "engine core size variation"
        $text = Replace-InSubEmitter $text $emitter 0 `
            "`t1.000000`t0.800000`t1.000000" `
            "`t1.000000`t0.480000`t0.680000" `
            "engine core alpha variation"
        $text = Replace-InSubEmitter $text $emitter 0 `
            ("`t0.000000`t8.000000`t12.000000${nl}`t1.000000`t8.000000`t12.000000") `
            ("`t0.000000`t4.500000`t6.500000${nl}`t1.000000`t4.500000`t6.500000") `
            "young core lifetime"

        $text = Replace-InSubEmitter $text $emitter 4 `
            "`t1.000000`t10.000000`t14.000000" `
            "`t1.000000`t12.000000`t18.000000" `
            "wake parcel density"
        $text = Replace-InSubEmitter $text $emitter 4 `
            ("`t0.000000`t1.200000`t1.800000${nl}`t1.000000`t1.200000`t1.800000") `
            ("`t0.000000`t0.050000`t0.200000${nl}`t1.000000`t0.050000`t0.200000") `
            "wake parcel residual speed"
        $text = Replace-InSubEmitter $text $emitter 4 `
            ("`t0.000000`t0.080000`t0.220000${nl}`t1.000000`t0.080000`t0.220000") `
            ("`t0.000000`t0.010000`t0.040000${nl}`t1.000000`t0.010000`t0.040000") `
            "wake parcel spawn radius"

        $text = Replace-InSubEmitter $text $emitter 5 `
            "`t1.000000`t6.000000`t9.000000" `
            "`t1.000000`t2.000000`t3.000000" `
            "secondary curtain density"
        $text = Replace-InSubEmitter $text $emitter 1 `
            "`t1.000000`t7.000000`t11.000000" `
            "`t1.000000`t1.000000`t2.000000" `
            "cirrus transition density"
    }

    $text = Replace-InSubEmitter $text "FFATMO_WING_VORTEX" 3 `
        "`t1.000000`t18.000000`t28.000000" `
        "`t1.000000`t55.000000`t80.000000" `
        "continuous small wingtip vortex rate"
    $text = Replace-InSubEmitter $text "FFATMO_WING_VORTEX" 3 `
        "`t1.000000`t0.700000`t1.000000" `
        "`t1.000000`t0.500000`t0.750000" `
        "wingtip vortex size variation"
    $text = Replace-InSubEmitter $text "FFATMO_WING_VORTEX" 3 `
        "`t1.000000`t0.650000`t1.000000" `
        "`t1.000000`t0.280000`t0.450000" `
        "wingtip vortex alpha variation"
    $text = Replace-InSubEmitter $text "FFATMO_WING_VORTEX" 3 `
        ("`t0.000000`t0.000000`t0.000000${nl}`t1.000000`t7.000000`t14.000000") `
        ("`t0.000000`t0.000000`t0.000000${nl}`t1.000000`t3.000000`t5.000000") `
        "wingtip vortex lifetime"

    Set-Content -Path $path -Value $text -Encoding utf8 -NoNewline
    Write-Host "Applied narrow-core calibration for the age-based wake solver to $path"
}
