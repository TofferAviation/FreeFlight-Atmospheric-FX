Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$sourcePath = "app/ImGuiCompanion.cpp"
if (-not (Test-Path $sourcePath)) {
    throw "Companion source not found: $sourcePath"
}

$stableBackgroundPath = "package/FFAtmo/ui/assets/backgrounds/background_atmospheric_clean.png"
if (-not (Test-Path $stableBackgroundPath)) {
    throw "Stable atmospheric background not found: $stableBackgroundPath"
}

$text = Get-Content $sourcePath -Raw
$brokenBackground = 'login/backgrounds/login_background_clean.png'
$stableBackground = 'backgrounds/background_atmospheric_clean.png'

if ($text.Contains($brokenBackground)) {
    $text = $text.Replace($brokenBackground, $stableBackground)
    Write-Host "Replaced the corrupted login background with the stable atmospheric background."
} elseif ($text.Contains($stableBackground)) {
    Write-Host "Stable login background is already selected."
} else {
    throw "Could not locate the login background asset reference."
}

$oldGithubLabel = 'centeredText(contentX+35,662,40,"GH",C(4,17,33),.72f);'
$newGithubIcon = 'image("login/icons/github_mark.png",contentX+37,656,36,36);'
if ($text.Contains($oldGithubLabel)) {
    $text = $text.Replace($oldGithubLabel, $newGithubIcon)
    Write-Host "Replaced the temporary GH badge with the supplied GitHub mark."
} elseif ($text.Contains($newGithubIcon)) {
    Write-Host "Supplied GitHub mark is already wired into the login page."
} else {
    throw "Could not locate the temporary GitHub badge implementation."
}

Set-Content -Path $sourcePath -Value $text -Encoding utf8 -NoNewline

$payloadPath = "package/FFAtmo/ui/assets/login/icons/github_mark.png.b64"
$iconPath = "package/FFAtmo/ui/assets/login/icons/github_mark.png"
if (-not (Test-Path $payloadPath)) {
    throw "GitHub icon payload not found: $payloadPath"
}

$payload = (Get-Content $payloadPath -Raw) -replace '\s',''
if ([string]::IsNullOrWhiteSpace($payload) -or ($payload.Length % 4) -ne 0) {
    throw "GitHub icon payload has an invalid Base64 length."
}

try {
    $bytes = [Convert]::FromBase64String($payload)
} catch {
    throw "GitHub icon payload is not valid Base64: $($_.Exception.Message)"
}

if ($bytes.Length -lt 100) {
    throw "Decoded GitHub icon payload is unexpectedly small."
}

$pngSignature = [byte[]](137,80,78,71,13,10,26,10)
for ($i = 0; $i -lt $pngSignature.Length; ++$i) {
    if ($bytes[$i] -ne $pngSignature[$i]) {
        throw "Decoded GitHub icon payload is not a PNG file."
    }
}

$iconDirectory = Split-Path -Parent $iconPath
New-Item -ItemType Directory -Path $iconDirectory -Force | Out-Null
$tempIconPath = "$iconPath.tmp"
[IO.File]::WriteAllBytes($tempIconPath, $bytes)
Move-Item $tempIconPath $iconPath -Force
Remove-Item $payloadPath -Force
Write-Host "Generated and validated the supplied transparent GitHub icon at $iconPath."
