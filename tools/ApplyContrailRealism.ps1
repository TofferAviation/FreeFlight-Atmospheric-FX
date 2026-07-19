param(
    [string[]]$Paths = @(
        "assets/FFAtmo_particles.pss",
        "package/FFAtmo/assets/FFAtmo_particles.pss"
    )
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Replace-Idempotent {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Old,
        [Parameter(Mandatory = $true)][string]$New,
        [Parameter(Mandatory = $true)][int]$MinimumOldCount,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $oldCount = ([regex]::Matches($Text, [regex]::Escape($Old))).Count
    $newCount = ([regex]::Matches($Text, [regex]::Escape($New))).Count

    if ($oldCount -eq 0) {
        if ($newCount -gt 0) {
            Write-Host "Calibration '$Label' is already applied ($newCount match(es))."
            return $Text
        }
        throw "Calibration '$Label' could not find either its original or calibrated value."
    }

    if ($oldCount -lt $MinimumOldCount) {
        throw "Calibration '$Label' expected at least $MinimumOldCount original match(es), found $oldCount."
    }

    Write-Host "Calibration '$Label': replacing $oldCount match(es)."
    return $Text.Replace($Old, $New)
}

foreach ($path in $Paths) {
    if (-not (Test-Path $path)) {
        throw "Particle definition not found: $path"
    }

    $text = Get-Content $path -Raw
    $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }

    $text = Replace-Idempotent $text `
        (" NAME ffatmo_contrail_core${newline} MAX_PARTICLES 6000") `
        (" NAME ffatmo_contrail_core${newline} MAX_PARTICLES 9000") 1 "core particle budget"

    $text = Replace-Idempotent $text `
        ("`t0.000000`t1.200000${newline}`t0.180000`t2.400000${newline}`t1.000000`t6.000000") `
        ("`t0.000000`t1.000000${newline}`t0.180000`t2.250000${newline}`t1.000000`t6.800000") 1 "core expansion"

    $text = Replace-Idempotent $text `
        ("`t0.000000`t0.000000${newline}`t0.050000`t0.180000${newline}`t0.650000`t0.120000${newline}`t1.000000`t0.000000") `
        ("`t0.000000`t0.000000${newline}`t0.045000`t0.145000${newline}`t0.300000`t0.155000${newline}`t0.720000`t0.090000${newline}`t1.000000`t0.000000") 1 "core opacity curve"

    $text = Replace-Idempotent $text `
        (" NAME ffatmo_primary_wake${newline} MAX_PARTICLES 8000") `
        (" NAME ffatmo_primary_wake${newline} MAX_PARTICLES 11000") 1 "primary wake budget"

    $text = Replace-Idempotent $text `
        (" NAME ffatmo_secondary_curtain${newline} MAX_PARTICLES 12000") `
        (" NAME ffatmo_secondary_curtain${newline} MAX_PARTICLES 15000") 1 "secondary curtain budget"

    $text = Replace-Idempotent $text `
        ("`t1.000000`t145.000000`t165.000000") `
        ("`t1.000000`t225.000000`t250.000000") 2 "engine core density"

    $text = Replace-Idempotent $text `
        ("`t1.000000`t0.650000`t1.000000") `
        ("`t1.000000`t0.550000`t0.900000") 2 "engine core size variation"

    $text = Replace-Idempotent $text `
        ("`t1.000000`t0.800000`t1.000000") `
        ("`t1.000000`t0.580000`t0.780000") 2 "engine core alpha variation"

    $text = Replace-Idempotent $text `
        ("`t0.000000`t8.000000`t12.000000${newline}`t1.000000`t8.000000`t12.000000") `
        ("`t0.000000`t10.000000`t16.000000${newline}`t1.000000`t10.000000`t16.000000") 2 "engine core lifetime"

    $text = Replace-Idempotent $text `
        ("`t1.000000`t10.000000`t14.000000") `
        ("`t1.000000`t14.000000`t19.000000") 2 "primary wake density"

    $text = Replace-Idempotent $text `
        ("`t1.000000`t6.000000`t9.000000") `
        ("`t1.000000`t9.000000`t13.000000") 2 "secondary curtain density"

    $text = Replace-Idempotent $text `
        ("`t1.000000`t7.000000`t11.000000") `
        ("`t1.000000`t10.000000`t15.000000") 2 "cirrus transition density"

    Set-Content -Path $path -Value $text -Encoding utf8 -NoNewline
    Write-Host "Applied layered contrail realism calibration to $path"
}
