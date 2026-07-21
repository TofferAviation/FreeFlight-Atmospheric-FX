#include "GeometryDebugOverlay.h"

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
#include <vector>

namespace ffatmo {
namespace {

struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
    bool visible = false;
};

struct ProjectionContext {
    std::array<float, 16> aircraftMatrix {};
    std::array<float, 16> projectionMatrix {};
    int width = 0;
    int height = 0;
};

struct Label {
    ScreenPoint point;
    std::string text;
    std::array<float, 3> colour {};
};

void multiplyMatrixVector(float destination[4], const float matrix[16], const float vector[4]) {
    destination[0] = vector[0] * matrix[0] + vector[1] * matrix[4] +
                     vector[2] * matrix[8] + vector[3] * matrix[12];
    destination[1] = vector[0] * matrix[1] + vector[1] * matrix[5] +
                     vector[2] * matrix[9] + vector[3] * matrix[13];
    destination[2] = vector[0] * matrix[2] + vector[1] * matrix[6] +
                     vector[2] * matrix[10] + vector[3] * matrix[14];
    destination[3] = vector[0] * matrix[3] + vector[1] * matrix[7] +
                     vector[2] * matrix[11] + vector[3] * matrix[15];
}

ScreenPoint projectPoint(const ProjectionContext& context, const acf::Vec3& bodyPoint) {
    const float body[4] = {
        static_cast<float>(bodyPoint.x),
        static_cast<float>(bodyPoint.y),
        static_cast<float>(bodyPoint.z),
        1.0f
    };

    float eye[4] {};
    float clip[4] {};
    multiplyMatrixVector(eye, context.aircraftMatrix.data(), body);
    multiplyMatrixVector(clip, context.projectionMatrix.data(), eye);

    // Positive clip W means the marker is in front of the camera for the
    // OpenGL-compatible matrices X-Plane exposes to 2-D callbacks.
    if (!std::isfinite(clip[3]) || clip[3] <= 0.0001f) return {};

    const float inverseW = 1.0f / clip[3];
    const float ndcX = clip[0] * inverseW;
    const float ndcY = clip[1] * inverseW;
    if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) return {};

    // Keep a small margin so lines do not explode across the screen when one
    // endpoint is far outside the current view frustum.
    if (ndcX < -1.15f || ndcX > 1.15f || ndcY < -1.15f || ndcY > 1.15f) return {};

    ScreenPoint result;
    result.x = static_cast<float>(context.width) * (ndcX * 0.5f + 0.5f);
    result.y = static_cast<float>(context.height) * (ndcY * 0.5f + 0.5f);
    result.visible = true;
    return result;
}

acf::Vec3 extended(const acf::Vec3& origin, const acf::Vec3& direction, double length) {
    return {
        origin.x + direction.x * length,
        origin.y + direction.y * length,
        origin.z + direction.z * length
    };
}

void drawLine(const ScreenPoint& first, const ScreenPoint& second) {
    if (!first.visible || !second.visible) return;
    glVertex2f(first.x, first.y);
    glVertex2f(second.x, second.y);
}

void drawCross(const ScreenPoint& point, float radiusPixels) {
    if (!point.visible) return;
    glVertex2f(point.x - radiusPixels, point.y);
    glVertex2f(point.x + radiusPixels, point.y);
    glVertex2f(point.x, point.y - radiusPixels);
    glVertex2f(point.x, point.y + radiusPixels);
}

void addLabel(std::vector<Label>& labels,
              const ScreenPoint& point,
              std::string text,
              std::array<float, 3> colour) {
    if (!point.visible) return;
    labels.push_back({point, std::move(text), colour});
}

void drawStatusBanner(int screenHeight, const char* text, const float colour[3]) {
    const int left = 18;
    const int top = screenHeight - 18;
    const int right = 560;
    const int bottom = screenHeight - 52;
    XPLMDrawTranslucentDarkBox(left, top, right, bottom);
    XPLMDrawString(const_cast<float*>(colour),
                   left + 10,
                   bottom + 11,
                   const_cast<char*>(text),
                   nullptr,
                   xplmFont_Basic);
}

}  // namespace

GeometryDebugOverlay::~GeometryDebugOverlay() {
    stop();
}

bool GeometryDebugOverlay::start() {
    if (registered_) return true;

    aircraftMatrix_ = XPLMFindDataRef("sim/graphics/view/acf_matrix");
    projectionMatrix_ = XPLMFindDataRef("sim/graphics/view/projection_matrix_3d");
    windowWidth_ = XPLMFindDataRef("sim/graphics/view/window_width");
    windowHeight_ = XPLMFindDataRef("sim/graphics/view/window_height");

    if (!aircraftMatrix_ || !projectionMatrix_ || !windowWidth_ || !windowHeight_) {
        return false;
    }

    registeredPhase_ = xplm_Phase_Window;
    registeredBefore_ = 0;
    registered_ =
        XPLMRegisterDrawCallback(drawCallback, registeredPhase_, registeredBefore_, this) != 0;
    return registered_;
}

void GeometryDebugOverlay::stop() {
    if (!registered_) return;
    XPLMUnregisterDrawCallback(drawCallback, registeredPhase_, registeredBefore_, this);
    registered_ = false;
}

int GeometryDebugOverlay::drawCallback(XPLMDrawingPhase, int, void* refcon) {
    return static_cast<GeometryDebugOverlay*>(refcon)->draw();
}

int GeometryDebugOverlay::draw() {
    if (!enabled_) return 1;

    ProjectionContext context;
    if (XPLMGetDatavf(aircraftMatrix_, context.aircraftMatrix.data(), 0, 16) != 16 ||
        XPLMGetDatavf(projectionMatrix_, context.projectionMatrix.data(), 0, 16) != 16) {
        return 1;
    }
    context.width = XPLMGetDatai(windowWidth_);
    context.height = XPLMGetDatai(windowHeight_);
    if (context.width <= 0 || context.height <= 0) return 1;

    const float white[3] = {1.0f, 1.0f, 1.0f};
    const float warning[3] = {1.0f, 0.72f, 0.15f};
    const float error[3] = {1.0f, 0.25f, 0.20f};

    if (!result_) {
        drawStatusBanner(context.height,
                         "FFAtmo geometry overlay ACTIVE - waiting for ACF parser",
                         warning);
        return 1;
    }
    if (!result_->ok) {
        drawStatusBanner(context.height,
                         "FFAtmo geometry overlay ACTIVE - ACF parse failed; export report",
                         error);
        return 1;
    }

    char status[256] {};
    std::snprintf(status,
                  sizeof(status),
                  "FFAtmo geometry overlay ACTIVE | %s | engines: %zu | wing segments: %zu",
                  result_->profile.aircraftIcao.empty() ? "AIRCRAFT" : result_->profile.aircraftIcao.c_str(),
                  result_->profile.engines.size(),
                  result_->profile.mainWingSegments.size());
    drawStatusBanner(context.height, status, white);

    std::vector<Label> labels;
    labels.reserve(16);

    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    glLineWidth(3.0f);
    glBegin(GL_LINES);

    const acf::Vec3 origin {};
    const ScreenPoint datum = projectPoint(context, origin);

    glColor4f(1.0f, 0.15f, 0.15f, 0.95f);
    drawLine(datum, projectPoint(context, {2.5, 0.0, 0.0}));
    glColor4f(0.15f, 1.0f, 0.15f, 0.95f);
    drawLine(datum, projectPoint(context, {0.0, 2.5, 0.0}));
    glColor4f(0.15f, 0.45f, 1.0f, 0.95f);
    drawLine(datum, projectPoint(context, {0.0, 0.0, 2.5}));
    addLabel(labels, datum, "DATUM / CG", {1.0f, 1.0f, 1.0f});

    glColor4f(0.95f, 0.20f, 1.0f, 0.95f);
    for (const auto& segment : result_->profile.mainWingSegments) {
        drawLine(projectPoint(context, segment.rootBodyM),
                 projectPoint(context, segment.tipBodyM));
    }

    for (std::size_t index = 0; index < result_->profile.engines.size(); ++index) {
        const auto& engine = result_->profile.engines[index];
        const ScreenPoint centre = projectPoint(context, engine.centreBodyM);
        const ScreenPoint exhaust = projectPoint(context, engine.exhaustOriginBodyM);
        const ScreenPoint directionEnd =
            projectPoint(context, extended(engine.exhaustOriginBodyM,
                                           engine.exhaustDirectionBody,
                                           4.0));

        glColor4f(1.0f, 0.85f, 0.10f, 1.0f);
        drawCross(centre, 10.0f);
        glColor4f(1.0f, 0.35f, 0.05f, 1.0f);
        drawCross(exhaust, 8.0f);
        glColor4f(0.10f, 0.95f, 1.0f, 1.0f);
        drawLine(exhaust, directionEnd);

        addLabel(labels,
                 centre,
                 "ENG " + std::to_string(index + 1) + " CENTER",
                 {1.0f, 0.85f, 0.10f});
        addLabel(labels,
                 exhaust,
                 "ENG " + std::to_string(index + 1) + " EXHAUST",
                 {1.0f, 0.45f, 0.10f});
    }

    glColor4f(0.15f, 1.0f, 0.35f, 1.0f);
    if (result_->profile.hasLeftWingtip) {
        const ScreenPoint point = projectPoint(context, result_->profile.leftWingtipBodyM);
        drawCross(point, 12.0f);
        addLabel(labels, point, "LEFT WINGTIP", {0.15f, 1.0f, 0.35f});
    }
    if (result_->profile.hasRightWingtip) {
        const ScreenPoint point = projectPoint(context, result_->profile.rightWingtipBodyM);
        drawCross(point, 12.0f);
        addLabel(labels, point, "RIGHT WINGTIP", {0.15f, 1.0f, 0.35f});
    }

    glEnd();
    glLineWidth(1.0f);

    // Draw labels only after all OpenGL geometry. XPLMDrawString may change GL state.
    for (Label& label : labels) {
        XPLMDrawString(label.colour.data(),
                       static_cast<int>(label.point.x + 12.0f),
                       static_cast<int>(label.point.y + 5.0f),
                       label.text.data(),
                       nullptr,
                       xplmFont_Basic);
    }

    return 1;
}

}  // namespace ffatmo
