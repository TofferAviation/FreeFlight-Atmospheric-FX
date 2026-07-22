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

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace ffatmo {
namespace {

struct ProjectionContext {
    std::array<float, 16> worldMatrix {};
    std::array<float, 16> projectionMatrix {};
    int width = 0;
    int height = 0;
};

struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
    bool visible = false;
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
    if (ndcX < -1.15f || ndcX > 1.15f ||
        ndcY < -1.15f || ndcY > 1.15f ||
        ndcZ < -1.15f || ndcZ > 1.15f) return {};

    return {
        static_cast<float>(context.width) * (ndcX * 0.5f + 0.5f),
        static_cast<float>(context.height) * (ndcY * 0.5f + 0.5f),
        true
    };
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
    XPLMUnregisterDrawCallback(drawCallback,
                               registeredPhase_,
                               registeredBefore_,
                               this);
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

    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // The old parcel discs and connector lines were intentionally removed.
    // Only the tiny exhaust-source coach marks remain as alignment diagnostics;
    // actual contrail visuals are X-Plane-managed world-object instances.
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (const auto& source : sources_) {
        const ScreenPoint point = projectLocalPoint(context, source.localPositionM);
        if (source.engineIndex == 0) {
            glColor4f(0.15f, 0.90f, 1.0f, 0.80f);
        } else {
            glColor4f(0.35f, 0.72f, 1.0f, 0.80f);
        }
        drawCross(point, 5.0f);
    }
    glEnd();
    glLineWidth(1.0f);

    const int left = 18;
    const int top = context.height - 18;
    const int right = 790;
    const int bottom = context.height - 112;
    XPLMDrawTranslucentDarkBox(left, top, right, bottom);

    char first[640] {};
    char second[640] {};
    char third[640] {};
    char fourth[640] {};
    const float temperatureC = status_.temperatureK - 273.15f;
    std::snprintf(first,
                  sizeof(first),
                  "FFAtmo WORLD CONTRAIL DEBUG | %s | %s | %s",
                  status_.aircraftIcao.empty() ? "AIRCRAFT" : status_.aircraftIcao.c_str(),
                  status_.mode.c_str(),
                  status_.geometryStatus.c_str());
    std::snprintf(second,
                  sizeof(second),
                  "renderer: %s | instances: %llu | parcels: %llu | emitted: %llu | expired: %llu",
                  status_.rendererStatus.c_str(),
                  static_cast<unsigned long long>(status_.visibleInstances),
                  static_cast<unsigned long long>(status_.activeParcels),
                  static_cast<unsigned long long>(status_.emittedParcels),
                  static_cast<unsigned long long>(status_.expiredParcels));
    std::snprintf(third,
                  sizeof(third),
                  "formation: %.1f%% | RHi: %.1f%% | temperature: %.1f C | physics: %s",
                  status_.formationPotential * 100.0f,
                  status_.relativeHumidityIcePercent,
                  temperatureC,
                  status_.physicsFrozen ? "FROZEN" :
                      (status_.simulationEnabled ? "RUNNING" : "DISABLED"));
    std::snprintf(fourth,
                  sizeof(fourth),
                  "preview gate: %s | peak parcels: %llu | origin rebases: %llu",
                  status_.previewGateOpen ? "AIRBORNE" : "WAITING FOR AIRBORNE / AIRSPEED",
                  static_cast<unsigned long long>(status_.peakParcels),
                  static_cast<unsigned long long>(status_.originRebases));

    float titleColour[3] = {0.72f, 0.92f, 1.0f};
    float bodyColour[3] = {1.0f, 1.0f, 1.0f};
    float stateColour[3] = {
        status_.rendererReady ? 0.45f : 1.0f,
        status_.rendererReady ? 1.0f : 0.55f,
        status_.rendererReady ? 0.55f : 0.25f
    };
    drawStatusLine(left + 10, top - 19, first, titleColour);
    drawStatusLine(left + 10, top - 39, second, stateColour);
    drawStatusLine(left + 10, top - 59, third, bodyColour);
    drawStatusLine(left + 10, top - 79, fourth, bodyColour);
    return 1;
}

}  // namespace ffatmo
