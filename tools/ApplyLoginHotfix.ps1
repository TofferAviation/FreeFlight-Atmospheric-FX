Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$sourcePath = "app/ImGuiCompanion.cpp"
if (-not (Test-Path $sourcePath)) {
    throw "Companion source not found: $sourcePath"
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
[IO.File]::WriteAllBytes($iconPath, [Convert]::FromBase64String($payload))
Remove-Item $payloadPath -Force
Write-Host "Generated the supplied transparent GitHub icon at $iconPath."
