#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>

#include "CompanionBridge.h"

#include <array>
#include <filesystem>
#include <memory>

namespace ffatmo::app {

class SkinRenderer {
public:
    struct Layout { Gdiplus::RectF destination; float scale = 1.0f; };

    SkinRenderer();
    ~SkinRenderer();

    bool load(const std::filesystem::path& directory);
    bool available() const { return available_; }
    void draw(HDC dc, const RECT& client, int page, const EffectSettings& settings,
              const CompanionStatus& status);
    POINT toReference(const RECT& client, POINT point) const;

private:
    Layout layout(const RECT& client) const;
    void drawOverview(Gdiplus::Graphics& graphics, const Layout& layout,
                      const CompanionStatus& status);
    void drawEffects(Gdiplus::Graphics& graphics, const Layout& layout,
                     const EffectSettings& settings);
    void drawWeather(Gdiplus::Graphics& graphics, const Layout& layout,
                     const EffectSettings& settings, const CompanionStatus& status);
    void drawPerformance(Gdiplus::Graphics& graphics, const Layout& layout,
                         const EffectSettings& settings, const CompanionStatus& status);
    void drawProfile(Gdiplus::Graphics& graphics, const Layout& layout,
                     const CompanionStatus& status);
    void drawAdvanced(Gdiplus::Graphics& graphics, const Layout& layout,
                      const EffectSettings& settings, const CompanionStatus& status);

    ULONG_PTR gdiplusToken_ = 0;
    static constexpr size_t kPageCount = 6;
    static constexpr size_t kBrandIndex = 6;
    std::array<std::unique_ptr<Gdiplus::Image>, 7> pages_;
    std::array<IStream*, 7> resourceStreams_ {};
    bool available_ = false;
};

}  // namespace ffatmo::app
