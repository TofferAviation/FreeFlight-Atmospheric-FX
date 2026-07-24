Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$path = "app/ImGuiCompanion.cpp"
if (-not (Test-Path $path)) {
    throw "Companion source not found: $path"
}

$text = Get-Content $path -Raw
$newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }

if (-not $text.Contains("bool syncFxPackageToXPlane()")) {
    $anchor = "void connectRoot() {"
    if (-not $text.Contains($anchor)) {
        throw "Could not find connectRoot() insertion point."
    }

    $syncCode = @'
fs::path findInstalledFxPluginRoot() {
    if (gXPlaneRoot.empty()) return {};
    const fs::path plugins = gXPlaneRoot / "Resources" / "plugins";
    std::error_code ec;
    if (fs::exists(plugins, ec)) {
        for (const auto& item : fs::directory_iterator(plugins, ec)) {
            if (ec || !item.is_directory()) continue;
            const fs::path candidate = item.path();
            if (fs::exists(candidate / "assets" / "FFAtmo_particles.pss", ec)) return candidate;
        }
    }
    return plugins / "FFAtmo";
}

bool copyFxTree(const fs::path& source, const fs::path& destination, std::string& error) {
    std::error_code ec;
    if (!fs::exists(source, ec)) {
        error = "Packaged FX source is missing: " + source.string();
        return false;
    }
    fs::create_directories(destination, ec);
    if (ec) {
        error = "Could not create X-Plane FX folder: " + destination.string();
        return false;
    }
    for (const auto& item : fs::recursive_directory_iterator(source, ec)) {
        if (ec) {
            error = "Could not scan packaged FX files.";
            return false;
        }
        const fs::path relative = fs::relative(item.path(), source, ec);
        if (ec) {
            error = "Could not resolve an FX asset path.";
            return false;
        }
        const fs::path target = destination / relative;
        if (item.is_directory()) {
            fs::create_directories(target, ec);
        } else if (item.is_regular_file()) {
            fs::create_directories(target.parent_path(), ec);
            if (!ec) fs::copy_file(item.path(), target, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            error = "Could not copy FX file into X-Plane: " + target.string();
            return false;
        }
    }
    return true;
}

bool syncFxPackageToXPlane() {
    if (gXPlaneRoot.empty()) return false;
    const fs::path sourceRoot = executablePath().parent_path();
    const fs::path targetRoot = findInstalledFxPluginRoot();
    std::error_code ec;
    if (fs::equivalent(sourceRoot, targetRoot, ec) && !ec) {
        gFooter = "Atmospheric FX is running directly from the X-Plane plugin folder.";
        return true;
    }

    std::string error;
    if (!copyFxTree(sourceRoot / "assets", targetRoot / "assets", error)) {
        gFooter = "FX sync failed: " + error + " Close X-Plane and try again.";
        return false;
    }

    const fs::path sourcePlugin = sourceRoot / "64" / "win.xpl";
    if (fs::exists(sourcePlugin, ec)) {
        fs::create_directories(targetRoot / "64", ec);
        if (!ec) fs::copy_file(sourcePlugin, targetRoot / "64" / "win.xpl", fs::copy_options::overwrite_existing, ec);
        if (ec) {
            gFooter = "FX assets synced, but win.xpl is locked. Close X-Plane and reopen Atmospheric FX.";
            return false;
        }
    }

    std::ofstream marker(targetRoot / "FFAtmo.asset-version.txt", std::ios::trunc);
    marker << ffatmo::app::kAppVersion << '\n';
    marker.close();
    gFooter = "Atmospheric FX v" + std::string(ffatmo::app::kAppVersion) + " synced into X-Plane. Restart the simulator to reload particles.";
    return true;
}
'@

    # GitHub's Windows runner gives here-strings CRLF endings. Normalize first,
    # then convert once to the source file's existing line ending style.
    $syncCode = ($syncCode -replace "`r`n", "`n").TrimEnd()
    $syncCode = $syncCode -replace "`n", $newline
    $text = $text.Replace($anchor, $syncCode + $newline + $newline + $anchor)
    Write-Host "Inserted X-Plane asset synchronization helpers."
} else {
    Write-Host "X-Plane asset synchronization helpers are already present."
}

$oldConnect = "void connectRoot() {${newline}    if (gXPlaneRoot.empty()) return;${newline}    std::string message;"
$newConnect = "void connectRoot() {${newline}    if (gXPlaneRoot.empty()) return;${newline}    syncFxPackageToXPlane();${newline}    std::string message;"
if ($text.Contains($oldConnect)) {
    $text = $text.Replace($oldConnect, $newConnect)
    Write-Host "Wired automatic FX synchronization into connectRoot()."
} elseif ($text.Contains($newConnect)) {
    Write-Host "Automatic FX synchronization is already wired into connectRoot()."
} else {
    throw "Could not wire automatic FX synchronization into connectRoot()."
}

if (-not $text.Contains('if (fs::exists(p/"Resources"/"plugins")) { gXPlaneRoot=p; connectRoot(); }')) {
    throw "Could not confirm X-Plane folder connection flow."
}

Set-Content -Path $path -Value $text -Encoding utf8 -NoNewline
Write-Host "Applied automatic deployment of packaged particle assets into the selected X-Plane plugin folder."
