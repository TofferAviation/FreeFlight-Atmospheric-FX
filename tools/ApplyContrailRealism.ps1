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

function Replace-InEmitter {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$EmitterName,
        [Parameter(Mandatory = $true)][string]$Old,
        [Parameter(Mandatory = $true)][string]$New,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $pattern = "(?ms)(^EMITTER\r?\n NAME " + [regex]::Escape($EmitterName) + "\r?\n.*?)(?=^EMITTER\r?\n|\z)"
    $match = [regex]::Match($Text, $pattern)
    if (-not $match.Success) {
        throw "Emitter '$EmitterName' was not found while applying '$Label'."
    }

    $block = $match.Value
    $oldCount = ([regex]::Matches($block, [regex]::Escape($Old))).Count
    $newCount = ([regex]::Matches($block, [regex]::Escape($New))).Count

    if ($oldCount -eq 0) {
        if ($newCount -eq 1) {
            Write-Host "Calibration '$Label' is already applied in $EmitterName."
            return $Text
        }
        throw "Calibration '$Label' could not find the original value in $EmitterName."
    }

    if ($oldCount -ne 1) {
        throw "Calibration '$Label' expected one match in $EmitterName, found $oldCount."
    }

    $updatedBlock = $block.Replace($Old, $New)
    Write-Host "Calibration '$Label' applied in $EmitterName."
    return $Text.Substring(0, $match.Index) + $updatedBlock + $Text.Substring($match.Index + $match.Length)
}

foreach ($path in $Paths) {
    if (-not (Test-Path $path)) {
        throw "Particle definition not found: $path"
    }

    $text = Get-Content $path -Raw
    $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }

    $text = Replace-UniqueOrApplied $text `
        (" NAME ffatmo_contrail_core${newline} MAX_PARTICLES 6000") `
        (" NAME ffatmo_contrail_core${newline} MAX_PARTICLES 9000") `
        "core particle budget"

    $text = Replace-UniqueOrApplied $text `
        ("`t0.000000`t1.200000${newline}`t0.180000`t2.400000${newline}`t1.000000`t6.000000") `
        ("`t0.000000`t1.000000${newline}`t0.180000`t2.250000${newline}`t1.000000`t6.800000") `
        "core expansion"

    $text = Replace-UniqueOrApplied $text `
        ("`t0.000000`t0.000000${newline}`t0.050000`t0.180000${newline}`t0.650000`t0.120000${newline}`t1.000000`t0.000000") `
        ("`t0.000000`t0.000000${newline}`t0.045000`t0.145000${newline}`t0.300000`t0.155000${newline}`t0.720000`t0.090000${newline}`t1.000000`t0.000000") `
        "core opacity curve"

    $text = Replace-UniqueOrApplied $text `
        (" NAME ffatmo_primary_wake${newline} MAX_PARTICLES 8000") `
        (" NAME ffatmo_primary_wake${newline} MAX_PARTICLES 11000") `
        "primary wake budget"

    $text = Replace-UniqueOrApplied $text `
        (" NAME ffatmo_secondary_curtain${newline} MAX_PARTICLES 12000") `
        (" NAME ffatmo_secondary_curtain${newline} MAX_PARTICLES 15000") `
        "secondary curtain budget"

    foreach ($emitter in @("FFATMO_ENGINE_LEFT", "FFATMO_ENGINE_RIGHT")) {
        $text = Replace-InEmitter $text $emitter `
            ("`t1.000000`t145.000000`t165.000000") `
            ("`t1.000000`t225.000000`t250.000000") `
            "engine core density"

        $text = Replace-InEmitter $text $emitter `
            ("`t1.000000`t0.650000`t1.000000") `
            ("`t1.000000`t0.550000`t0.900000") `
            "engine core size variation"

        $text = Replace-InEmitter $text $emitter `
            ("`t1.000000`t0.800000`t1.000000") `
            ("`t1.000000`t0.580000`t0.780000") `
            "engine core alpha variation"

        $text = Replace-InEmitter $text $emitter `
            ("`t0.000000`t8.000000`t12.000000${newline}`t1.000000`t8.000000`t12.000000") `
            ("`t0.000000`t10.000000`t16.000000${newline}`t1.000000`t10.000000`t16.000000") `
            "engine core lifetime"

        $text = Replace-InEmitter $text $emitter `
            ("`t1.000000`t10.000000`t14.000000") `
            ("`t1.000000`t14.000000`t19.000000") `
            "primary wake density"

        $text = Replace-InEmitter $text $emitter `
            ("`t1.000000`t6.000000`t9.000000") `
            ("`t1.000000`t9.000000`t13.000000") `
            "secondary curtain density"

        $text = Replace-InEmitter $text $emitter `
            ("`t1.000000`t7.000000`t11.000000") `
            ("`t1.000000`t10.000000`t15.000000") `
            "cirrus transition density"
    }

    Set-Content -Path $path -Value $text -Encoding utf8 -NoNewline
    Write-Host "Applied layered contrail realism calibration to $path"
}
