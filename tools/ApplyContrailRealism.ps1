param(
    [string[]]$Paths = @(
        "assets/FFAtmo_particles.pss",
        "package/FFAtmo/assets/FFAtmo_particles.pss"
    )
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Replace-UniqueOrApplied {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Old,
        [Parameter(Mandatory = $true)][string]$New,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $oldCount = ([regex]::Matches($Text, [regex]::Escape($Old))).Count
    $newCount = ([regex]::Matches($Text, [regex]::Escape($New))).Count

    if ($oldCount -eq 0) {
        if ($newCount -gt 0) {
            Write-Host "Calibration '$Label' is already applied."
            return $Text
        }
        throw "Calibration '$Label' could not find either the original or calibrated value."
    }

    if ($oldCount -ne 1) {
        throw "Calibration '$Label' expected one original match, found $oldCount."
    }

    Write-Host "Calibration '$Label' applied."
    return $Text.Replace($Old, $New)
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
    $subPattern = "(?ms)(^SUB_EMITTER\r?\n PARTICLE_TYPE " + $ParticleType + "\r?\n.*?)(?=^SUB_EMITTER\r?\n|\z)"
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

    $updatedSubBlock = $subBlock.Replace($Old, $New)
    $updatedEmitterBlock = $emitterBlock.Substring(0, $subMatch.Index) + $updatedSubBlock + $emitterBlock.Substring($subMatch.Index + $subMatch.Length)
    Write-Host "Calibration '$Label' applied in $EmitterName particle type $ParticleType."
    return $Text.Substring(0, $emitterMatch.Index) + $updatedEmitterBlock + $Text.Substring($emitterMatch.Index + $emitterMatch.Length)
}

foreach ($path in $Paths) {
    if (-not (Test-Path $path)) {
        throw "Particle definition not found: $path"
    }

    $text = Get-Content $path -Raw
    $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }

    $text = Replace-UniqueOrApplied $text `
        (" NAME ffatmo_contrail_core${newline} MAX_PARTICLES 6000") `
        (" NAME ffatmo_contrail_core${newline} MAX_PARTICLES 16000") `
        "core particle budget"

    $text = Replace-UniqueOrApplied $text `
        ("`t0.000000`t1.200000${newline}`t0.180000`t2.400000${newline}`t1.000000`t6.000000") `
        ("`t0.000000`t2.200000${newline}`t0.140000`t5.800000${newline}`t0.450000`t10.000000${newline}`t1.000000`t18.000000") `
        "core volumetric expansion"

    $text = Replace-UniqueOrApplied $text `
        ("`t0.000000`t0.000000${newline}`t0.050000`t0.180000${newline}`t0.650000`t0.120000${newline}`t1.000000`t0.000000") `
        ("`t0.000000`t0.000000${newline}`t0.035000`t0.100000${newline}`t0.160000`t0.220000${newline}`t0.520000`t0.180000${newline}`t0.820000`t0.070000${newline}`t1.000000`t0.000000") `
        "core soft opacity curve"

    $text = Replace-UniqueOrApplied $text `
        (" NAME ffatmo_primary_wake${newline} MAX_PARTICLES 8000") `
        (" NAME ffatmo_primary_wake${newline} MAX_PARTICLES 11000") `
        "primary wake budget"

    $text = Replace-UniqueOrApplied $text `
        (" NAME ffatmo_secondary_curtain${newline} MAX_PARTICLES 12000") `
        (" NAME ffatmo_secondary_curtain${newline} MAX_PARTICLES 15000") `
        "secondary curtain budget"

    foreach ($emitter in @("FFATMO_ENGINE_LEFT", "FFATMO_ENGINE_RIGHT")) {
        $text = Replace-InSubEmitter $text $emitter 0 `
            ("`t1.000000`t145.000000`t165.000000") `
            ("`t1.000000`t360.000000`t430.000000") `
            "engine core density"

        $text = Replace-InSubEmitter $text $emitter 0 `
            ("`t1.000000`t0.650000`t1.000000") `
            ("`t1.000000`t1.250000`t2.100000") `
            "engine core size variation"

        $text = Replace-InSubEmitter $text $emitter 0 `
            ("`t1.000000`t0.800000`t1.000000") `
            ("`t1.000000`t0.650000`t0.880000") `
            "engine core alpha variation"

        $text = Replace-InSubEmitter $text $emitter 0 `
            ("`t0.000000`t8.000000`t12.000000${newline}`t1.000000`t8.000000`t12.000000") `
            ("`t0.000000`t16.000000`t24.000000${newline}`t1.000000`t16.000000`t24.000000") `
            "engine core lifetime"

        $text = Replace-InSubEmitter $text $emitter 4 `
            ("`t1.000000`t10.000000`t14.000000") `
            ("`t1.000000`t12.000000`t16.000000") `
            "primary wake density"

        $text = Replace-InSubEmitter $text $emitter 5 `
            ("`t1.000000`t6.000000`t9.000000") `
            ("`t1.000000`t4.000000`t6.000000") `
            "secondary curtain density"

        $text = Replace-InSubEmitter $text $emitter 1 `
            ("`t1.000000`t7.000000`t11.000000") `
            ("`t1.000000`t2.000000`t4.000000") `
            "cirrus transition density"
    }

    Set-Content -Path $path -Value $text -Encoding utf8 -NoNewline
    Write-Host "Applied dense continuous contrail calibration to $path"
}
