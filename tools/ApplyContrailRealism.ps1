param(
    [string[]]$Paths = @(
        "assets/FFAtmo_particles.pss",
        "package/FFAtmo/assets/FFAtmo_particles.pss"
    )
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Replace-Literal {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Old,
        [Parameter(Mandatory = $true)][string]$New,
        [Parameter