#pragma once

#include <EGL/egl.h>
#include <android/native_window.h>

/**
 * @brief Selects and validates EGL configurations for Android windows.
 *
 * Responsibilities:
 * - Enumerate available EGL configurations.
 * - Require EGL window-surface support.
 * - Require OpenGL ES 3 support.
 * - Require at least 8 bits for each RGB channel.
 * - Prefer a native visual compatible with ANativeWindow.
 * - Prefer RGBA8888, no unnecessary MSAA, and no unnecessary
 *   default-framebuffer depth/stencil allocation.
 * - Apply the selected EGL native visual to the Android window
 *   when its current format does not match.
 *
 * This class does not own EGLDisplay, EGLConfig, or ANativeWindow.
 */
class CEglConfig final
{
public:
    struct SelectionInfo
    {
        EGLint configId = 0;
        EGLint nativeVisualId = 0;

        EGLint redSize = 0;
        EGLint greenSize = 0;
        EGLint blueSize = 0;
        EGLint alphaSize = 0;

        EGLint depthSize = 0;
        EGLint stencilSize = 0;

        EGLint sampleBuffers = 0;
        EGLint samples = 0;

        EGLint bufferSize = 0;
        EGLint caveat = EGL_NONE;

        EGLint nativeWindowFormat = 0;

        long long score = 0;
        EGLint enumeratedConfigs = 0;
        EGLint eligibleConfigs = 0;
    };

    CEglConfig() = delete;
    ~CEglConfig() = delete;

    CEglConfig(const CEglConfig&) = delete;
    CEglConfig& operator=(const CEglConfig&) = delete;

    /**
     * @brief Select the best EGLConfig for an Android native window.
     *
     * The returned EGLConfig is owned by EGLDisplay and must not
     * be freed by the caller.
     */
    static bool ChooseWindowConfig(
        EGLDisplay display,
        ANativeWindow* window,
        EGLConfig* config,
        SelectionInfo* info = nullptr,
        EGLint* error = nullptr);

    /**
     * @brief Make the native window use the visual selected by EGL.
     *
     * No geometry change is performed if the current format already
     * matches the selected visual.
     */
    static bool ApplyNativeVisual(
        ANativeWindow* window,
        EGLint nativeVisualId,
        EGLint* error = nullptr);
};
