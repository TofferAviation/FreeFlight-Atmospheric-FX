#pragma once

#include "render/ContrailRenderPlanner.h"

#include "XPLMCamera.h"
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMUtilities.h"

#if IBM
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <GL/gl.h>
#  include <GL/glext.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ffatmo {

// Renderer Foundation v6.0: actual 3-D density volumes.
//
// This renderer deliberately does not use X-Plane particle billboards, axial
// billboards, ribbons, OBJ cards, or camera-facing impostor geometry. The wake
// solver remains CPU-side; rendering occurs only from xplm_Phase_Modern3D.
// Each selected wake region becomes a camera-independent oriented 3-D volume
// cell. A fragment shader ray-marches procedural ice density through that cell
// and composites the resulting extinction/scattering into the X-Plane scene.
//
// v6.0 is a Windows/Vulkan visual foundation. The modern 3-D callback is the
// documented path for custom weather-style drawing under Vulkan. Metal does not
// expose this callback, so a separate macOS renderer would be required later.
class ContrailVolumeRenderer {
public:
    static constexpr std::size_t kAssetCount = 1;
    static constexpr std::size_t kInstancesPerAsset = 192;  // diagnostic: max volume cells
    static constexpr std::size_t kVisibleCapacity = 4096;   // planner input capacity
    static constexpr std::size_t kDiagnosticAssetCount = render::kContrailRenderAssetCount;
    using DiagnosticCounts = std::array<std::size_t, kDiagnosticAssetCount>;

    ~ContrailVolumeRenderer() { stop(); }

    bool start(const std::filesystem::path&) {
        if (running_) return true;
#if !IBM
        log("Renderer v6.0 volumetric proof currently targets Windows/Vulkan only.\n");
        return false;
#else
        worldRenderTypeRef_ = XPLMFindDataRef("sim/graphics/view/world_render_type");
        reverseZRef_ = XPLMFindDataRef("sim/graphics/view/is_reverse_float_z");
        reverseYRef_ = XPLMFindDataRef("sim/graphics/view/is_reverse_y");
        if (!worldRenderTypeRef_ || !reverseZRef_ || !reverseYRef_) {
            log("Renderer v6.0 could not resolve modern 3-D drawing datarefs.\n");
            return false;
        }
        if (!XPLMRegisterDrawCallback(drawCallback, xplm_Phase_Modern3D, 0, this)) {
            log("Renderer v6.0 could not register xplm_Phase_Modern3D.\n");
            return false;
        }
        drawCallbackRegistered_ = true;
        running_ = true;
        enabled_ = true;
        log("Renderer v6.0 armed: procedural 3-D volume ray-march callback registered.\n");
        return true;
#endif
    }

    void stop() {
#if IBM
        if (drawCallbackRegistered_) {
            XPLMUnregisterDrawCallback(drawCallback, xplm_Phase_Modern3D, 0, this);
            drawCallbackRegistered_ = false;
        }
        // OpenGL resources are owned by the plugin OpenGL context. Delete them
        // only when the function entry points were successfully resolved.
        if (gl_.DeleteProgram && program_) gl_.DeleteProgram(program_);
        if (gl_.DeleteShader && vertexShader_) gl_.DeleteShader(vertexShader_);
        if (gl_.DeleteShader && fragmentShader_) gl_.DeleteShader(fragmentShader_);
        program_ = 0;
        vertexShader_ = 0;
        fragmentShader_ = 0;
        gpuReady_ = false;
#endif
        cells_.clear();
        running_ = false;
        visibleCellCount_ = 0;
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
    }

    void update(const std::vector<render::ContrailRenderSample>& samples) {
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        visibleCellCount_ = 0;
        maximumWakeTurnDeg_ = 0.0;
        maximumWakeDescentM_ = 0.0;
        swirlCandidateCount_ = 0;
        if (!enabled_ || !running_ || samples.empty()) {
            cells_.clear();
            return;
        }

        std::array<std::vector<const render::ContrailRenderSample*>, 2> streams;
        for (const auto& sample : samples) {
            if (sample.engineIndex >= streams.size()) continue;
            if (!finiteSample(sample) || sample.opacityStrength <= 0.001f) continue;
            streams[sample.engineIndex].push_back(&sample);
        }
        for (auto& stream : streams) {
            std::stable_sort(stream.begin(), stream.end(), [](const auto* lhs, const auto* rhs) {
                if (lhs->ageSeconds != rhs->ageSeconds) return lhs->ageSeconds < rhs->ageSeconds;
                return lhs->renderId < rhs->renderId;
            });
        }

        std::vector<VolumeCell> next;
        next.reserve(kInstancesPerAsset);
        const std::size_t perEngineBudget = kInstancesPerAsset / 2;
        for (std::size_t engine = 0; engine < streams.size(); ++engine) {
            buildStreamCells(streams[engine], static_cast<std::uint32_t>(engine), perEngineBudget, next);
            updateDiagnostics(streams[engine]);
        }
        cells_.swap(next);
        visibleCellCount_ = cells_.size();
        selectedPerAsset_[0] = cells_.size();
    }

    void setEnabled(bool enabled) {
        enabled_ = enabled;
        if (!enabled_) cells_.clear();
    }

    bool enabled() const { return enabled_; }
    bool ready() const { return running_ && drawCallbackRegistered_; }
    std::size_t visibleInstanceCount() const { return visibleCellCount_; }
    std::size_t loadedObjectCount() const { return gpuReady_ ? 1u : 0u; }
    std::uint64_t poolCapacityDropCount() const { return 0; }
    std::size_t ownershipReuseCount() const { return visibleCellCount_; }
    std::size_t ownershipNewBindingCount() const { return 0; }
    std::size_t ownershipReleaseCount() const { return 0; }
    std::size_t swirlCandidateCount() const { return swirlCandidateCount_; }
    double maximumWakeTurnDeg() const { return maximumWakeTurnDeg_; }
    double maximumWakeDescentM() const { return maximumWakeDescentM_; }
    double maximumBillboardErrorDeg() const { return 0.0; }
    double maximumTrailAlignmentErrorDeg() const { return 0.0; }
    double minimumTrailProjectionFactor() const { return 1.0; }
    double maximumLengthCompressionRatio() const { return 0.0; }
    const DiagnosticCounts& selectedPerAsset() const { return selectedPerAsset_; }
    const DiagnosticCounts& renderedPerAsset() const { return renderedPerAsset_; }

private:
    struct VolumeCell {
        engine::Vec3d centre {};
        engine::Vec3d tangent {0.0, 0.0, -1.0};
        float halfLengthM = 12.0f;
        float radiusM = 1.5f;
        float extinction = 0.5f;
        float ageSeconds = 0.0f;
        float swirl = 0.0f;
        std::uint32_t engineIndex = 0;
        std::uint64_t seed = 0;
    };

#if IBM
    struct GlFunctions {
        PFNGLCREATESHADERPROC CreateShader = nullptr;
        PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
        PFNGLCOMPILESHADERPROC CompileShader = nullptr;
        PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
        PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = nullptr;
        PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
        PFNGLATTACHSHADERPROC AttachShader = nullptr;
        PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
        PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
        PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog = nullptr;
        PFNGLUSEPROGRAMPROC UseProgram = nullptr;
        PFNGLDELETEPROGRAMPROC DeleteProgram = nullptr;
        PFNGLDELETESHADERPROC DeleteShader = nullptr;
        PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
        PFNGLUNIFORM1FPROC Uniform1f = nullptr;
        PFNGLUNIFORM3FPROC Uniform3f = nullptr;
    };
#endif

    static void log(const std::string& message) {
        XPLMDebugString(("[FFAtmo Contrail Volume Renderer] " + message).c_str());
    }

    static float smoothstep(float edge0, float edge1, float value) {
        if (edge0 == edge1) return value >= edge1 ? 1.0f : 0.0f;
        const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    static double distanceM(const engine::Vec3d& a, const engine::Vec3d& b) {
        const double dx = a.x - b.x;
        const double dy = a.y - b.y;
        const double dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    static engine::Vec3d normalize(engine::Vec3d value) {
        const double length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        if (!std::isfinite(length) || length < 1.0e-7) return {0.0, 0.0, -1.0};
        value.x /= length;
        value.y /= length;
        value.z /= length;
        return value;
    }

    static engine::Vec3d cross(const engine::Vec3d& a, const engine::Vec3d& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    static double dot(const engine::Vec3d& a, const engine::Vec3d& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static bool finiteSample(const render::ContrailRenderSample& sample) {
        return std::isfinite(sample.localPositionM.x) &&
               std::isfinite(sample.localPositionM.y) &&
               std::isfinite(sample.localPositionM.z) &&
               std::isfinite(sample.trailTangentLocal.x) &&
               std::isfinite(sample.trailTangentLocal.y) &&
               std::isfinite(sample.trailTangentLocal.z) &&
               std::isfinite(sample.widthM) && sample.widthM > 0.0f &&
               std::isfinite(sample.opacityStrength) && sample.opacityStrength >= 0.0f &&
               std::isfinite(sample.ageSeconds);
    }

    static std::uint64_t mix64(std::uint64_t value) {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    void buildStreamCells(
        const std::vector<const render::ContrailRenderSample*>& stream,
        std::uint32_t engineIndex,
        std::size_t budget,
        std::vector<VolumeCell>& output) const {
        if (stream.empty() || budget == 0) return;

        std::vector<const render::ContrailRenderSample*> anchors;
        anchors.reserve(budget);
        engine::Vec3d lastPosition {};
        bool haveLast = false;
        for (const auto* sample : stream) {
            const float targetSpacing = sample->ageSeconds < 5.0f ? 16.0f :
                                        sample->ageSeconds < 18.0f ? 28.0f :
                                        sample->ageSeconds < 36.0f ? 48.0f : 72.0f;
            if (!haveLast || distanceM(lastPosition, sample->localPositionM) >= targetSpacing) {
                anchors.push_back(sample);
                lastPosition = sample->localPositionM;
                haveLast = true;
                if (anchors.size() >= budget) break;
            }
        }
        if (anchors.empty()) anchors.push_back(stream.front());

        for (std::size_t index = 0; index < anchors.size(); ++index) {
            const auto& sample = *anchors[index];
            double neighbourGap = 18.0;
            if (index + 1 < anchors.size()) {
                neighbourGap = distanceM(sample.localPositionM, anchors[index + 1]->localPositionM);
            } else if (index > 0) {
                neighbourGap = distanceM(anchors[index - 1]->localPositionM, sample.localPositionM);
            }
            neighbourGap = std::clamp(neighbourGap, 8.0, 100.0);

            const float ageExpansion =
                0.8f +
                1.2f * smoothstep(2.0f, 10.0f, sample.ageSeconds) +
                2.4f * smoothstep(10.0f, 35.0f, sample.ageSeconds);
            const float physicalRadius = std::max(sample.widthM * 0.55f, ageExpansion);
            const float radius = std::clamp(physicalRadius, 0.65f, 13.0f);
            const float halfLength = static_cast<float>(
                std::clamp(neighbourGap * 0.72, 7.0, 58.0));
            const float optical = std::sqrt(std::max(sample.opacityStrength, 0.0f));
            const float ageFade = 1.0f - 0.55f * smoothstep(42.0f, 65.0f, sample.ageSeconds);
            const float extinction = std::clamp(
                (0.35f + 1.35f * optical) * ageFade,
                0.22f,
                1.80f);
            const float rollRamp = smoothstep(4.0f, 20.0f, sample.ageSeconds);
            const float rollDecay = 1.0f - 0.55f * smoothstep(28.0f, 58.0f, sample.ageSeconds);
            const float engineSign = engineIndex == 0 ? -1.0f : 1.0f;

            VolumeCell cell;
            cell.centre = sample.localPositionM;
            cell.tangent = normalize(sample.trailTangentLocal);
            cell.halfLengthM = halfLength;
            cell.radiusM = radius;
            cell.extinction = extinction;
            cell.ageSeconds = sample.ageSeconds;
            cell.swirl = engineSign * rollRamp * rollDecay *
                std::clamp(sample.ageSeconds * 0.12f, 0.0f, 5.0f);
            cell.engineIndex = engineIndex;
            cell.seed = mix64(sample.renderId ^ (static_cast<std::uint64_t>(engineIndex) << 61U));
            output.push_back(cell);
        }
    }

    void updateDiagnostics(const std::vector<const render::ContrailRenderSample*>& stream) {
        if (stream.empty()) return;
        maximumWakeDescentM_ = std::max(
            maximumWakeDescentM_,
            std::max(0.0, stream.front()->localPositionM.y - stream.back()->localPositionM.y));
        for (const auto* sample : stream) {
            if (sample->ageSeconds >= 4.0f && sample->ageSeconds <= 45.0f) ++swirlCandidateCount_;
        }
        for (std::size_t i = 1; i < stream.size(); ++i) {
            const auto a = normalize(stream[i - 1]->trailTangentLocal);
            const auto b = normalize(stream[i]->trailTangentLocal);
            const double cosine = std::clamp(dot(a, b), -1.0, 1.0);
            maximumWakeTurnDeg_ = std::max(
                maximumWakeTurnDeg_,
                std::acos(cosine) * 57.29577951308232);
        }
    }

#if IBM
    template <typename T>
    static T loadProc(const char* name) {
        PROC proc = wglGetProcAddress(name);
        if (!proc || proc == reinterpret_cast<PROC>(1) ||
            proc == reinterpret_cast<PROC>(2) ||
            proc == reinterpret_cast<PROC>(3) ||
            proc == reinterpret_cast<PROC>(-1)) {
            return nullptr;
        }
        return reinterpret_cast<T>(proc);
    }

    bool loadGlFunctions() {
        gl_.CreateShader = loadProc<PFNGLCREATESHADERPROC>("glCreateShader");
        gl_.ShaderSource = loadProc<PFNGLSHADERSOURCEPROC>("glShaderSource");
        gl_.CompileShader = loadProc<PFNGLCOMPILESHADERPROC>("glCompileShader");
        gl_.GetShaderiv = loadProc<PFNGLGETSHADERIVPROC>("glGetShaderiv");
        gl_.GetShaderInfoLog = loadProc<PFNGLGETSHADERINFOLOGPROC>("glGetShaderInfoLog");
        gl_.CreateProgram = loadProc<PFNGLCREATEPROGRAMPROC>("glCreateProgram");
        gl_.AttachShader = loadProc<PFNGLATTACHSHADERPROC>("glAttachShader");
        gl_.LinkProgram = loadProc<PFNGLLINKPROGRAMPROC>("glLinkProgram");
        gl_.GetProgramiv = loadProc<PFNGLGETPROGRAMIVPROC>("glGetProgramiv");
        gl_.GetProgramInfoLog = loadProc<PFNGLGETPROGRAMINFOLOGPROC>("glGetProgramInfoLog");
        gl_.UseProgram = loadProc<PFNGLUSEPROGRAMPROC>("glUseProgram");
        gl_.DeleteProgram = loadProc<PFNGLDELETEPROGRAMPROC>("glDeleteProgram");
        gl_.DeleteShader = loadProc<PFNGLDELETESHADERPROC>("glDeleteShader");
        gl_.GetUniformLocation = loadProc<PFNGLGETUNIFORMLOCATIONPROC>("glGetUniformLocation");
        gl_.Uniform1f = loadProc<PFNGLUNIFORM1FPROC>("glUniform1f");
        gl_.Uniform3f = loadProc<PFNGLUNIFORM3FPROC>("glUniform3f");
        return gl_.CreateShader && gl_.ShaderSource && gl_.CompileShader && gl_.GetShaderiv &&
               gl_.GetShaderInfoLog && gl_.CreateProgram && gl_.AttachShader && gl_.LinkProgram &&
               gl_.GetProgramiv && gl_.GetProgramInfoLog && gl_.UseProgram && gl_.DeleteProgram &&
               gl_.DeleteShader && gl_.GetUniformLocation && gl_.Uniform1f && gl_.Uniform3f;
    }

    static const char* vertexShaderSource() {
        return R"GLSL(#version 120
varying vec3 vLocal;
void main() {
    vLocal = gl_MultiTexCoord0.xyz;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)GLSL";
    }

    static const char* fragmentShaderSource() {
        return R"GLSL(#version 120
varying vec3 vLocal;
uniform vec3 uCameraLocal;
uniform float uExtinction;
uniform float uSwirl;
uniform float uAge;
uniform float uSeed;

float hash31(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7)) + uSeed * 17.0) * 43758.5453);
}

float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(i + vec3(0.0,0.0,0.0));
    float n100 = hash31(i + vec3(1.0,0.0,0.0));
    float n010 = hash31(i + vec3(0.0,1.0,0.0));
    float n110 = hash31(i + vec3(1.0,1.0,0.0));
    float n001 = hash31(i + vec3(0.0,0.0,1.0));
    float n101 = hash31(i + vec3(1.0,0.0,1.0));
    float n011 = hash31(i + vec3(0.0,1.0,1.0));
    float n111 = hash31(i + vec3(1.0,1.0,1.0));
    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    return mix(mix(nx00, nx10, f.y), mix(nx01, nx11, f.y), f.z);
}

float fbm(vec3 p) {
    float value = 0.0;
    float weight = 0.56;
    value += weight * noise3(p); p = p * 2.03 + vec3(11.3,7.1,5.7); weight *= 0.48;
    value += weight * noise3(p); p = p * 2.01 + vec3(3.7,13.9,9.2); weight *= 0.48;
    value += weight * noise3(p);
    return value;
}

float densityAt(vec3 p) {
    float edge = 1.0 - smoothstep(0.72, 1.0, abs(p.x));
    float phase = uSwirl + p.x * 0.75 * uSwirl;
    float c = cos(phase);
    float s = sin(phase);
    vec2 yz = vec2(c * p.y - s * p.z, s * p.y + c * p.z);
    float r = length(yz);
    float core = exp(-2.7 * r * r);

    float maturity = smoothstep(4.0, 22.0, uAge);
    vec2 offset = vec2(0.28 + 0.10 * maturity, 0.0);
    float lobeA = exp(-9.0 * dot(yz - offset, yz - offset));
    float lobeB = exp(-9.0 * dot(yz + offset, yz + offset));
    float roll = mix(core, 0.58 * core + 0.36 * (lobeA + lobeB), maturity);

    float coarse = fbm(p * vec3(2.2, 3.4, 3.4) + vec3(uAge * 0.025));
    float fine = fbm(p * vec3(5.5, 7.0, 7.0) + vec3(4.0,2.0,uAge * 0.04));
    float structure = clamp(0.62 + coarse * 0.52 + fine * 0.20, 0.20, 1.35);
    float envelope = 1.0 - smoothstep(0.62, 1.02, r);
    return max(0.0, roll * structure * envelope * edge);
}

void main() {
    if (!gl_FrontFacing) discard;
    vec3 rayDir = normalize(vLocal - uCameraLocal);
    vec3 p = vLocal + rayDir * 0.012;
    float transmittance = 1.0;
    vec3 accumulated = vec3(0.0);
    const float stepLength = 0.105;

    for (int i = 0; i < 34; ++i) {
        if (max(max(abs(p.x), abs(p.y)), abs(p.z)) > 1.02) break;
        float density = densityAt(p);
        float stepAlpha = 1.0 - exp(-density * uExtinction * stepLength);
        float bright = 0.82 + 0.18 * clamp(1.0 - abs(p.y) * 0.55, 0.0, 1.0);
        vec3 iceColour = vec3(0.90, 0.94, 1.0) * bright;
        accumulated += transmittance * stepAlpha * iceColour;
        transmittance *= (1.0 - stepAlpha);
        if (transmittance < 0.025) break;
        p += rayDir * stepLength;
    }

    float alpha = 1.0 - transmittance;
    if (alpha < 0.004) discard;
    vec3 colour = accumulated / max(alpha, 0.0001);
    gl_FragColor = vec4(colour, alpha);
}
)GLSL";
    }

    GLuint compileShader(GLenum type, const char* source, const char* label) {
        const GLuint shader = gl_.CreateShader(type);
        if (!shader) return 0;
        gl_.ShaderSource(shader, 1, &source, nullptr);
        gl_.CompileShader(shader);
        GLint ok = GL_FALSE;
        gl_.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (ok == GL_TRUE) return shader;
        char buffer[2048] {};
        GLsizei length = 0;
        gl_.GetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(buffer) - 1), &length, buffer);
        log(std::string("v6.0 ") + label + " shader compile failed: " + buffer + "\n");
        gl_.DeleteShader(shader);
        return 0;
    }

    bool ensureGpuReady() {
        if (gpuReady_) return true;
        if (!loadGlFunctions()) {
            log("Renderer v6.0 could not resolve required OpenGL shader entry points.\n");
            return false;
        }
        vertexShader_ = compileShader(GL_VERTEX_SHADER, vertexShaderSource(), "vertex");
        fragmentShader_ = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource(), "fragment");
        if (!vertexShader_ || !fragmentShader_) return false;
        program_ = gl_.CreateProgram();
        gl_.AttachShader(program_, vertexShader_);
        gl_.AttachShader(program_, fragmentShader_);
        gl_.LinkProgram(program_);
        GLint linked = GL_FALSE;
        gl_.GetProgramiv(program_, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            char buffer[2048] {};
            GLsizei length = 0;
            gl_.GetProgramInfoLog(program_, static_cast<GLsizei>(sizeof(buffer) - 1), &length, buffer);
            log(std::string("Renderer v6.0 shader link failed: ") + buffer + "\n");
            return false;
        }
        cameraUniform_ = gl_.GetUniformLocation(program_, "uCameraLocal");
        extinctionUniform_ = gl_.GetUniformLocation(program_, "uExtinction");
        swirlUniform_ = gl_.GetUniformLocation(program_, "uSwirl");
        ageUniform_ = gl_.GetUniformLocation(program_, "uAge");
        seedUniform_ = gl_.GetUniformLocation(program_, "uSeed");
        gpuReady_ = cameraUniform_ >= 0 && extinctionUniform_ >= 0 && swirlUniform_ >= 0 &&
                    ageUniform_ >= 0 && seedUniform_ >= 0;
        if (gpuReady_) log("Renderer v6.0 GPU ray-march shaders ready.\n");
        return gpuReady_;
    }

    static int drawCallback(XPLMDrawingPhase, int, void* refcon) {
        return static_cast<ContrailVolumeRenderer*>(refcon)->draw();
    }

    int draw() {
        if (!enabled_ || !running_ || cells_.empty()) return 1;
        if (XPLMGetDatai(worldRenderTypeRef_) != 0) return 1;
        if (!ensureGpuReady()) return 1;

        XPLMCameraPosition_t camera {};
        XPLMReadCameraPosition(&camera);
        const engine::Vec3d cameraPosition {camera.x, camera.y, camera.z};

        std::vector<const VolumeCell*> ordered;
        ordered.reserve(cells_.size());
        for (const auto& cell : cells_) ordered.push_back(&cell);
        std::stable_sort(ordered.begin(), ordered.end(), [&](const VolumeCell* a, const VolumeCell* b) {
            return distanceM(a->centre, cameraPosition) > distanceM(b->centre, cameraPosition);
        });

        XPLMSetGraphicsState(0, 0, 0, 0, 1, 1, 0);
        gl_.UseProgram(program_);
        for (const auto* cell : ordered) drawCell(*cell, cameraPosition);
        gl_.UseProgram(0);
        renderedPerAsset_.fill(0);
        renderedPerAsset_[0] = ordered.size();
        return 1;
    }

    void drawCell(const VolumeCell& cell, const engine::Vec3d& cameraPosition) {
        const engine::Vec3d tangent = normalize(cell.tangent);
        engine::Vec3d side = cross({0.0, 1.0, 0.0}, tangent);
        if (std::sqrt(dot(side, side)) < 1.0e-5) side = {1.0, 0.0, 0.0};
        side = normalize(side);
        const engine::Vec3d up = normalize(cross(tangent, side));

        const engine::Vec3d delta {
            cameraPosition.x - cell.centre.x,
            cameraPosition.y - cell.centre.y,
            cameraPosition.z - cell.centre.z
        };
        const float cameraLocalX = static_cast<float>(dot(delta, tangent) / cell.halfLengthM);
        const float cameraLocalY = static_cast<float>(dot(delta, up) / cell.radiusM);
        const float cameraLocalZ = static_cast<float>(dot(delta, side) / cell.radiusM);

        gl_.Uniform3f(cameraUniform_, cameraLocalX, cameraLocalY, cameraLocalZ);
        gl_.Uniform1f(extinctionUniform_, cell.extinction);
        gl_.Uniform1f(swirlUniform_, cell.swirl);
        gl_.Uniform1f(ageUniform_, cell.ageSeconds);
        gl_.Uniform1f(seedUniform_, static_cast<float>(cell.seed & 0xffffu) / 65535.0f);

        GLdouble matrix[16] = {
            tangent.x * cell.halfLengthM, tangent.y * cell.halfLengthM, tangent.z * cell.halfLengthM, 0.0,
            up.x * cell.radiusM, up.y * cell.radiusM, up.z * cell.radiusM, 0.0,
            side.x * cell.radiusM, side.y * cell.radiusM, side.z * cell.radiusM, 0.0,
            cell.centre.x, cell.centre.y, cell.centre.z, 1.0
        };
        glPushMatrix();
        glMultMatrixd(matrix);
        drawUnitCube();
        glPopMatrix();
    }

    static void emitVertex(float x, float y, float z) {
        glTexCoord3f(x, y, z);
        glVertex3f(x, y, z);
    }

    static void drawUnitCube() {
        glBegin(GL_QUADS);
        // +X
        emitVertex( 1,-1,-1); emitVertex( 1, 1,-1); emitVertex( 1, 1, 1); emitVertex( 1,-1, 1);
        // -X
        emitVertex(-1,-1, 1); emitVertex(-1, 1, 1); emitVertex(-1, 1,-1); emitVertex(-1,-1,-1);
        // +Y
        emitVertex(-1, 1,-1); emitVertex(-1, 1, 1); emitVertex( 1, 1, 1); emitVertex( 1, 1,-1);
        // -Y
        emitVertex(-1,-1, 1); emitVertex(-1,-1,-1); emitVertex( 1,-1,-1); emitVertex( 1,-1, 1);
        // +Z
        emitVertex(-1,-1, 1); emitVertex( 1,-1, 1); emitVertex( 1, 1, 1); emitVertex(-1, 1, 1);
        // -Z
        emitVertex( 1,-1,-1); emitVertex(-1,-1,-1); emitVertex(-1, 1,-1); emitVertex( 1, 1,-1);
        glEnd();
    }
#endif

    std::vector<VolumeCell> cells_;
    DiagnosticCounts selectedPerAsset_ {};
    DiagnosticCounts renderedPerAsset_ {};
    XPLMDataRef worldRenderTypeRef_ = nullptr;
    XPLMDataRef reverseZRef_ = nullptr;
    XPLMDataRef reverseYRef_ = nullptr;
    std::size_t visibleCellCount_ = 0;
    std::size_t swirlCandidateCount_ = 0;
    double maximumWakeTurnDeg_ = 0.0;
    double maximumWakeDescentM_ = 0.0;
    bool running_ = false;
    bool enabled_ = true;
    bool drawCallbackRegistered_ = false;
#if IBM
    GlFunctions gl_ {};
    GLuint vertexShader_ = 0;
    GLuint fragmentShader_ = 0;
    GLuint program_ = 0;
    GLint cameraUniform_ = -1;
    GLint extinctionUniform_ = -1;
    GLint swirlUniform_ = -1;
    GLint ageUniform_ = -1;
    GLint seedUniform_ = -1;
    bool gpuReady_ = false;
#else
    bool gpuReady_ = false;
#endif
};

}  // namespace ffatmo
