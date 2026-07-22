#include "ContrailDebugOverlay.h"

#include "XPLMGraphics.h"
#include "XPLMUtilities.h"

#if IBM
#include <windows.h>
#include <GL/gl.h>
#elif LIN
#include <GL/gl.h>
#else
#include <OpenGL/gl.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace ffatmo {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct ProjectionContext {
    std::array<float, 16> worldMatrix {};
    std::array<float, 16> projectionMatrix {};
    int width = 0;
    int height = 0;
};

struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
    float clipW = 0.0f;
    bool visible = false;
};

struct ProjectedParcel {
    ScreenPoint point;
    engine::Vec3d localPositionM {};
    std::uint32_t engineIndex = 0;
    float radiusPixels = 0.0f;
    float alpha = 0.0f;
    float ageRatio = 0.0f;
};

void multiplyMatrixVector(float destination[4],
                          const float matrix[16],
                          const float vector[4]) {
    destination[0] = vector[0] * matrix[0] + vector[1] * matrix[4] +
                     vector[2] * matrix[8] + vector[3] * matrix[12];
    destination[1] = vector[0] * matrix[1] + vector[1] * matrix[5] +
                     vector[2] * matrix[9] + vector[3] * matrix[13];
    destination[2] = vector[0] * matrix[2] + vector[1] * matrix[6] +
                     vector[2] * matrix[10] + vector[3] * matrix[14];
    destination[3] = vector[0] * matrix[3] + vector[1] * matrix[7] +
                     vector[2] * matrix[11] + vector[3] * matrix[15];
}

ScreenPoint projectLocalPoint(const ProjectionContext& context,
                              const engine::Vec3d& localPoint) {
    const float world[4] = {
        static_cast<float>(localPoint.x),
        static_cast<float>(localPoint.y),
        static_cast<float>(localPoint.z),
        1.0f
    };
    float eye[4] {};
    float clip[4] {};
    multiplyMatrixVector(eye, context.worldMatrix.data(), world);
    multiplyMatrixVector(clip, context.projectionMatrix.data(), eye);

    if (!std::isfinite(clip[3]) || clip[3] <= 0.0001f) return {};
    const float inverseW = 1.0f / clip[3];
    const float ndcX = clip[0] * inverseW;
    const float ndcY = clip[1] * inverseW;
    const float ndcZ = clip[2] * inverseW;
    if (!std::isfinite(ndcX) || !std::isfinite(ndcY) || !std::isfinite(ndcZ)) return {};
    if (ndcX < -1.20f || ndcX > 1.20f ||
        ndcY < -1.20f || ndcY > 1.20f ||
        ndcZ < -1.20f || ndcZ > 1.20f) {
        return {};
    }

    ScreenPoint result;
    result.x = static_cast<float>(context.width) * (ndcX * 0.5f + 0.5f);
    result.y = static_cast<float>(context.height) * (ndcY * 0.5f + 0.5f);
    result.clipW = clip[3];
    result.visible = true;
    return result;
}

float screenRadiusPixels(const ProjectionContext& context,
                         const ScreenPoint& point,
                         float physicalRadiusM) {
    if (!point.visible || point.clipW <= 0.0f) return 0.0f;
    const float verticalProjection = std::max(
        std::abs(context.projectionMatrix[5]), 0.25f);
    const float pixelsPerMetre = static_cast<float>(context.height) * 0.5f *
                                 verticalProjection / point.clipW;
    // Deliberate debug enlargement: parcel physics radii are real metres, but a
    // lightly enlarged coach mark makes young ice visible during validation.
    return std::clamp(physicalRadiusM * pixelsPerMetre * 1.65f, 1.8f, 44.0f);
}

float localDistance(const engine::Vec3d& first, const engine::Vec3d& second) {
    const double deltaX = first.x - second.x;
    const double deltaY = first.y - second.y;
    const double deltaZ = first.z - second.z;
    return static_cast<float>(std::sqrt(
        deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ));
}

void drawDisc(float x, float y, float radius, float red, float green, float blue, float alpha) {
    if (radius <= 0.0f || alpha <= 0.0f) return;
    constexpr int segments = 18;
    glColor4f(red, green, blue, alpha);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int index = 0; index <= segments; ++index) {
        const float angle = 2.0f * kPi * static_cast<float>(index) /
                            static_cast<float>(segments);
        glVertex2f(x + std::cos(angle) * radius,
                   y + std::sin(angle) * radius);
    }
    glEnd();
}

void drawSoftParcel(const ProjectedParcel& parcel) {
    const float old = std::clamp(parcel.ageRatio, 0.0f, 1.0f);
    const float red = 0.90f - 0.13f * old;
    const float green = 0.96f - 0.12f * old;
    const float blue = 1.00f - 0.10f * old;
    drawDisc(parcel.point.x,
             parcel.point.y,
             parcel.radiusPixels * 1.55f,
             red,
             green,
             blue,
             parcel.alpha * 0.16f);
    drawDisc(parcel.point.x,
             parcel.point.y,
             parcel.radiusPixels,
             red,
             green,
             blue,
             parcel.alpha * 0.30f);
    drawDisc(parcel.point.x,
             parcel.point.y,
             parcel.radiusPixels * 0.48f,
             0.96f,
             0.985f,
             1.0f,
             parcel.alpha * 0.42f);
}

void drawCross(const ScreenPoint& point, float radiusPixels) {
    if (!point.visible) return;
    glVertex2f(point.x - radiusPixels, point.y);
    glVertex2f(point.x + radiusPixels, point.y);
    glVertex2f(point.x, point.y - radiusPixels);
    glVertex2f(point.x, point.y + radiusPixels);
}

void drawStatusLine(int x, int y, const std::string& text, float colour[3]) {
    XPLMDrawString(colour,
                   x,
                   y,
                   const_cast<char*>(text.c_str()),
                   nullptr,
                   xplmFont_Basic);
}

}  // namespace

ContrailDebugOverlay::~ContrailDebugOverlay() {
    stop();
}

bool ContrailDebugOverlay::start() {
    if (registered_) return true;
    worldMatrix_ = XPLMFindDataRef("sim/graphics/view/world_matrix");
    projectionMatrix_ = XPLMFindDataRef("sim/graphics/view/projection_matrix_3d");
    windowWidth_ = XPLMFindDataRef("sim/graphics/view/window_width");
    windowHeight_ = XPLMFindDataRef("sim/graphics/view/window_height");
    if (!worldMatrix_ || !projectionMatrix_ || !windowWidth_ || !windowHeight_) {
        return false;
    }

    registeredPhase_ = xplm_Phase_Window;
    registeredBefore_ = 0;
    registered_ = XPLMRegisterDrawCallback(
        drawCallback, registeredPhase_, registeredBefore_, this) != 0;
    return registered_;
}

void ContrailDebugOverlay::stop() {
    if (!registered_) return;
    XPLMUnregisterDrawCallback(
        drawCallback, registeredPhase_, registeredBefore_, this);
    registered_ = false;
}

void ContrailDebugOverlay::setFrame(
    std::vector<ContrailDebugRenderParcel> parcels,
    std::vector<ContrailDebugRenderSource> sources,
    ContrailDebugOverlayStatus status) {
    parcels_ = std::move(parcels);
    sources_ = std::move(sources);
    status_ = std::move(status);
}

int ContrailDebugOverlay::drawCallback(XPLMDrawingPhase, int, void* refcon) {
    return static_cast<ContrailDebugOverlay*>(refcon)->draw();
}

int ContrailDebugOverlay::draw() {
    if (!enabled_) return 1;

    ProjectionContext context;
    if (XPLMGetDatavf(worldMatrix_, context.worldMatrix.data(), 0, 16) != 16 ||
        XPLMGetDatavf(projectionMatrix_, context.projectionMatrix.data(), 0, 16) != 16) {
        return 1;
    }
    context.width = XPLMGetDatai(windowWidth_);
    context.height = XPLMGetDatai(windowHeight_);
    if (context.width <= 0 || context.height <= 0) return 1;

    std::vector<ProjectedParcel> projected;
    projected.reserve(parcels_.size());
    for (const auto& parcel : parcels_) {
        const ScreenPoint point = projectLocalPoint(context, parcel.localPositionM);
        if (!point.visible) continue;
        const float visibility = std::clamp(
            (parcel.opticalDepth - 0.008f) / 0.22f, 0.0f, 1.0f);
        if (visibility <= 0.0f) continue;

        ProjectedParcel render;
        render.point = point;
        render.localPositionM = parcel.localPositionM;
        render.engineIndex = parcel.engineIndex;
        render.radiusPixels = screenRadiusPixels(context, point, parcel.radiusM);
        render.alpha = 0.18f + 0.72f * std::sqrt(visibility);
        render.ageRatio = std::clamp(parcel.ageSeconds / 55.0f, 0.0f, 1.0f);
        projected.push_back(render);
    }

    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Connect consecutive parcels from the same engine to make the discrete
    // deterministic samples read as one continuous debug trail.
    std::array<ProjectedParcel, engine::kMaximumRecordedEngines> previous {};
    std::array<bool, engine::kMaximumRecordedEngines> hasPrevious {};
    glLineWidth(2.4f);
    glBegin(GL_LINES);
    for (const auto& parcel : projected) {
        if (parcel.engineIndex >= engine::kMaximumRecordedEngines) continue;
        const std::size_t engineIndex = static_cast<std::size_t>(parcel.engineIndex);
        if (hasPrevious[engineIndex] &&
            localDistance(previous[engineIndex].localPositionM,
                          parcel.localPositionM) < 2000.0f) {
            const float alpha = std::min(previous[engineIndex].alpha, parcel.alpha) * 0.52f;
            glColor4f(0.86f, 0.94f, 1.0f, alpha);
            glVertex2f(previous[engineIndex].point.x, previous[engineIndex].point.y);
            glVertex2f(parcel.point.x, parcel.point.y);
        }
        previous[engineIndex] = parcel;
        hasPrevious[engineIndex] = true;
    }
    glEnd();
    glLineWidth(1.0f);

    for (const auto& parcel : projected) drawSoftParcel(parcel);

    // Cyan crosses show the parsed live exhaust source locations. They are
    // intentionally small and disappear once the overlay is disabled.
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (const auto& source : sources_) {
        const ScreenPoint point = projectLocalPoint(context, source.localPositionM);
        if (source.engineIndex == 0) {
            glColor4f(0.15f, 0.90f, 1.0f, 0.92f);
        } else {
            glColor4f(0.35f, 0.72f, 1.0f, 0.92f);
        }
        drawCross(point, 6.0f);
    }
    glEnd();
    glLineWidth(1.0f);

    const int left = 18;
    const int top = context.height - 18;
    const int right = 700;
    const int bottom = context.height - 92;
    XPLMDrawTranslucentDarkBox(left, top, right, bottom);

    char first[512] {};
    char second[512] {};
    char third[512] {};
    const float temperatureC = status_.temperatureK - 273.15f;
    std::snprintf(first,
                  sizeof(first),
                  "FFAtmo LIVE CONTRAIL DEBUG | %s | %s | %s",
                  status_.aircraftIcao.empty() ? "AIRCRAFT" : status_.aircraftIcao.c_str(),
                  status_.mode.c_str(),
                  status_.geometryStatus.c_str());
    std::snprintf(second,
                  sizeof(second),
                  "active: %llu | emitted: %llu | expired: %llu | peak: %llu | rebases: %llu",
                  static_cast<unsigned long long>(status_.activeParcels),
                  static_cast<unsigned long long>(status_.emittedParcels),
                  static_cast<unsigned long long>(status_.expiredParcels),
                  static_cast<unsigned long long>(status_.peakParcels),
                  static_cast<unsigned long long>(status_.originRebases));
    std::snprintf(third,
                  sizeof(third),
                  "formation: %.1f%% | RHi: %.1f%% | temperature: %.1f C | physics: %s",
                  status_.formationPotential * 100.0f,
                  status_.relativeHumidityIcePercent,
                  temperatureC,
                  status_.physicsFrozen ? "FROZEN" :
                      (status_.simulationEnabled ? "RUNNING" : "DISABLED"));

    float titleColour[3] = {0.72f, 0.92f, 1.0f};
    float bodyColour[3] = {1.0f, 1.0f, 1.0f};
    float stateColour[3] = {
        status_.physicsFrozen ? 1.0f : 0.55f,
        status_.physicsFrozen ? 0.75f : 1.0f,
        status_.physicsFrozen ? 0.20f : 0.65f
    };
    drawStatusLine(left + 10, bottom + 55, first, titleColour);
    drawStatusLine(left + 10, bottom + 35, second, bodyColour);
    drawStatusLine(left + 10, bottom + 15, third, stateColour);

    return 1;
}

}  // namespace ffatmo
