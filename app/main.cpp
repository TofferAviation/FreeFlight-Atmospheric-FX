#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include "Settings.h"
#include "FFAtmoResource.h"
#include "SkinRenderer.h"
#include "TelemetryProvider.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using ffatmo::EffectSettings;
using ffatmo::QualityPreset;

namespace {

constexpr wchar_t kWindowClass[] = L"FFAtmoCompanionWindow";
constexpr int kNavFirst = 100;
constexpr int kBrowse = 180;
constexpr int kEnabled = 200;
constexpr int kPreview = 201;
constexpr int kContrail = 202;
constexpr int kWingSheet = 203;
constexpr int kWingVortex = 204;
constexpr int kPersistence = 205;
constexpr int kAutomaticWeather = 220;
constexpr int kMinimumAltitude = 221;
constexpr int kQuality = 240;
constexpr int kAdaptive = 241;
constexpr int kTargetFps = 242;
constexpr int kOpenPlugin = 260;
constexpr int kOpenLog = 280;
constexpr int kSave = 281;
constexpr int kRefresh = 282;
constexpr int kStatusBase = 500;

const COLORREF kBackground = RGB(7, 17, 30);
const COLORREF kSidebar = RGB(5, 14, 25);
const COLORREF kPanel = RGB(18, 34, 52);
const COLORREF kPanelHi = RGB(23, 48, 73);
const COLORREF kText = RGB(236, 246, 255);
const COLORREF kMuted = RGB(145, 167, 189);
const COLORREF kBlue = RGB(47, 159, 255);
const COLORREF kGold = RGB(217, 175, 96);
const COLORREF kGreen = RGB(76, 211, 154);

std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), count);
    return result;
}

std::string narrow(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0,
                                          nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring executablePath() {
    std::array<wchar_t, 32768> path {};
    GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return path.data();
}

fs::path detectXPlaneRoot() {
    fs::path cursor = fs::path(executablePath()).parent_path();
    for (int i = 0; i < 6 && !cursor.empty(); ++i) {
        if (fs::exists(cursor / "Resources" / "plugins") &&
            fs::exists(cursor / "Output" / "preferences")) return cursor;
        cursor = cursor.parent_path();
    }
    return {};
}

struct AppState {
    HWND window = nullptr;
    HFONT titleFont = nullptr;
    HFONT bodyFont = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH sidebarBrush = nullptr;
    HBRUSH panelBrush = nullptr;
    int page = 0;
    fs::path xplaneRoot;
    fs::path configPath;
    EffectSettings settings {};
    ffatmo::CompanionStatus status {};
    std::unique_ptr<ffatmo::app::TelemetryProvider> telemetry;
    std::unique_ptr<ffatmo::app::SkinRenderer> skin;
    std::vector<HWND> pageControls;
    std::wstring footer = L"Choose the X-Plane 12 folder to begin.";
};

AppState g;

fs::path settingsPath() { return g.xplaneRoot / "Output" / "preferences" / "FFAtmo.ini"; }
fs::path statusPath() { return g.xplaneRoot / "Output" / "preferences" / "FFAtmo.status.ini"; }
fs::path pluginPath() { return g.xplaneRoot / "Resources" / "plugins" / "FFAtmo"; }

void setFont(HWND control, HFONT font = nullptr) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : g.bodyFont), TRUE);
}

HWND addControl(DWORD exStyle, const wchar_t* klass, const wchar_t* text, DWORD style,
                int x, int y, int w, int h, int id = 0, bool pageControl = true) {
    HWND control = CreateWindowExW(exStyle, klass, text, style | WS_CHILD | WS_VISIBLE,
        x, y, w, h, g.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    setFont(control);
    if (pageControl) g.pageControls.push_back(control);
    return control;
}

HWND label(const wchar_t* text, int x, int y, int w, int h, int id = 0,
           DWORD extra = 0) {
    return addControl(0, L"STATIC", text, SS_LEFT | extra, x, y, w, h, id);
}

HWND heading(const wchar_t* title, const wchar_t* subtitle) {
    HWND h = label(title, 280, 30, 600, 38);
    setFont(h, g.titleFont);
    label(subtitle, 280, 70, 760, 24);
    return h;
}

HWND check(const wchar_t* text, int x, int y, int w, int id, bool value) {
    HWND h = addControl(0, L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP,
                        x, y, w, 30, id);
    SendMessageW(h, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    return h;
}

HWND slider(const wchar_t* title, int x, int y, int w, int id, int minimum,
            int maximum, int value) {
    label(title, x, y, w, 22);
    HWND h = addControl(0, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                        x, y + 24, w, 34, id);
    SendMessageW(h, TBM_SETRANGE, TRUE, MAKELONG(minimum, maximum));
    SendMessageW(h, TBM_SETPOS, TRUE, value);
    return h;
}

void clearPage() {
    for (HWND control : g.pageControls) DestroyWindow(control);
    g.pageControls.clear();
}

void saveRootConfig() {
    std::ofstream out(g.configPath, std::ios::trunc);
    if (out) out << "xplane_root=" << narrow(g.xplaneRoot.wstring()) << '\n';
}

void loadRootConfig() {
    std::ifstream in(g.configPath);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("xplane_root=", 0) == 0) {
            const fs::path candidate = fs::u8path(line.substr(12));
            if (fs::exists(candidate / "Resources" / "plugins")) g.xplaneRoot = candidate;
        }
    }
    if (g.xplaneRoot.empty()) g.xplaneRoot = detectXPlaneRoot();
}

void connectPaths() {
    if (g.xplaneRoot.empty()) {
        g.telemetry.reset();
        return;
    }
    std::string message;
    ffatmo::SettingsStore::load(settingsPath().string(), g.settings, &message);
    g.telemetry = std::make_unique<ffatmo::app::FileBridgeTelemetry>(statusPath().string());
    saveRootConfig();
}

void saveLiveSettings() {
    if (g.xplaneRoot.empty()) {
        g.footer = L"Select the X-Plane 12 root before changing live settings.";
        InvalidateRect(g.window, nullptr, FALSE);
        return;
    }
    std::string error;
    if (ffatmo::SettingsStore::save(settingsPath().string(), g.settings, &error)) {
        g.footer = L"Live settings written — the plugin will apply them within 0.25 seconds.";
    } else {
        g.footer = L"Could not write FFAtmo.ini: " + widen(error);
    }
    InvalidateRect(g.window, nullptr, FALSE);
}

void showOverview() {
    heading(L"Atmospheric Operations", L"Live aircraft wake, atmospheric state and effect channels");

    label(L"FORMATION", 280, 120, 150, 20);
    label(L"0%", 280, 143, 150, 42, kStatusBase + 2);
    label(L"PERSISTENCE", 450, 120, 150, 20);
    label(L"0 sec / 0 km", 450, 143, 180, 42, kStatusBase + 3);
    label(L"WING VAPOUR", 650, 120, 150, 20);
    label(L"0%", 650, 143, 150, 42, kStatusBase + 5);
    label(L"PARTICLE LOAD", 820, 120, 150, 20);
    label(L"0%", 820, 143, 150, 42, kStatusBase + 6);

    label(L"LIVE WAKE VISUALIZATION", 280, 215, 300, 22);
    label(L"EFFECT CHANNELS", 1010, 120, 250, 22);
    check(L"Engine contrails", 1010, 155, 280, kEnabled, g.settings.enabled);
    check(L"Wingtip vortices", 1010, 195, 280, kWingVortex,
          g.settings.wingVortexIntensity > 0.01f);
    check(L"Over-wing vapour", 1010, 235, 280, kWingSheet,
          g.settings.wingCondensationIntensity > 0.01f);

    label(L"ENVIRONMENT", 1010, 300, 250, 22);
    label(L"Flight level", 1010, 335, 130, 24);
    label(L"GROUND", 1160, 335, 130, 24, kStatusBase + 8);
    label(L"Outside air temperature", 1010, 368, 180, 24);
    label(L"15 °C", 1200, 368, 90, 24, kStatusBase + 9);
    label(L"RH over ice", 1010, 401, 150, 24);
    label(L"0%", 1200, 401, 90, 24, kStatusBase + 10);

    label(L"QUALITY PRESET", 1010, 470, 250, 22);
    HWND quality = addControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                              1010, 500, 280, 150, kQuality);
    for (const wchar_t* value : {L"Low", L"Medium", L"High", L"Ultra"})
        SendMessageW(quality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    SendMessageW(quality, CB_SETCURSEL, static_cast<int>(g.settings.quality), 0);

    label(L"WAKE DEVELOPMENT", 280, 640, 260, 22);
    label(L"Formation        Vortex capture        Wake curtain        Contrail cirrus",
          310, 740, 650, 24);
    label(L"Waiting for X-Plane", 1010, 585, 280, 28, kStatusBase);
    label(L"No aircraft detected", 1010, 620, 280, 28, kStatusBase + 1);
}

void showEffects() {
    heading(L"Effects Control", L"Changes are written live to the running simulator plugin");
    check(L"Enable FFAtmospherics", 280, 126, 320, kEnabled, g.settings.enabled);
    check(L"Preview Mode (automatically enables effects)", 620, 126, 390,
          kPreview, g.settings.previewMode);
    slider(L"Contrail intensity", 280, 200, 340, kContrail, 0, 200,
           static_cast<int>(g.settings.contrailIntensity * 100.0f));
    slider(L"Over-wing condensation", 660, 200, 340, kWingSheet, 0, 200,
           static_cast<int>(g.settings.wingCondensationIntensity * 100.0f));
    slider(L"Wingtip vortex intensity", 280, 310, 340, kWingVortex, 0, 200,
           static_cast<int>(g.settings.wingVortexIntensity * 100.0f));
    slider(L"Persistence scale", 660, 310, 340, kPersistence, 25, 200,
           static_cast<int>(g.settings.persistenceScale * 100.0f));
    label(L"Preview is a calibration tool. Normal operation uses live weather, aircraft load,\n"
          L"speed and angle of attack to decide which layers should appear.",
          280, 450, 720, 70);
}

void showWeather() {
    heading(L"Weather & Realism", L"FFAtmospherics reads simulator weather and never changes it");
    check(L"Automatic live-weather evaluation", 280, 126, 380, kAutomaticWeather,
          g.settings.automaticWeather);
    slider(L"Minimum contrail altitude", 280, 210, 500, kMinimumAltitude, 0, 500,
           static_cast<int>(g.settings.minimumContrailAltitudeFt / 100.0f));
    label(L"Formation requires cold exhaust mixing and sufficient moisture. Persistent trails\n"
          L"require ice-supersaturated air; wing condensation follows local lift and humidity.",
          280, 330, 740, 70);
    label(L"The companion displays decisions from FFAtmo.status.ini. XUIPC may later provide\n"
          L"additional aircraft telemetry, but is not required for weather physics.",
          280, 440, 740, 70);
}

void showPerformance() {
    heading(L"Quality & Performance", L"Balance trail detail, persistence and frame-time protection");
    label(L"Quality preset", 280, 130, 180, 24);
    HWND combo = addControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                            280, 160, 300, 160, kQuality);
    for (const wchar_t* value : {L"Low", L"Medium", L"High", L"Ultra"})
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    SendMessageW(combo, CB_SETCURSEL, static_cast<int>(g.settings.quality), 0);
    check(L"Adaptive quality protection", 620, 160, 340, kAdaptive,
          g.settings.adaptiveQuality);
    slider(L"Target frame rate", 280, 260, 500, kTargetFps, 20, 90,
           static_cast<int>(g.settings.adaptiveTargetFps));
    label(L"Adaptive quality scales newly emitted distant particles before shortening the trail.\n"
          L"Existing wake sections continue fading naturally.", 280, 390, 740, 70);
}

void showProfile() {
    heading(L"Aircraft Profile", L"Detection, installation and emitter-calibration status");
    label(L"Detected aircraft", 280, 130, 240, 24);
    label(L"Waiting for simulator", 280, 165, 720, 34, kStatusBase + 1);
    label(L"Particle object", 280, 230, 240, 24);
    label(L"Not connected", 280, 265, 400, 34, kStatusBase);
    addControl(0, L"BUTTON", L"Open installed FFAtmo folder", BS_OWNERDRAW | WS_TABSTOP,
               280, 350, 300, 42, kOpenPlugin);
    label(L"Lineage 1000 is the first supported profile. The standalone layout keeps all\n"
          L"aircraft-specific anchors in FFAtmo\\profiles without modifying the aircraft.",
          280, 440, 740, 70);
}

void showAdvanced() {
    heading(L"Advanced & Test", L"Diagnostics, log access and product maintenance");
    addControl(0, L"BUTTON", L"Save settings now", BS_OWNERDRAW | WS_TABSTOP,
               280, 130, 220, 42, kSave);
    addControl(0, L"BUTTON", L"Refresh telemetry", BS_OWNERDRAW | WS_TABSTOP,
               520, 130, 220, 42, kRefresh);
    addControl(0, L"BUTTON", L"Open X-Plane Log.txt", BS_OWNERDRAW | WS_TABSTOP,
               760, 130, 240, 42, kOpenLog);
    label(L"LIVE BRIDGE", 280, 230, 180, 24);
    label(L"Settings: Output\\preferences\\FFAtmo.ini\n"
          L"Status: Output\\preferences\\FFAtmo.status.ini\n"
          L"Polling: settings 250 ms / telemetry 500 ms",
          280, 265, 720, 90);
    label(L"UPDATE CHANNEL", 280, 390, 180, 24);
    label(L"Development build — updater disabled until the stable V1 GitHub baseline.\n"
          L"The companion owns the future update workflow so win.xpl is never replaced while loaded.",
          280, 425, 740, 70);
}

void showPage(int page) {
    clearPage();
    g.page = std::clamp(page, 0, 5);
    if (g.skin && g.skin->available()) {
        InvalidateRect(g.window, nullptr, TRUE);
        return;
    }
    switch (g.page) {
        case 0: showOverview(); break;
        case 1: showEffects(); break;
        case 2: showWeather(); break;
        case 3: showPerformance(); break;
        case 4: showProfile(); break;
        case 5: showAdvanced(); break;
    }
    InvalidateRect(g.window, nullptr, TRUE);
}

void readCurrentPageSettings() {
    auto checked = [](int id) {
        HWND h = GetDlgItem(g.window, id);
        return h && SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
    };
    auto position = [](int id, int fallback) {
        HWND h = GetDlgItem(g.window, id);
        return h ? static_cast<int>(SendMessageW(h, TBM_GETPOS, 0, 0)) : fallback;
    };
    if (g.page == 0) {
        g.settings.enabled = checked(kEnabled);
        g.settings.wingVortexIntensity = checked(kWingVortex) ? 1.0f : 0.0f;
        g.settings.wingCondensationIntensity = checked(kWingSheet) ? 1.0f : 0.0f;
        HWND combo = GetDlgItem(g.window, kQuality);
        const int quality = combo ? static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)) : 2;
        g.settings.quality = static_cast<QualityPreset>(std::clamp(quality, 0, 3));
    } else if (g.page == 1) {
        g.settings.enabled = checked(kEnabled);
        g.settings.previewMode = checked(kPreview);
        if (g.settings.previewMode) {
            g.settings.enabled = true;
            SendMessageW(GetDlgItem(g.window, kEnabled), BM_SETCHECK, BST_CHECKED, 0);
        }
        g.settings.contrailIntensity = position(kContrail, 100) / 100.0f;
        g.settings.wingCondensationIntensity = position(kWingSheet, 100) / 100.0f;
        g.settings.wingVortexIntensity = position(kWingVortex, 100) / 100.0f;
        g.settings.persistenceScale = position(kPersistence, 100) / 100.0f;
    } else if (g.page == 2) {
        g.settings.automaticWeather = checked(kAutomaticWeather);
        g.settings.minimumContrailAltitudeFt = position(kMinimumAltitude, 180) * 100.0f;
    } else if (g.page == 3) {
        HWND combo = GetDlgItem(g.window, kQuality);
        const int quality = combo ? static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)) : 2;
        g.settings.quality = static_cast<QualityPreset>(std::clamp(quality, 0, 3));
        g.settings.adaptiveQuality = checked(kAdaptive);
        g.settings.adaptiveTargetFps = static_cast<float>(position(kTargetFps, 30));
    }
}

void refreshTelemetry() {
    if (!g.telemetry) return;
    ffatmo::CompanionStatus next;
    std::string error;
    if (g.telemetry->read(next, &error)) {
        g.status = next;
        g.footer = next.pluginRunning ? L"Connected to FFAtmospherics in X-Plane."
                                      : L"Status file found; plugin is not currently running.";
    } else {
        g.footer = L"Waiting for FFAtmospherics telemetry.";
    }
    auto set = [](int id, const std::wstring& value) {
        if (HWND control = GetDlgItem(g.window, id)) SetWindowTextW(control, value.c_str());
    };
    set(kStatusBase, g.status.pluginRunning
        ? (g.status.particleObjectLoaded ? L"Connected · particles attached" : L"Connected · particles waiting")
        : L"Waiting for X-Plane");
    set(kStatusBase + 1, g.status.profileName.empty() ? L"No supported aircraft detected"
                                                     : widen(g.status.profileName));
    set(kStatusBase + 2, std::to_wstring(static_cast<int>(g.status.state.formationProbability * 100.0f)) + L"%");
    std::wostringstream trail;
    trail << static_cast<int>(g.status.state.contrailPersistenceSeconds) << L" sec / "
          << static_cast<int>(g.status.state.estimatedTrailLengthKm) << L" km";
    set(kStatusBase + 3, trail.str());
    set(kStatusBase + 4, std::to_wstring(static_cast<int>(g.status.state.primaryWakeRatio * 100.0f)) + L"%");
    set(kStatusBase + 5, std::to_wstring(static_cast<int>(g.status.state.secondaryCurtainRatio * 100.0f)) + L"%");
    set(kStatusBase + 6, std::to_wstring(static_cast<int>(g.status.state.contrailCirrusRatio * 100.0f)) + L"%");
    set(kStatusBase + 8, g.status.input.altitudeFt > 1000.0f
        ? L"FL" + std::to_wstring(static_cast<int>(g.status.input.altitudeFt / 100.0f))
        : L"GROUND");
    set(kStatusBase + 9, std::to_wstring(static_cast<int>(g.status.input.ambientTemperatureC)) + L" °C");
    set(kStatusBase + 10, std::to_wstring(static_cast<int>(g.status.state.relativeHumidityIcePercent)) + L"%");
    InvalidateRect(g.window, nullptr, FALSE);
}

void chooseXPlaneFolder() {
    BROWSEINFOW info {};
    info.hwndOwner = g.window;
    info.lpszTitle = L"Select your X-Plane 12 folder";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    if (PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info)) {
        std::array<wchar_t, MAX_PATH> path {};
        if (SHGetPathFromIDListW(item, path.data())) {
            fs::path selected(path.data());
            if (fs::exists(selected / "Resources" / "plugins") &&
                fs::exists(selected / "Output" / "preferences")) {
                g.xplaneRoot = selected;
                connectPaths();
                g.footer = L"X-Plane folder connected.";
                showPage(g.page);
            } else {
                MessageBoxW(g.window, L"That folder does not look like an X-Plane 12 root.",
                            L"FFAtmospherics", MB_OK | MB_ICONWARNING);
            }
        }
        CoTaskMemFree(item);
    }
}

void openPath(const fs::path& path) {
    ShellExecuteW(g.window, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void copyText(const std::wstring& value) {
    if (!OpenClipboard(g.window)) return;
    EmptyClipboard();
    const SIZE_T bytes = (value.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* destination = GlobalLock(memory);
        std::memcpy(destination, value.c_str(), bytes);
        GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
    }
    CloseClipboard();
    g.footer = L"Dataref copied to the clipboard.";
    InvalidateRect(g.window, nullptr, FALSE);
}

void resetEffectSettings() {
    const bool automaticWeather = g.settings.automaticWeather;
    const QualityPreset quality = g.settings.quality;
    const bool adaptiveQuality = g.settings.adaptiveQuality;
    const float targetFps = g.settings.adaptiveTargetFps;
    g.settings = EffectSettings {};
    g.settings.automaticWeather = automaticWeather;
    g.settings.quality = quality;
    g.settings.adaptiveQuality = adaptiveQuality;
    g.settings.adaptiveTargetFps = targetFps;
    g.footer = L"Effect shaping reset to defaults.";
    saveLiveSettings();
}

void exportDiagnostics() {
    if (g.xplaneRoot.empty()) {
        g.footer = L"Choose the X-Plane folder before exporting diagnostics.";
        InvalidateRect(g.window, nullptr, FALSE);
        return;
    }
    const fs::path destination = fs::path(executablePath()).parent_path() /
                                 "FFAtmo-diagnostics.txt";
    std::ofstream out(destination, std::ios::trunc);
    if (!out) {
        g.footer = L"Could not create FFAtmo-diagnostics.txt.";
        InvalidateRect(g.window, nullptr, FALSE);
        return;
    }
    out << "FFAtmospherics companion diagnostics\n"
        << "plugin_running=" << (g.status.pluginRunning ? 1 : 0) << '\n'
        << "profile=" << g.status.profileName << '\n'
        << "particle_object_loaded=" << (g.status.particleObjectLoaded ? 1 : 0) << '\n'
        << "formation_probability=" << g.status.state.formationProbability << '\n'
        << "persistent_seconds=" << g.status.state.contrailPersistenceSeconds << '\n'
        << "trail_length_km=" << g.status.state.estimatedTrailLengthKm << '\n'
        << "particle_budget=" << g.status.state.particleBudgetRatio << '\n'
        << "xplane_root=" << narrow(g.xplaneRoot.wstring()) << '\n';
    out.close();
    g.footer = L"Diagnostics exported beside FFAtmoCompanion.exe.";
    openPath(destination);
    InvalidateRect(g.window, nullptr, FALSE);
}

void drawOwnerButton(const DRAWITEMSTRUCT* item) {
    const bool nav = item->CtlID >= kNavFirst && item->CtlID < kNavFirst + 6;
    const bool active = nav && item->CtlID - kNavFirst == g.page;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    HBRUSH brush = CreateSolidBrush(active ? kPanelHi : (pressed ? kPanelHi : kPanel));
    FillRect(item->hDC, &item->rcItem, brush);
    DeleteObject(brush);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, active ? kText : kMuted);
    SelectObject(item->hDC, g.bodyFont);
    std::array<wchar_t, 256> text {};
    GetWindowTextW(item->hwndItem, text.data(), static_cast<int>(text.size()));
    RECT rect = item->rcItem;
    rect.left += nav ? 18 : 0;
    DrawTextW(item->hDC, text.data(), -1, &rect,
              (nav ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE);
    if (active) {
        RECT accent = item->rcItem;
        accent.right = accent.left + 4;
        HBRUSH blue = CreateSolidBrush(kBlue);
        FillRect(item->hDC, &accent, blue);
        DeleteObject(blue);
    }
}

void drawOverviewCanvas(HDC dc) {
    if (g.page != 0) return;

    HPEN border = CreatePen(PS_SOLID, 1, RGB(34, 67, 96));
    HPEN blue = CreatePen(PS_SOLID, 2, kBlue);
    HPEN cyan = CreatePen(PS_SOLID, 2, RGB(50, 215, 230));
    HPEN gold = CreatePen(PS_SOLID, 2, kGold);
    HBRUSH card = CreateSolidBrush(RGB(10, 27, 44));
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBrush = SelectObject(dc, card);

    const RECT cards[] {{270,108,430,195}, {440,108,630,195},
                        {640,108,800,195}, {810,108,980,195},
                        {270,205,980,620}, {995,108,1315,280},
                        {995,290,1315,450}, {995,460,1315,560},
                        {270,630,980,790}};
    for (const RECT& rect : cards) RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 14, 14);

    // Procedural top-down aircraft schematic. This is intentionally rendered
    // from geometry rather than loaded from the UI reference image.
    SelectObject(dc, blue);
    POINT aircraft[] {{625,270},{646,345},{760,410},{646,390},{646,515},
                      {690,555},{625,535},{560,555},{604,515},{604,390},
                      {490,410},{604,345},{625,270}};
    Polyline(dc, aircraft, static_cast<int>(std::size(aircraft)));
    MoveToEx(dc, 625, 270, nullptr); LineTo(dc, 625, 535);

    // Live twin vortex paths. Their spread and vertical placement respond to
    // the wake state, so this is a real telemetry visualization.
    const int spread = 55 + static_cast<int>(g.status.state.secondaryCurtainRatio * 45.0f);
    const int drop = 45 + static_cast<int>(g.status.state.primaryWakeRatio * 55.0f);
    SelectObject(dc, cyan);
    POINT leftWake[] {{560,410},{525,420},{505,450},{520,475-drop/4},
                      {550-spread,500},{500-spread,515},{475-spread,520}};
    POINT rightWake[] {{690,410},{725,420},{745,450},{730,475-drop/4},
                       {700+spread,500},{750+spread,515},{775+spread,520}};
    PolyBezier(dc, leftWake, 7);
    PolyBezier(dc, rightWake, 7);

    const int lineLeft = 310;
    const int lineRight = 940;
    const int lineY = 705;
    SelectObject(dc, border);
    MoveToEx(dc, lineLeft, lineY, nullptr); LineTo(dc, lineRight, lineY);
    const float phases[] {g.status.state.formationProbability, g.status.state.primaryWakeRatio,
                          g.status.state.secondaryCurtainRatio, g.status.state.contrailCirrusRatio};
    HPEN phasePens[] {blue, cyan, gold, gold};
    for (int i = 0; i < 4; ++i) {
        const int x = lineLeft + i * 205;
        SelectObject(dc, phasePens[i]);
        Ellipse(dc, x - 8, lineY - 8, x + 8, lineY + 8);
        const int height = static_cast<int>(std::clamp(phases[i], 0.0f, 1.0f) * 55.0f);
        MoveToEx(dc, x, lineY - 14, nullptr); LineTo(dc, x, lineY - 14 - height);
    }

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(card);
    DeleteObject(border);
    DeleteObject(blue);
    DeleteObject(cyan);
    DeleteObject(gold);
}

bool inside(POINT point, int left, int top, int right, int bottom) {
    return point.x >= left && point.x <= right && point.y >= top && point.y <= bottom;
}

float horizontalRatio(POINT point, int left = 293, int right = 895) {
    return std::clamp((point.x - left) / static_cast<float>(right - left), 0.0f, 1.0f);
}

void handleSkinClick(POINT clientPoint) {
    RECT client;
    GetClientRect(g.window, &client);
    const POINT point = g.skin->toReference(client, clientPoint);

    for (int page = 0; page < 6; ++page) {
        const int top = 159 + page * 54;
        if (inside(point, 20, top, 223, top + 48)) {
            showPage(page);
            return;
        }
    }
    if (inside(point, 20, 910, 223, 980)) {
        chooseXPlaneFolder();
        return;
    }

    bool changed = false;
    if (g.page == 0) {
        if (point.y >= 310 && point.y <= 370 && point.x >= 1125 && point.x <= 1560) {
            const int quality = std::clamp((static_cast<int>(point.x) - 1125) / 108, 0, 3);
            g.settings.quality = static_cast<QualityPreset>(quality);
            changed = true;
        } else if (inside(point, 1475, 630, 1555, 680)) {
            g.settings.enabled = !g.settings.enabled;
            changed = true;
        } else if (inside(point, 1475, 685, 1555, 735)) {
            g.settings.wingVortexIntensity = g.settings.wingVortexIntensity > 0.01f ? 0.0f : 1.0f;
            changed = true;
        } else if (inside(point, 1475, 738, 1555, 790)) {
            g.settings.wingCondensationIntensity = g.settings.wingCondensationIntensity > 0.01f ? 0.0f : 1.0f;
            changed = true;
        }
    } else if (g.page == 1) {
        if (inside(point, 835, 100, 910, 145)) {
            g.settings.enabled = !g.settings.enabled;
            changed = true;
        } else if (inside(point, 285, 194, 905, 224)) {
            g.settings.contrailIntensity = horizontalRatio(point) * 2.0f;
            changed = true;
        } else if (inside(point, 285, 306, 905, 338)) {
            g.settings.persistenceScale = std::max(0.25f, horizontalRatio(point) * 2.0f);
            changed = true;
        } else if (inside(point, 820, 550, 910, 604)) {
            g.settings.wingVortexIntensity = g.settings.wingVortexIntensity > 0.01f ? 0.0f : 1.0f;
            changed = true;
        } else if (inside(point, 820, 604, 910, 658)) {
            g.settings.wingCondensationIntensity = g.settings.wingCondensationIntensity > 0.01f ? 0.0f : 1.0f;
            changed = true;
        } else if (inside(point, 948, 645, 1080, 700)) {
            g.settings.previewMode = !g.settings.previewMode;
            if (g.settings.previewMode) g.settings.enabled = true;
            changed = true;
        } else if (inside(point, 1080, 645, 1180, 700)) {
            resetEffectSettings();
            return;
        }
    } else if (g.page == 2) {
        if (inside(point, 740, 525, 815, 585)) {
            g.settings.automaticWeather = !g.settings.automaticWeather;
            changed = true;
        }
    } else if (g.page == 3) {
        if (point.y >= 95 && point.y <= 240 && point.x >= 270) {
            const int quality = std::clamp((static_cast<int>(point.x) - 270) / 327, 0, 3);
            g.settings.quality = static_cast<QualityPreset>(quality);
            changed = true;
        } else if (inside(point, 830, 395, 915, 450)) {
            g.settings.adaptiveQuality = !g.settings.adaptiveQuality;
            changed = true;
        } else if (inside(point, 285, 488, 905, 522)) {
            g.settings.adaptiveTargetFps = 20.0f + horizontalRatio(point) * 70.0f;
            changed = true;
        }
    } else if (g.page == 4) {
        if (inside(point, 1038, 378, 1155, 430)) {
            openPath(pluginPath() / "profiles");
        } else if (inside(point, 1155, 378, 1275, 430)) {
            MessageBoxW(g.window,
                        L"Profile duplication will be enabled when the calibration editor is introduced.\n\n"
                        L"The current Lineage profile remains protected from accidental edits.",
                        L"FFAtmospherics", MB_OK | MB_ICONINFORMATION);
        } else if (inside(point, 1038, 765, 1210, 820)) {
            g.settings.previewMode = true;
            g.settings.enabled = true;
            saveLiveSettings();
        }
    } else if (g.page == 5) {
        if (inside(point, 835, 100, 910, 145)) {
            g.settings.previewMode = !g.settings.previewMode;
            if (g.settings.previewMode) g.settings.enabled = true;
            changed = true;
        } else if (inside(point, 290, 235, 426, 285)) {
            g.settings.previewMode = true;
            g.settings.enabled = true;
            g.settings.contrailIntensity = std::max(1.0f, g.settings.contrailIntensity);
            changed = true;
        } else if (inside(point, 427, 235, 550, 285)) {
            g.settings.previewMode = true;
            g.settings.enabled = true;
            g.settings.wingCondensationIntensity = std::max(1.0f, g.settings.wingCondensationIntensity);
            g.settings.wingVortexIntensity = std::max(1.0f, g.settings.wingVortexIntensity);
            changed = true;
        } else if (inside(point, 551, 235, 630, 285)) {
            g.settings.previewMode = false;
            g.settings.enabled = false;
            changed = true;
        } else if (inside(point, 285, 408, 905, 440)) {
            g.settings.contrailIntensity = horizontalRatio(point) * 2.0f;
            changed = true;
        } else if (inside(point, 285, 465, 905, 496)) {
            g.settings.wingCondensationIntensity = horizontalRatio(point) * 2.0f;
            changed = true;
        } else if (inside(point, 285, 520, 905, 552)) {
            g.settings.persistenceScale = std::max(0.25f, horizontalRatio(point) * 2.0f);
            changed = true;
        } else if (inside(point, 945, 300, 1045, 355)) {
            refreshTelemetry();
            g.footer = L"Effects and telemetry reloaded.";
        } else if (inside(point, 1045, 300, 1135, 355)) {
            exportDiagnostics();
        } else if (inside(point, 1135, 300, 1245, 355)) {
            openPath(g.xplaneRoot / "Log.txt");
        } else if (inside(point, 1490, 455, 1555, 500)) {
            copyText(L"ffatmo/contrail/intensity");
        } else if (inside(point, 1490, 500, 1555, 535)) {
            copyText(L"ffatmo/contrail/persistence");
        } else if (inside(point, 1490, 535, 1555, 570)) {
            copyText(L"ffatmo/wing/vapour_ratio");
        } else if (inside(point, 1490, 570, 1555, 610)) {
            copyText(L"ffatmo/system/particle_load");
        }
    }
    if (changed) {
        saveLiveSettings();
        InvalidateRect(g.window, nullptr, FALSE);
    }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            g.window = window;
            if (g.skin && g.skin->available()) {
                SetTimer(window, 1, 500, nullptr);
                showPage(0);
                return 0;
            }
            HWND logo = addControl(0, L"STATIC", L"", SS_ICON | SS_CENTERIMAGE,
                                   18, 17, 44, 56, 0, false);
            SendMessageW(logo, STM_SETICON,
                         reinterpret_cast<WPARAM>(LoadIconW(GetModuleHandleW(nullptr),
                                                            MAKEINTRESOURCEW(IDI_APP_ICON))), 0);
            addControl(0, L"STATIC", L"FREEFLIGHT", SS_LEFT, 72, 24, 150, 24, 0, false);
            HWND brand = addControl(0, L"STATIC", L"ATMOSPHERIC FX", SS_LEFT,
                                    72, 49, 165, 24, 0, false);
            setFont(brand, g.titleFont);
            const wchar_t* pages[] = {L"Live Overview", L"Effects Control", L"Weather & Realism",
                                      L"Quality & Performance", L"Aircraft Profile", L"Advanced & Test"};
            for (int i = 0; i < 6; ++i) {
                addControl(0, L"BUTTON", pages[i], BS_OWNERDRAW | WS_TABSTOP,
                           18, 115 + i * 54, 220, 44, kNavFirst + i, false);
            }
            addControl(0, L"BUTTON", L"Choose X-Plane folder", BS_OWNERDRAW | WS_TABSTOP,
                       18, 500, 220, 42, kBrowse, false);
            SetTimer(window, 1, 500, nullptr);
            showPage(0);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id >= kNavFirst && id < kNavFirst + 6) {
                showPage(id - kNavFirst);
                return 0;
            }
            if (id == kBrowse) { chooseXPlaneFolder(); return 0; }
            if (id == kOpenPlugin) { openPath(pluginPath()); return 0; }
            if (id == kOpenLog) { openPath(g.xplaneRoot / "Log.txt"); return 0; }
            if (id == kRefresh) { refreshTelemetry(); return 0; }
            if (id == kSave) { readCurrentPageSettings(); saveLiveSettings(); return 0; }
            if (id == kEnabled || id == kPreview || id == kWingVortex || id == kWingSheet ||
                id == kAutomaticWeather ||
                id == kAdaptive || (id == kQuality && HIWORD(wParam) == CBN_SELCHANGE)) {
                readCurrentPageSettings();
                saveLiveSettings();
                return 0;
            }
            break;
        }
        case WM_HSCROLL:
            readCurrentPageSettings();
            saveLiveSettings();
            return 0;
        case WM_LBUTTONUP:
            if (g.skin && g.skin->available()) {
                handleSkinClick(POINT {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                return 0;
            }
            break;
        case WM_SIZE:
            if (g.skin && g.skin->available()) InvalidateRect(window, nullptr, TRUE);
            return 0;
        case WM_TIMER:
            refreshTelemetry();
            return 0;
        case WM_DRAWITEM:
            drawOwnerButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, kText);
            SetBkColor(dc, kBackground);
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(g.backgroundBrush);
        }
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, kText);
            SetBkColor(dc, kBackground);
            return reinterpret_cast<LRESULT>(g.backgroundBrush);
        }
        case WM_ERASEBKGND: {
            RECT rect;
            GetClientRect(window, &rect);
            if (g.skin && g.skin->available()) {
                // WM_PAINT replaces the entire client area from a completed
                // back-buffer. Erasing first produces a visible dark flash.
                return 1;
            }
            FillRect(reinterpret_cast<HDC>(wParam), &rect, g.backgroundBrush);
            RECT side = rect;
            side.right = 255;
            FillRect(reinterpret_cast<HDC>(wParam), &side, g.sidebarBrush);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            RECT client;
            GetClientRect(window, &client);
            if (g.skin && g.skin->available()) {
                // Compose the complete skinned frame off-screen. Drawing the large
                // page bitmap directly to the window exposed intermediate pixels
                // whenever a slider caused a live settings repaint.
                const int width = std::max(1L, client.right - client.left);
                const int height = std::max(1L, client.bottom - client.top);
                HDC memoryDc = CreateCompatibleDC(dc);
                HBITMAP frame = CreateCompatibleBitmap(dc, width, height);
                HGDIOBJ previous = SelectObject(memoryDc, frame);
                FillRect(memoryDc, &client, g.backgroundBrush);
                g.skin->draw(memoryDc, client, g.page, g.settings, g.status);
                BitBlt(dc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);
                SelectObject(memoryDc, previous);
                DeleteObject(frame);
                DeleteDC(memoryDc);
                EndPaint(window, &paint);
                return 0;
            }
            RECT accent {255, 0, client.right, 4};
            HBRUSH blue = CreateSolidBrush(kBlue);
            FillRect(dc, &accent, blue);
            DeleteObject(blue);
            drawOverviewCanvas(dc);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kMuted);
            SelectObject(dc, g.bodyFont);
            RECT footer {280, client.bottom - 45, client.right - 24, client.bottom - 15};
            DrawTextW(dc, g.footer.c_str(), -1, &footer, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (!g.xplaneRoot.empty()) {
                std::wstring root = L"X-Plane: " + g.xplaneRoot.wstring();
                RECT rootRect {18, 565, 238, 640};
                DrawTextW(dc, root.c_str(), -1, &rootRect, DT_LEFT | DT_WORDBREAK);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(window, 1);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls {sizeof(controls), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    g.backgroundBrush = CreateSolidBrush(kBackground);
    g.sidebarBrush = CreateSolidBrush(kSidebar);
    g.panelBrush = CreateSolidBrush(kPanel);
    g.bodyFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g.titleFont = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g.configPath = fs::path(executablePath()).parent_path() / "FFAtmoCompanion.ini";
    // The companion now uses real Win32 controls for every page. The approved
    // page compositions remain design references only; using them as a runtime
    // bitmap caused fake controls, fragile hit maps and expensive repaints.
    g.skin.reset();
    loadRootConfig();
    connectPaths();

    WNDCLASSEXW windowClass {sizeof(windowClass)};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    HICON appIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    windowClass.hIcon = appIcon ? appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hbrBackground = g.backgroundBrush;
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);

    HWND window = CreateWindowExW(0, kWindowClass, L"FreeFlight Atmospheric FX",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        1400, 900, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DeleteObject(g.bodyFont);
    DeleteObject(g.titleFont);
    DeleteObject(g.backgroundBrush);
    DeleteObject(g.sidebarBrush);
    DeleteObject(g.panelBrush);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
