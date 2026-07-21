#include "GeometryDebugOverlay.h"

#include "XPLMGraphics.h"

#if IBM
#include <windows.h>
#include <GL/gl.h>
#elif LIN
#include <GL/gl.h>
#else
#include <OpenGL/gl.h>
#endif

namespace ffatmo {
namespace {

void vertex(const acf::Vec3& value) {
    glVertex3d(value.x, value.y, value.z);
}

void drawCross(const acf::Vec3& point, double radius) {
    glBegin(GL_LINES);
    glVertex3d(point.x - radius, point.y, point.z);
    glVertex3d(point.x + radius, point.y, point.z);
    glVertex3d(point.x, point.y - radius, point.z);
    glVertex3d(point.x, point.y + radius, point.z);
    glVertex3d(point.x, point.y, point.z - radius);
    glVertex3d(point.x, point.y, point.z + radius);
    glEnd();
}

acf::Vec3 extended(const acf::Vec3& origin, const acf::Vec3& direction, double length) {
    return {
        origin.x + direction.x * length,
        origin.y + direction.y * length,
        origin.z + direction.z * length
    };
}

}  // namespace

GeometryDebugOverlay::~GeometryDebugOverlay() {
    stop();
}

bool GeometryDebugOverlay::start() {
    if (registered_) return true;

    registeredPhase_ = xplm_Phase_Modern3D;
    registeredBefore_ = 0;
    registered_ =
        XPLMRegisterDrawCallback(drawCallback, registeredPhase_, registeredBefore_, this) != 0;

    if (!registered_) {
        registeredPhase_ = xplm_Phase_Airplanes;
        registeredBefore_ = 0;
        registered_ =
            XPLMRegisterDrawCallback(drawCallback, registeredPhase_, registeredBefore_, this) != 0;
    }
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
    if (!enabled_ || !result_ || !result_->ok) return 1;

    XPLMSetGraphicsState(0, 0, 0, 0, 1, 1, 0);

    glPushMatrix();
    glTranslated(pose_.localX, pose_.localY, pose_.localZ);
    glRotated(-pose_.headingDeg, 0.0, 1.0, 0.0);
    glRotated(-pose_.pitchDeg, -1.0, 0.0, 0.0);
    glRotated(-pose_.rollDeg, 0.0, 0.0, 1.0);

    glLineWidth(3.0f);

    glBegin(GL_LINES);
    glColor4f(1.0f, 0.15f, 0.15f, 0.95f);
    glVertex3d(0.0, 0.0, 0.0);
    glVertex3d(2.5, 0.0, 0.0);
    glColor4f(0.15f, 1.0f, 0.15f, 0.95f);
    glVertex3d(0.0, 0.0, 0.0);
    glVertex3d(0.0, 2.5, 0.0);
    glColor4f(0.15f, 0.45f, 1.0f, 0.95f);
    glVertex3d(0.0, 0.0, 0.0);
    glVertex3d(0.0, 0.0, 2.5);
    glEnd();

    glColor4f(0.95f, 0.20f, 1.0f, 0.90f);
    glBegin(GL_LINES);
    for (const auto& segment : result_->profile.mainWingSegments) {
        vertex(segment.rootBodyM);
        vertex(segment.tipBodyM);
    }
    glEnd();

    glColor4f(1.0f, 0.85f, 0.10f, 1.0f);
    for (const auto& engine : result_->profile.engines) {
        drawCross(engine.centreBodyM, 0.55);
    }

    glColor4f(1.0f, 0.35f, 0.05f, 1.0f);
    for (const auto& engine : result_->profile.engines) {
        drawCross(engine.exhaustOriginBodyM, 0.40);
    }

    glColor4f(0.10f, 0.95f, 1.0f, 1.0f);
    glBegin(GL_LINES);
    for (const auto& engine : result_->profile.engines) {
        vertex(engine.exhaustOriginBodyM);
        vertex(extended(engine.exhaustOriginBodyM, engine.exhaustDirectionBody, 4.0));
    }
    glEnd();

    glColor4f(0.15f, 1.0f, 0.35f, 1.0f);
    if (result_->profile.hasLeftWingtip) {
        drawCross(result_->profile.leftWingtipBodyM, 0.70);
    }
    if (result_->profile.hasRightWingtip) {
        drawCross(result_->profile.rightWingtipBodyM, 0.70);
    }

    glLineWidth(1.0f);
    glPopMatrix();
    return 1;
}

}  // namespace ffatmo
