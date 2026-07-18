#include "SkinRenderer.h"
#include "FFAtmoResource.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>

namespace ffatmo::app {
namespace {

constexpr float kWidth = 1600.0f;
constexpr float kHeight = 1000.0f;
const Gdiplus::Color kCard(255, 16, 34, 52);
const Gdiplus::Color kDeep(255, 7, 17, 30);
const Gdiplus::Color kText(255, 238, 246, 255);
const Gdiplus::Color kMuted(255, 145, 167, 189);
const Gdiplus::Color kBlue(255, 47, 159, 255);
const Gdiplus::Color kGreen(255, 76, 211, 154);

Gdiplus::RectF mapRect(const SkinRenderer::Layout& layout,
                       float x, float y, float width, float height) {
    return {layout.destination.X + x * layout.scale,
            layout.destination.Y + y * layout.scale,
            width * layout.scale, height * layout.scale};
}

void cover(Gdiplus::Graphics& graphics, const SkinRenderer::Layout& layout,
           float x, float y, float width, float height, const Gdiplus::Color& color = kCard) {
    Gdiplus::SolidBrush brush(color);
    graphics.FillRectangle(&brush, mapRect(layout, x, y, width, height));
}

void text(Gdiplus::Graphics& graphics, const SkinRenderer::Layout& layout,
          const std::wstring& value, float x, float y, float width, float height,
          float size, const Gdiplus::Color& color = kText,
          Gdiplus::FontStyle style = Gdiplus::FontStyleRegular) {
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, size * layout.scale, style, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format;
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    graphics.DrawString(value.c_str(), -1, &font, mapRect(layout, x, y, width, height),
                        &format, &brush);
}

void slider(Gdiplus::Graphics& graphics, const SkinRenderer::Layout& layout,
            float y, float ratio) {
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    const auto base = mapRect(layout, 293.0f, y, 602.0f, 4.0f);
    Gdiplus::Pen dark(Gdiplus::Color(255, 31, 61, 88), 4.0f * layout.scale);
    Gdiplus::Pen blue(kBlue, 4.0f * layout.scale);
    graphics.DrawLine(&dark, base.X, base.Y, base.X + base.Width, base.Y);
    graphics.DrawLine(&blue, base.X, base.Y, base.X + base.Width * ratio, base.Y);
    Gdiplus::SolidBrush dot(Gdiplus::Color(255, 255, 255, 255));
    const float radius = 6.0f * layout.scale;
    graphics.FillEllipse(&dot, base.X + base.Width * ratio - radius,
                         base.Y - radius, radius * 2.0f, radius * 2.0f);
}

void toggle(Gdiplus::Graphics& graphics, const SkinRenderer::Layout& layout,
            float x, float y, bool enabled) {
    const auto rect = mapRect(layout, x, y, 38.0f, 22.0f);
    Gdiplus::SolidBrush track(enabled ? Gdiplus::Color(255, 30, 111, 171)
                                      : Gdiplus::Color(255, 45, 70, 92));
    graphics.FillEllipse(&track, rect);
    Gdiplus::SolidBrush knob(enabled ? Gdiplus::Color(255, 255, 255, 255)
                                      : Gdiplus::Color(255, 130, 151, 170));
    const float d = 16.0f * layout.scale;
    const float left = enabled ? rect.X + rect.Width - d - 3.0f * layout.scale
                               : rect.X + 3.0f * layout.scale;
    graphics.FillEllipse(&knob, left, rect.Y + 3.0f * layout.scale, d, d);
}

std::wstring integer(float value) {
    return std::to_wstring(static_cast<int>(std::round(value)));
}

}  // namespace

SkinRenderer::SkinRenderer() {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&gdiplusToken_, &input, nullptr);
}

SkinRenderer::~SkinRenderer() {
    pages_ = {};
    for (IStream* stream : resourceStreams_) {
        if (stream) stream->Release();
    }
    resourceStreams_ = {};
    if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
}

bool SkinRenderer::load(const std::filesystem::path& directory) {
    const std::array<const wchar_t*, 7> names {
        L"overview.png", L"effects.png", L"weather.png", L"performance.png",
        L"profile.png", L"advanced.png", L"brand-logo.png"
    };
    const std::array<int, 7> resourceIds {
        IDR_UI_OVERVIEW, IDR_UI_EFFECTS, IDR_UI_WEATHER, IDR_UI_PERFORMANCE,
        IDR_UI_PROFILE, IDR_UI_ADVANCED, IDR_BRAND_LOGO
    };
    available_ = true;
    for (size_t i = 0; i < names.size(); ++i) {
        HRSRC resource = FindResourceW(GetModuleHandleW(nullptr),
                                       MAKEINTRESOURCEW(resourceIds[i]), RT_RCDATA);
        if (resource) {
            const DWORD size = SizeofResource(GetModuleHandleW(nullptr), resource);
            HGLOBAL loaded = LoadResource(GetModuleHandleW(nullptr), resource);
            const void* bytes = loaded ? LockResource(loaded) : nullptr;
            HGLOBAL copy = bytes && size ? GlobalAlloc(GMEM_MOVEABLE, size) : nullptr;
            if (copy) {
                void* destination = GlobalLock(copy);
                std::memcpy(destination, bytes, size);
                GlobalUnlock(copy);
                if (CreateStreamOnHGlobal(copy, TRUE, &resourceStreams_[i]) == S_OK) {
                    pages_[i].reset(Gdiplus::Image::FromStream(resourceStreams_[i], FALSE));
                } else {
                    GlobalFree(copy);
                }
            }
        }
        if (!pages_[i] || pages_[i]->GetLastStatus() != Gdiplus::Ok) {
            pages_[i] = std::make_unique<Gdiplus::Image>((directory / names[i]).c_str());
        }
        if (!pages_[i] || pages_[i]->GetLastStatus() != Gdiplus::Ok) available_ = false;
    }
    return available_;
}

SkinRenderer::Layout SkinRenderer::layout(const RECT& client) const {
    const float width = static_cast<float>(client.right - client.left);
    const float height = static_cast<float>(client.bottom - client.top);
    const float scale = std::min(width / kWidth, height / kHeight);
    return {{(width - kWidth * scale) * 0.5f, (height - kHeight * scale) * 0.5f,
             kWidth * scale, kHeight * scale}, scale};
}

POINT SkinRenderer::toReference(const RECT& client, POINT point) const {
    const Layout value = layout(client);
    POINT result;
    result.x = static_cast<LONG>((point.x - value.destination.X) / value.scale);
    result.y = static_cast<LONG>((point.y - value.destination.Y) / value.scale);
    return result;
}

void SkinRenderer::draw(HDC dc, const RECT& client, int page,
                        const EffectSettings& settings, const CompanionStatus& status) {
    if (!available_ || page < 0 || page >= static_cast<int>(kPageCount)) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const Layout value = layout(client);
    graphics.DrawImage(pages_[page].get(), value.destination);
    // The page mockups originally carried a temporary three-stripe mark. Overlay
    // the approved FreeFlight emblem from an embedded resource on every page.
    graphics.DrawImage(pages_[kBrandIndex].get(), mapRect(value, 18, 12, 205, 94));
    switch (page) {
        case 0: drawOverview(graphics, value, status); break;
        case 1: drawEffects(graphics, value, settings); break;
        case 2: drawWeather(graphics, value, settings, status); break;
        case 3: drawPerformance(graphics, value, settings, status); break;
        case 4: drawProfile(graphics, value, status); break;
        case 5: drawAdvanced(graphics, value, settings, status); break;
    }
}

void SkinRenderer::drawOverview(Gdiplus::Graphics& g, const Layout& l,
                                const CompanionStatus& status) {
    cover(g, l, 284, 142, 120, 42); text(g, l, integer(status.state.formationProbability * 100) + L"%", 290, 145, 110, 38, 28, kText, Gdiplus::FontStyleBold);
    cover(g, l, 612, 142, 120, 42); text(g, l, integer(status.state.wingCondensationRatio * 100) + L"%", 618, 145, 110, 38, 28, kText, Gdiplus::FontStyleBold);
    cover(g, l, 940, 142, 145, 42); text(g, l, integer(status.state.particleBudgetRatio * 100) + L"%", 946, 145, 130, 38, 28, kText, Gdiplus::FontStyleBold);
    cover(g, l, 1268, 142, 140, 42); text(g, l, status.pluginRunning ? L"LIVE" : L"OFFLINE", 1274, 145, 125, 38, 27, status.pluginRunning ? kGreen : kMuted, Gdiplus::FontStyleBold);
    cover(g, l, 294, 286, 280, 26); text(g, l, status.profileName.empty() ? L"Waiting for supported aircraft" : std::wstring(status.profileName.begin(), status.profileName.end()), 294, 288, 350, 26, 14, kText, Gdiplus::FontStyleBold);
    cover(g, l, 305, 808, 160, 28, kDeep); text(g, l, integer(status.input.altitudeFt / 100.0f) == L"0" ? L"GROUND" : L"FL " + integer(status.input.altitudeFt / 100.0f), 310, 812, 150, 24, 13, kText, Gdiplus::FontStyleBold);
    cover(g, l, 500, 808, 160, 28, kDeep); text(g, l, integer(status.input.trueAirspeedMps * 1.94384f) + L" KT", 505, 812, 150, 24, 13, kText, Gdiplus::FontStyleBold);
    cover(g, l, 696, 808, 150, 28, kDeep); text(g, l, integer(status.input.ambientTemperatureC) + L" °C", 701, 812, 140, 24, 13, kText, Gdiplus::FontStyleBold);
    cover(g, l, 892, 808, 170, 28, kDeep); text(g, l, integer(status.state.contrailPersistenceSeconds) + L" SEC", 897, 812, 160, 24, 13, kText, Gdiplus::FontStyleBold);
    cover(g, l, 33, 930, 170, 34, kCard); text(g, l, status.pluginRunning ? L"● X-Plane connected" : L"○ Waiting for X-Plane", 34, 932, 170, 28, 11, status.pluginRunning ? kGreen : kMuted);
}

void SkinRenderer::drawEffects(Gdiplus::Graphics& g, const Layout& l,
                               const EffectSettings& s) {
    slider(g, l, 210, s.contrailIntensity / 2.0f);
    slider(g, l, 323, s.persistenceScale / 2.0f);
    toggle(g, l, 857, 114, s.enabled);
    toggle(g, l, 844, 568, s.wingVortexIntensity > 0.01f);
    toggle(g, l, 844, 622, s.wingCondensationIntensity > 0.01f);
    cover(g, l, 842, 178, 55, 22); text(g, l, integer(s.contrailIntensity * 100) + L"%", 842, 181, 55, 18, 12, kText, Gdiplus::FontStyleBold);
    cover(g, l, 840, 292, 58, 22); text(g, l, integer(s.persistenceScale * 248) + L" sec", 838, 294, 60, 18, 11, kText, Gdiplus::FontStyleBold);
}

void SkinRenderer::drawWeather(Gdiplus::Graphics& g, const Layout& l,
                               const EffectSettings& s, const CompanionStatus& status) {
    cover(g, l, 493, 224, 105, 52, kCard); text(g, l, integer(status.state.formationProbability * 100) + L"%", 510, 226, 90, 45, 32, kText, Gdiplus::FontStyleBold);
    cover(g, l, 301, 386, 130, 26, kDeep); text(g, l, integer(status.input.ambientTemperatureC) + L" °C", 307, 389, 120, 20, 12, kText, Gdiplus::FontStyleBold);
    cover(g, l, 560, 386, 130, 26, kDeep); text(g, l, integer(status.state.relativeHumidityIcePercent) + L"%", 566, 389, 120, 20, 12, kText, Gdiplus::FontStyleBold);
    cover(g, l, 301, 459, 130, 26, kDeep); text(g, l, integer(status.input.pressurePa / 100.0f) + L" hPa", 307, 462, 120, 20, 12, kText, Gdiplus::FontStyleBold);
    toggle(g, l, 761, 544, s.automaticWeather);
}

void SkinRenderer::drawPerformance(Gdiplus::Graphics& g, const Layout& l,
                                   const EffectSettings& s, const CompanionStatus& status) {
    const std::array<Gdiplus::RectF, 4> cards {{mapRect(l,274,97,315,138), mapRect(l,601,97,315,138), mapRect(l,928,97,315,138), mapRect(l,1255,97,315,138)}};
    Gdiplus::Pen border(kBlue, 2.0f * l.scale);
    g.DrawRectangle(&border, cards[std::clamp(static_cast<int>(s.quality), 0, 3)]);
    cover(g, l, 785, 272, 110, 34, kCard); text(g, l, integer(status.state.particleBudgetRatio * 100) + L"%", 800, 276, 90, 28, 20, kText, Gdiplus::FontStyleBold);
    toggle(g, l, 857, 410, s.adaptiveQuality);
    slider(g, l, 505, std::clamp((s.adaptiveTargetFps - 20.0f) / 70.0f, 0.0f, 1.0f));
}

void SkinRenderer::drawProfile(Gdiplus::Graphics& g, const Layout& l,
                               const CompanionStatus& status) {
    cover(g, l, 292, 130, 360, 34, kCard); text(g, l, status.profileName.empty() ? L"Waiting for supported aircraft" : std::wstring(status.profileName.begin(), status.profileName.end()), 294, 133, 350, 28, 15, kText, Gdiplus::FontStyleBold);
    cover(g, l, 913, 116, 86, 28, kCard); text(g, l, status.particleObjectLoaded ? L"CALIBRATED" : L"WAITING", 920, 120, 74, 20, 10, status.particleObjectLoaded ? kGreen : kMuted);
}

void SkinRenderer::drawAdvanced(Gdiplus::Graphics& g, const Layout& l,
                                const EffectSettings& s, const CompanionStatus& status) {
    toggle(g, l, 857, 114, s.previewMode);
    cover(g, l, 956, 177, 560, 100, kDeep);
    text(g, l, status.pluginRunning ? L"[LIVE] Plugin bridge connected\n[OK] Profile: " + (status.profileName.empty() ? L"waiting" : std::wstring(status.profileName.begin(), status.profileName.end())) + L"\n[OK] Particle object: " + (status.particleObjectLoaded ? L"attached" : L"waiting") + L"\n[OK] Formation probability: " + integer(status.state.formationProbability * 100) + L"%" : L"[WAIT] Start X-Plane to receive live diagnostics", 964, 183, 540, 88, 11, status.pluginRunning ? kGreen : kMuted);
    slider(g, l, 425, s.contrailIntensity / 2.0f);
    slider(g, l, 481, s.wingCondensationIntensity / 2.0f);
    slider(g, l, 537, s.persistenceScale / 2.0f);
}

}  // namespace ffatmo::app
