#include "ui/entity_hitbox_overlay.hpp"
#include "ui/chest_esp.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dobby {
namespace {

constexpr float kNearPlane = 0.01F;
constexpr std::size_t kMaximumBoxesPerFrame = 512;

Vec3f subtract(const Vec3f& left, const Vec3f& right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

bool finite(const Vec3f& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool validBounds(const EntityAabb& bounds) {
    if (!finite(bounds.minimum) || !finite(bounds.maximum))
        return false;
    const Vec3f extent = subtract(bounds.maximum, bounds.minimum);
    return extent.x > 0.0001F && extent.y > 0.0001F && extent.z > 0.0001F &&
            extent.x <= 1024.0F && extent.y <= 1024.0F && extent.z <= 1024.0F;
}

bool validCamera(const CameraFrame& camera) {
    if (!finite(camera.position))
        return false;
    const auto finiteMatrix = [](const std::array<float, 16>& matrix) {
        return std::all_of(matrix.begin(), matrix.end(), [](float value) {
            return std::isfinite(value) && std::fabs(value) < 1.0e8F;
        });
    };
    return finiteMatrix(camera.view) && finiteMatrix(camera.projection) &&
            std::fabs(camera.projection[0]) > 0.0001F &&
            std::fabs(camera.projection[5]) > 0.0001F;
}

std::array<float, 4> transform(
        const std::array<float, 16>& matrix, const std::array<float, 4>& point) {
    return {{
            point[0] * matrix[0] + point[1] * matrix[4] +
                    point[2] * matrix[8] + point[3] * matrix[12],
            point[0] * matrix[1] + point[1] * matrix[5] +
                    point[2] * matrix[9] + point[3] * matrix[13],
            point[0] * matrix[2] + point[1] * matrix[6] +
                    point[2] * matrix[10] + point[3] * matrix[14],
            point[0] * matrix[3] + point[1] * matrix[7] +
                    point[2] * matrix[11] + point[3] * matrix[15],
    }};
}

struct CapturedBox {
    EntityAabb bounds;
    CameraFrame camera;
    std::uint64_t lastSeenFrame{};
};

std::mutex captureMutex;
std::unordered_map<std::uintptr_t, CapturedBox> capturedBoxes;
std::uintptr_t capturedLevel{};
CameraFrame latestCamera{};
std::uintptr_t latestCameraLevel{};
std::uint64_t latestCameraFrame{};
std::atomic_uint64_t presentationFrame{};

} // namespace

bool validCameraFrame(const CameraFrame& camera) {
    return validCamera(camera);
}

bool validEntityBounds(const EntityAabb& bounds) {
    return validBounds(bounds);
}

bool projectWorldPoint(
        const CameraFrame& camera, const Vec3f& world, float width, float height,
        ScreenPoint& output) {
    if (!validCamera(camera) || !finite(world) || !std::isfinite(width) ||
        !std::isfinite(height) || width <= 0.0F || height <= 0.0F) {
        return false;
    }

    // Bedrock renders world geometry relative to the camera origin. Apply the
    // exact view and projection matrices retained by that same camera. This is
    // the world-to-screen path used by Horion, without reconstructing FOV or
    // camera axes independently.
    const Vec3f relative = subtract(world, camera.position);
    const auto eye = transform(camera.view, {{relative.x, relative.y, relative.z, 1.0F}});
    const auto clip = transform(camera.projection, eye);
    if (!std::isfinite(clip[3]) || clip[3] <= kNearPlane)
        return false;
    const float normalizedX = clip[0] / clip[3];
    const float normalizedY = clip[1] / clip[3];
    if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY))
        return false;

    output.x = (normalizedX + 1.0F) * width * 0.5F;
    output.y = (1.0F - normalizedY) * height * 0.5F;
    return std::isfinite(output.x) && std::isfinite(output.y);
}

HitboxSubmissionResult submitEntityHitbox(
        const void* entityIdentity, const void* levelIdentity,
        const EntityAabb& bounds, const CameraFrame& camera) {
    if (entityIdentity == nullptr || levelIdentity == nullptr)
        return HitboxSubmissionResult::invalidBounds;
    if (!validBounds(bounds))
        return HitboxSubmissionResult::invalidBounds;
    if (!validCamera(camera))
        return HitboxSubmissionResult::invalidCamera;
    std::lock_guard lock(captureMutex);
    const auto level = reinterpret_cast<std::uintptr_t>(levelIdentity);
    if (capturedLevel != level) {
        capturedBoxes.clear();
        capturedLevel = level;
    }
    const auto entity = reinterpret_cast<std::uintptr_t>(entityIdentity);
    const auto existing = capturedBoxes.find(entity);
    if (existing == capturedBoxes.end() &&
        capturedBoxes.size() >= kMaximumBoxesPerFrame) {
        return HitboxSubmissionResult::frameCapacityReached;
    }
    capturedBoxes[entity] = {
            bounds, camera, presentationFrame.load(std::memory_order_relaxed)};
    return HitboxSubmissionResult::accepted;
}

void submitOverlayCamera(
        const void* levelIdentity, const CameraFrame& camera) {
    if (levelIdentity == nullptr || !validCamera(camera))
        return;
    std::lock_guard lock(captureMutex);
    latestCamera = camera;
    latestCameraLevel = reinterpret_cast<std::uintptr_t>(levelIdentity);
    latestCameraFrame = presentationFrame.load(std::memory_order_relaxed);
}

bool currentOverlayCamera(
        std::uint64_t currentFrame, const void*& levelIdentity,
        CameraFrame& camera) {
    std::lock_guard lock(captureMutex);
    if (latestCameraLevel == 0 ||
        !entityHitboxObservedForPresentation(currentFrame, latestCameraFrame)) {
        return false;
    }
    levelIdentity = reinterpret_cast<const void*>(latestCameraLevel);
    camera = latestCamera;
    return true;
}

std::uint64_t entityHitboxPresentationFrame() {
    return presentationFrame.load(std::memory_order_acquire);
}

bool entityHitboxObservedForPresentation(
        std::uint64_t currentFrame, std::uint64_t lastSeenFrame) {
    return currentFrame >= lastSeenFrame && currentFrame - lastSeenFrame <= 1;
}

} // namespace dobby

#if defined(__ANDROID__)

#include "core/runtime_state.hpp"
#include "metrics/network_metrics.hpp"
#include "platform/launcher.hpp"
#include "platform/log.hpp"
#include "ui/network_metrics_overlay.hpp"

#include <GLES2/gl2.h>

#include <atomic>
#include <cstdio>
#include <dlfcn.h>
#include <limits>

namespace dobby {
namespace {

struct GlApi {
    PFNGLCREATESHADERPROC createShader{};
    PFNGLSHADERSOURCEPROC shaderSource{};
    PFNGLCOMPILESHADERPROC compileShader{};
    PFNGLGETSHADERIVPROC getShaderiv{};
    PFNGLGETSHADERINFOLOGPROC getShaderInfoLog{};
    PFNGLDELETESHADERPROC deleteShader{};
    PFNGLCREATEPROGRAMPROC createProgram{};
    PFNGLATTACHSHADERPROC attachShader{};
    PFNGLBINDATTRIBLOCATIONPROC bindAttribLocation{};
    PFNGLLINKPROGRAMPROC linkProgram{};
    PFNGLGETPROGRAMIVPROC getProgramiv{};
    PFNGLGETPROGRAMINFOLOGPROC getProgramInfoLog{};
    PFNGLDELETEPROGRAMPROC deleteProgram{};
    PFNGLGENBUFFERSPROC genBuffers{};
    PFNGLBINDBUFFERPROC bindBuffer{};
    PFNGLBUFFERDATAPROC bufferData{};
    PFNGLENABLEVERTEXATTRIBARRAYPROC enableVertexAttribArray{};
    PFNGLDISABLEVERTEXATTRIBARRAYPROC disableVertexAttribArray{};
    PFNGLGETVERTEXATTRIBIVPROC getVertexAttribiv{};
    PFNGLVERTEXATTRIBPOINTERPROC vertexAttribPointer{};
    PFNGLUSEPROGRAMPROC useProgram{};
    PFNGLGETUNIFORMLOCATIONPROC getUniformLocation{};
    PFNGLUNIFORM4FPROC uniform4f{};
    PFNGLGETINTEGERVPROC getIntegerv{};
    PFNGLGETFLOATVPROC getFloatv{};
    PFNGLISENABLEDPROC isEnabled{};
    PFNGLENABLEPROC enable{};
    PFNGLDISABLEPROC disable{};
    PFNGLBLENDFUNCSEPARATEPROC blendFuncSeparate{};
    PFNGLLINEWIDTHPROC lineWidth{};
    PFNGLDRAWARRAYSPROC drawArrays{};
    PFNGLBINDFRAMEBUFFERPROC bindFramebuffer{};
    PFNGLCHECKFRAMEBUFFERSTATUSPROC checkFramebufferStatus{};
    PFNGLVIEWPORTPROC viewport{};
    PFNGLGETERRORPROC getError{};
    PFNGLGETSTRINGPROC getString{};
};

GlApi gl;
GLuint program{};
GLuint vertexBuffer{};
GLint colorUniform{-1};
std::atomic_bool overlayInstalled{false};
std::atomic_bool failedProjectionLogged{false};
std::atomic_bool successfulProjectionLogged{false};
std::atomic_bool presentationSampleLogged{false};
std::atomic_bool rendererFailureLogged{false};
std::atomic_bool chestPresentationLogged{false};
std::atomic_bool chestLevelMismatchLogged{false};
std::size_t largestBatchLogged = 0;
std::size_t batchSamplesLogged = 0;

template <class Function>
bool resolveGlFunction(void* library, Function& output, const char* name) {
    void* symbol = library == nullptr ? nullptr : dlsym(library, name);
    output = reinterpret_cast<Function>(symbol);
    if (output == nullptr) {
        char message[160]{};
        std::snprintf(message, sizeof(message),
                      "ERROR: entity overlay missing GLES2 function %s", name);
        logLine(message);
    }
    return output != nullptr;
}

bool resolveGlApi() {
    void* library = dlopen("libGLESv2.so", RTLD_NOW | RTLD_NOLOAD);
    if (library == nullptr)
        library = dlopen("libGLESv2.so", RTLD_NOW);
    return resolveGlFunction(library, gl.createShader, "glCreateShader") &&
            resolveGlFunction(library, gl.shaderSource, "glShaderSource") &&
            resolveGlFunction(library, gl.compileShader, "glCompileShader") &&
            resolveGlFunction(library, gl.getShaderiv, "glGetShaderiv") &&
            resolveGlFunction(library, gl.getShaderInfoLog, "glGetShaderInfoLog") &&
            resolveGlFunction(library, gl.deleteShader, "glDeleteShader") &&
            resolveGlFunction(library, gl.createProgram, "glCreateProgram") &&
            resolveGlFunction(library, gl.attachShader, "glAttachShader") &&
            resolveGlFunction(library, gl.bindAttribLocation, "glBindAttribLocation") &&
            resolveGlFunction(library, gl.linkProgram, "glLinkProgram") &&
            resolveGlFunction(library, gl.getProgramiv, "glGetProgramiv") &&
            resolveGlFunction(library, gl.getProgramInfoLog, "glGetProgramInfoLog") &&
            resolveGlFunction(library, gl.deleteProgram, "glDeleteProgram") &&
            resolveGlFunction(library, gl.genBuffers, "glGenBuffers") &&
            resolveGlFunction(library, gl.bindBuffer, "glBindBuffer") &&
            resolveGlFunction(library, gl.bufferData, "glBufferData") &&
            resolveGlFunction(library, gl.enableVertexAttribArray, "glEnableVertexAttribArray") &&
            resolveGlFunction(library, gl.disableVertexAttribArray, "glDisableVertexAttribArray") &&
            resolveGlFunction(library, gl.getVertexAttribiv, "glGetVertexAttribiv") &&
            resolveGlFunction(library, gl.vertexAttribPointer, "glVertexAttribPointer") &&
            resolveGlFunction(library, gl.useProgram, "glUseProgram") &&
            resolveGlFunction(library, gl.getUniformLocation, "glGetUniformLocation") &&
            resolveGlFunction(library, gl.uniform4f, "glUniform4f") &&
            resolveGlFunction(library, gl.getIntegerv, "glGetIntegerv") &&
            resolveGlFunction(library, gl.getFloatv, "glGetFloatv") &&
            resolveGlFunction(library, gl.isEnabled, "glIsEnabled") &&
            resolveGlFunction(library, gl.enable, "glEnable") &&
            resolveGlFunction(library, gl.disable, "glDisable") &&
            resolveGlFunction(library, gl.blendFuncSeparate, "glBlendFuncSeparate") &&
            resolveGlFunction(library, gl.lineWidth, "glLineWidth") &&
            resolveGlFunction(library, gl.drawArrays, "glDrawArrays") &&
            resolveGlFunction(library, gl.bindFramebuffer, "glBindFramebuffer") &&
            resolveGlFunction(library, gl.checkFramebufferStatus, "glCheckFramebufferStatus") &&
            resolveGlFunction(library, gl.viewport, "glViewport") &&
            resolveGlFunction(library, gl.getError, "glGetError") &&
            resolveGlFunction(library, gl.getString, "glGetString");
}

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = gl.createShader(type);
    if (shader == 0)
        return 0;
    gl.shaderSource(shader, 1, &source, nullptr);
    gl.compileShader(shader);
    GLint compiled = GL_FALSE;
    gl.getShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE)
        return shader;
    char details[512]{};
    GLsizei length = 0;
    gl.getShaderInfoLog(shader, static_cast<GLsizei>(sizeof(details) - 1), &length, details);
    char message[640]{};
    std::snprintf(message, sizeof(message),
                  "ERROR: entity overlay shader compile failed: %s", details);
    logLine(message);
    gl.deleteShader(shader);
    return 0;
}

bool createRenderer() {
    constexpr char vertexSource[] =
            "#version 100\n"
            "attribute vec2 position;\n"
            "void main(){ gl_Position=vec4(position,0.0,1.0); }\n";
    constexpr char fragmentSource[] =
            "#version 100\n"
            "precision mediump float;\n"
            "uniform vec4 color;\n"
            "void main(){ gl_FragColor=color; }\n";
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0)
            gl.deleteShader(vertex);
        if (fragment != 0)
            gl.deleteShader(fragment);
        return false;
    }
    program = gl.createProgram();
    gl.attachShader(program, vertex);
    gl.attachShader(program, fragment);
    gl.bindAttribLocation(program, 0, "position");
    gl.linkProgram(program);
    gl.deleteShader(vertex);
    gl.deleteShader(fragment);
    GLint linked = GL_FALSE;
    gl.getProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char details[512]{};
        GLsizei length = 0;
        gl.getProgramInfoLog(
                program, static_cast<GLsizei>(sizeof(details) - 1), &length, details);
        char message[640]{};
        std::snprintf(message, sizeof(message),
                      "ERROR: entity overlay program link failed: %s", details);
        logLine(message);
        gl.deleteProgram(program);
        program = 0;
        return false;
    }
    colorUniform = gl.getUniformLocation(program, "color");
    gl.genBuffers(1, &vertexBuffer);
    return colorUniform >= 0 && vertexBuffer != 0;
}

std::array<Vec3f, 8> corners(const EntityAabb& bounds) {
    return {{
            {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z},
            {bounds.maximum.x, bounds.minimum.y, bounds.minimum.z},
            {bounds.maximum.x, bounds.maximum.y, bounds.minimum.z},
            {bounds.minimum.x, bounds.maximum.y, bounds.minimum.z},
            {bounds.minimum.x, bounds.minimum.y, bounds.maximum.z},
            {bounds.maximum.x, bounds.minimum.y, bounds.maximum.z},
            {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z},
            {bounds.minimum.x, bounds.maximum.y, bounds.maximum.z},
    }};
}

void restoreCapability(GLenum capability, bool enabled) {
    if (enabled)
        gl.enable(capability);
    else
        gl.disable(capability);
}

void clearGlErrors() {
    for (int count = 0; count < 16 && gl.getError() != GL_NO_ERROR; ++count) {
    }
}

void drawLines(const float* vertices, std::size_t floatCount,
               float red, float green, float blue, float alpha, float width) {
    if (vertices == nullptr || floatCount < 4)
        return;
    gl.bufferData(
            GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(floatCount * sizeof(float)),
            vertices, GL_STREAM_DRAW);
    gl.uniform4f(colorUniform, red, green, blue, alpha);
    gl.lineWidth(width);
    gl.drawArrays(GL_LINES, 0, static_cast<GLsizei>(floatCount / 2));
}

void drawEntityHitboxes(void*, void* display, void* surface) {
    const bool showHitboxes = runtimeState().entityHitboxes();
    const bool showChests = runtimeState().chestEsp();
    const NetworkMetricsSnapshot metrics = runtimeState().networkMetricsOverlay()
            ? currentNetworkMetrics() : NetworkMetricsSnapshot{};
    std::vector<CapturedBox> boxes;
    const std::uint64_t currentFrame =
            presentationFrame.fetch_add(1, std::memory_order_acq_rel) + 1;
    {
        std::lock_guard lock(captureMutex);
        for (auto entry = capturedBoxes.begin(); entry != capturedBoxes.end();) {
            if (!entityHitboxObservedForPresentation(
                        currentFrame, entry->second.lastSeenFrame)) {
                entry = capturedBoxes.erase(entry);
            } else {
                boxes.push_back(entry->second);
                ++entry;
            }
        }
    }
    if (!showHitboxes)
        boxes.clear();
    std::vector<ChestEspObservation> chests;
    const void* cameraLevel = nullptr;
    CameraFrame chestCamera{};
    if (showChests && currentOverlayCamera(
                              currentFrame, cameraLevel, chestCamera)) {
        chests = snapshotClientKnownChests(cameraLevel, chestCamera);
        if (!chests.empty() && !chestPresentationLogged.exchange(
                                       true, std::memory_order_acq_rel)) {
            char message[128]{};
            std::snprintf(
                    message, sizeof(message),
                    "chest ESP presentation: %zu client-known chest(s)",
                    chests.size());
            logLine(message);
        } else if (chests.empty() && clientKnownChestCount() != 0 &&
                   !chestLevelMismatchLogged.exchange(
                           true, std::memory_order_acq_rel)) {
            char message[192]{};
            std::snprintf(
                    message, sizeof(message),
                    "ERROR: chest ESP level mismatch: registry=%zu "
                    "camera_level=%zu",
                    clientKnownChestCount(),
                    clientKnownChestCountForLevel(cameraLevel));
            logLine(message);
        }
    }
    if (!overlayInstalled.load(std::memory_order_acquire)) {
        return;
    }
    if (program == 0 && !createRenderer()) {
        if (!rendererFailureLogged.exchange(true, std::memory_order_acq_rel))
            logLine("ERROR: entity overlay shader setup failed");
        return;
    }

    GLint viewport[4]{};
    gl.getIntegerv(GL_VIEWPORT, viewport);
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    const bool hasSurfaceSize = launcherSurfaceSize(
            display, surface, surfaceWidth, surfaceHeight);
    const float width = static_cast<float>(
            hasSurfaceSize ? surfaceWidth : viewport[2]);
    const float height = static_cast<float>(
            hasSurfaceSize ? surfaceHeight : viewport[3]);
    if (width <= 0.0F || height <= 0.0F)
        return;
    const NetworkMetricsGeometry metricsGeometry =
            buildNetworkMetricsGeometry(metrics, width, height);
    if (boxes.empty() && chests.empty() && metricsGeometry.shadowVertices.empty())
        return;

    if (boxes.size() > largestBatchLogged && batchSamplesLogged < 8) {
        largestBatchLogged = boxes.size();
        ++batchSamplesLogged;
        const CapturedBox* largest = nullptr;
        float largestExtent = 0.0F;
        for (const auto& box : boxes) {
            const Vec3f extent = subtract(box.bounds.maximum, box.bounds.minimum);
            const float candidate = std::max({extent.x, extent.y, extent.z});
            if (candidate > largestExtent) {
                largestExtent = candidate;
                largest = &box;
            }
        }
        char message[384]{};
        if (largest != nullptr) {
            const Vec3f extent = subtract(largest->bounds.maximum, largest->bounds.minimum);
            std::snprintf(
                    message, sizeof(message),
                    "entity frame sample: actors=%zu largest=(%.3f,%.3f,%.3f) "
                    "bounds=(%.3f,%.3f,%.3f)->(%.3f,%.3f,%.3f) origin=(%.3f,%.3f,%.3f)",
                    boxes.size(), extent.x, extent.y, extent.z,
                    largest->bounds.minimum.x, largest->bounds.minimum.y,
                    largest->bounds.minimum.z, largest->bounds.maximum.x,
                    largest->bounds.maximum.y, largest->bounds.maximum.z,
                    largest->camera.position.x, largest->camera.position.y,
                    largest->camera.position.z);
        } else {
            std::snprintf(message, sizeof(message),
                          "entity frame sample: actors=%zu", boxes.size());
        }
        logLine(message);
    }

    constexpr std::array<std::array<std::size_t, 2>, 12> edges{{
            {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
            {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
            {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
    }};
    std::vector<float> vertices;
    vertices.reserve(boxes.size() * edges.size() * 4);
    std::vector<float> chestVertices;
    chestVertices.reserve(chests.size() * edges.size() * 4);
    std::vector<float> locatorVertices;
    locatorVertices.reserve(boxes.size() * 24);
    const auto appendScreenLine = [&](float x1, float y1, float x2, float y2) {
        locatorVertices.push_back(x1 / width * 2.0F - 1.0F);
        locatorVertices.push_back(1.0F - y1 / height * 2.0F);
        locatorVertices.push_back(x2 / width * 2.0F - 1.0F);
        locatorVertices.push_back(1.0F - y2 / height * 2.0F);
    };
    std::size_t projectedCorners = 0;
    std::size_t locatorCount = 0;
    float minimumScreenX = std::numeric_limits<float>::infinity();
    float maximumScreenX = -std::numeric_limits<float>::infinity();
    float minimumScreenY = std::numeric_limits<float>::infinity();
    float maximumScreenY = -std::numeric_limits<float>::infinity();
    for (const auto& box : boxes) {
        const auto worldCorners = corners(box.bounds);
        std::array<ScreenPoint, 8> screenCorners{};
        std::array<bool, 8> visible{};
        float boxMinimumX = std::numeric_limits<float>::infinity();
        float boxMaximumX = -std::numeric_limits<float>::infinity();
        float boxMinimumY = std::numeric_limits<float>::infinity();
        float boxMaximumY = -std::numeric_limits<float>::infinity();
        for (std::size_t index = 0; index < worldCorners.size(); ++index) {
            visible[index] = projectWorldPoint(
                    box.camera, worldCorners[index], width, height, screenCorners[index]);
            if (visible[index]) {
                ++projectedCorners;
                minimumScreenX = std::min(minimumScreenX, screenCorners[index].x);
                maximumScreenX = std::max(maximumScreenX, screenCorners[index].x);
                minimumScreenY = std::min(minimumScreenY, screenCorners[index].y);
                maximumScreenY = std::max(maximumScreenY, screenCorners[index].y);
                boxMinimumX = std::min(boxMinimumX, screenCorners[index].x);
                boxMaximumX = std::max(boxMaximumX, screenCorners[index].x);
                boxMinimumY = std::min(boxMinimumY, screenCorners[index].y);
                boxMaximumY = std::max(boxMaximumY, screenCorners[index].y);
            }
        }
        for (const auto& edge : edges) {
            if (!visible[edge[0]] || !visible[edge[1]])
                continue;
            for (const std::size_t index : edge) {
                vertices.push_back(screenCorners[index].x / width * 2.0F - 1.0F);
                vertices.push_back(1.0F - screenCorners[index].y / height * 2.0F);
            }
        }
        if (std::isfinite(boxMinimumX) &&
            (boxMaximumX - boxMinimumX < 4.0F || boxMaximumY - boxMinimumY < 4.0F)) {
            const float centerX = (boxMinimumX + boxMaximumX) * 0.5F;
            const float centerY = (boxMinimumY + boxMaximumY) * 0.5F;
            constexpr float halfSquare = 7.0F;
            constexpr float halfCross = 11.0F;
            appendScreenLine(centerX - halfSquare, centerY - halfSquare,
                             centerX + halfSquare, centerY - halfSquare);
            appendScreenLine(centerX + halfSquare, centerY - halfSquare,
                             centerX + halfSquare, centerY + halfSquare);
            appendScreenLine(centerX + halfSquare, centerY + halfSquare,
                             centerX - halfSquare, centerY + halfSquare);
            appendScreenLine(centerX - halfSquare, centerY + halfSquare,
                             centerX - halfSquare, centerY - halfSquare);
            appendScreenLine(centerX - halfCross, centerY,
                             centerX + halfCross, centerY);
            appendScreenLine(centerX, centerY - halfCross,
                             centerX, centerY + halfCross);
            ++locatorCount;
        }
    }
    for (const auto& chest : chests) {
        const auto worldCorners = corners(chest.bounds);
        std::array<ScreenPoint, 8> screenCorners{};
        std::array<bool, 8> visible{};
        for (std::size_t index = 0; index < worldCorners.size(); ++index) {
            visible[index] = projectWorldPoint(
                    chest.camera, worldCorners[index], width, height,
                    screenCorners[index]);
        }
        for (const auto& edge : edges) {
            if (!visible[edge[0]] || !visible[edge[1]])
                continue;
            for (const std::size_t index : edge) {
                chestVertices.push_back(
                        screenCorners[index].x / width * 2.0F - 1.0F);
                chestVertices.push_back(
                        1.0F - screenCorners[index].y / height * 2.0F);
            }
        }
    }
    auto& projectionFlag = vertices.empty()
            ? failedProjectionLogged : successfulProjectionLogged;
    if (!boxes.empty() && !projectionFlag.exchange(true, std::memory_order_acq_rel)) {
        const auto& sample = boxes.front();
        const Vec3f center{
                (sample.bounds.minimum.x + sample.bounds.maximum.x) * 0.5F,
                (sample.bounds.minimum.y + sample.bounds.maximum.y) * 0.5F,
                (sample.bounds.minimum.z + sample.bounds.maximum.z) * 0.5F,
        };
        const Vec3f relativeCenter = subtract(center, sample.camera.position);
        const auto centerEye = transform(
                sample.camera.view,
                {{relativeCenter.x, relativeCenter.y, relativeCenter.z, 1.0F}});
        const auto centerClip = transform(sample.camera.projection, centerEye);
        const float centerDepth = centerClip[3];
        char message[448]{};
        std::snprintf(
                message, sizeof(message),
                "entity projection sample: surface=%.0fx%.0f glViewport=%dx%d boxes=%zu depth=%.3f "
                "corners=%zu screen=(%.1f..%.1f,%.1f..%.1f) vertices=%zu locators=%zu",
                width, height, viewport[2], viewport[3], boxes.size(), centerDepth, projectedCorners,
                minimumScreenX, maximumScreenX, minimumScreenY, maximumScreenY,
                vertices.size() / 2, locatorCount);
        logLine(message);
    }

    GLint oldProgram{};
    GLint oldBuffer{};
    GLint oldFramebuffer{};
    GLint oldPositionAttributeEnabled{};
    GLint oldBlendSourceRgb{};
    GLint oldBlendDestinationRgb{};
    GLint oldBlendSourceAlpha{};
    GLint oldBlendDestinationAlpha{};
    GLfloat oldLineWidth{1.0F};
    gl.getIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    gl.getIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
    gl.getIntegerv(GL_FRAMEBUFFER_BINDING, &oldFramebuffer);
    gl.getVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &oldPositionAttributeEnabled);
    gl.getIntegerv(GL_BLEND_SRC_RGB, &oldBlendSourceRgb);
    gl.getIntegerv(GL_BLEND_DST_RGB, &oldBlendDestinationRgb);
    gl.getIntegerv(GL_BLEND_SRC_ALPHA, &oldBlendSourceAlpha);
    gl.getIntegerv(GL_BLEND_DST_ALPHA, &oldBlendDestinationAlpha);
    gl.getFloatv(GL_LINE_WIDTH, &oldLineWidth);
    const bool depthEnabled = gl.isEnabled(GL_DEPTH_TEST) == GL_TRUE;
    const bool blendEnabled = gl.isEnabled(GL_BLEND) == GL_TRUE;
    const bool cullEnabled = gl.isEnabled(GL_CULL_FACE) == GL_TRUE;
    const bool scissorEnabled = gl.isEnabled(GL_SCISSOR_TEST) == GL_TRUE;

    clearGlErrors();
    gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.viewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    const GLenum framebufferStatus = gl.checkFramebufferStatus(GL_FRAMEBUFFER);
    gl.disable(GL_DEPTH_TEST);
    gl.disable(GL_CULL_FACE);
    gl.disable(GL_SCISSOR_TEST);
    gl.enable(GL_BLEND);
    gl.blendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl.useProgram(program);
    gl.bindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    gl.bufferData(
            GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
            vertices.data(), GL_STREAM_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    if (!vertices.empty()) {
        drawLines(vertices.data(), vertices.size(), 0.0F, 0.0F, 0.0F, 0.85F, 6.0F);
        drawLines(vertices.data(), vertices.size(), 0.25F, 1.0F, 0.25F, 1.0F, 3.0F);
    }
    if (!chestVertices.empty()) {
        drawLines(chestVertices.data(), chestVertices.size(),
                  0.0F, 0.0F, 0.0F, 0.9F, 7.0F);
        drawLines(chestVertices.data(), chestVertices.size(),
                  1.0F, 0.65F, 0.05F, 1.0F, 3.0F);
    }
    if (!locatorVertices.empty()) {
        drawLines(locatorVertices.data(), locatorVertices.size(), 0.0F, 0.0F, 0.0F, 0.9F, 7.0F);
        drawLines(locatorVertices.data(), locatorVertices.size(), 1.0F, 0.2F, 0.65F, 1.0F, 3.0F);
    }
    if (!metricsGeometry.shadowVertices.empty()) {
        drawLines(metricsGeometry.shadowVertices.data(),
                  metricsGeometry.shadowVertices.size(),
                  0.0F, 0.0F, 0.0F, 0.9F,
                  metricsGeometry.lineWidth + 2.0F);
        drawLines(metricsGeometry.pingVertices.data(),
                  metricsGeometry.pingVertices.size(),
                  1.0F, 1.0F, 1.0F, 1.0F,
                  metricsGeometry.lineWidth);
        drawLines(metricsGeometry.tpsVertices.data(),
                  metricsGeometry.tpsVertices.size(),
                  0.35F, 1.0F, 0.45F, 1.0F,
                  metricsGeometry.lineWidth);
        drawLines(metricsGeometry.chunkVertices.data(),
                  metricsGeometry.chunkVertices.size(),
                  0.45F, 0.85F, 1.0F, 1.0F,
                  metricsGeometry.lineWidth);
        drawLines(metricsGeometry.pendingVertices.data(),
                  metricsGeometry.pendingVertices.size(),
                  1.0F, 0.8F, 0.3F, 1.0F,
                  metricsGeometry.lineWidth);
    }
    const GLenum drawError = gl.getError();

    if (!presentationSampleLogged.exchange(true, std::memory_order_acq_rel)) {
        const auto* renderer = reinterpret_cast<const char*>(gl.getString(GL_RENDERER));
        char message[512]{};
        std::snprintf(
                message, sizeof(message),
                "entity presentation sample: surface=%.0fx%.0f viewport=%dx%d "
                "oldFramebuffer=%d defaultStatus=0x%x glError=0x%x renderer=%s",
                width, height, viewport[2], viewport[3], oldFramebuffer,
                static_cast<unsigned int>(framebufferStatus),
                static_cast<unsigned int>(drawError),
                renderer == nullptr ? "unknown" : renderer);
        logLine(message);
    }

    gl.lineWidth(oldLineWidth);
    gl.blendFuncSeparate(
            static_cast<GLenum>(oldBlendSourceRgb), static_cast<GLenum>(oldBlendDestinationRgb),
            static_cast<GLenum>(oldBlendSourceAlpha), static_cast<GLenum>(oldBlendDestinationAlpha));
    restoreCapability(GL_DEPTH_TEST, depthEnabled);
    restoreCapability(GL_BLEND, blendEnabled);
    restoreCapability(GL_CULL_FACE, cullEnabled);
    restoreCapability(GL_SCISSOR_TEST, scissorEnabled);
    if (oldPositionAttributeEnabled == GL_FALSE)
        gl.disableVertexAttribArray(0);
    gl.bindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(oldBuffer));
    gl.useProgram(static_cast<GLuint>(oldProgram));
    gl.bindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(oldFramebuffer));
    gl.viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

} // namespace

bool installEntityHitboxOverlay() {
    if (overlayInstalled.load(std::memory_order_acquire))
        return true;
    if (!resolveGlApi()) {
        logLine("ERROR: developer overlay unavailable; launcher GLES2 API missing");
        return false;
    }
    if (!addLauncherSwapBuffersCallback(nullptr, drawEntityHitboxes)) {
        logLine("ERROR: developer overlay unavailable; launcher render callback missing");
        return false;
    }
    overlayInstalled.store(true, std::memory_order_release);
    logLine("developer overlay: through-wall GLES2 renderer ready");
    return true;
}

} // namespace dobby

#else

namespace dobby {

bool installEntityHitboxOverlay() { return false; }

} // namespace dobby

#endif
