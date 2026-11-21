#pragma once

#include <EGL/egl.h>

/**
 * @brief Process-wide reference-counted EGL display manager.
 *
 * All Android OpenGL renderers that use EGL_DEFAULT_DISPLAY
 * must acquire and release the display through this class.
 *
 * Thread safety:
 * - Acquire() and Release() are serialized internally.
 * - The manager protects display initialization/termination.
 * - Context and surface operations remain the responsibility
 *   of the renderer's GS execution thread.
 *
 * Ownership:
 * - Every successful Acquire() creates exactly one lease.
 * - Every lease must be matched by exactly one Release().
 * - eglTerminate() is called only after the final lease is released.
 */
class CEglDisplayManager final
{
public:
    CEglDisplayManager() = delete;
    ~CEglDisplayManager() = delete;

    CEglDisplayManager(const CEglDisplayManager&) = delete;
    CEglDisplayManager& operator=(const CEglDisplayManager&) = delete;

    /**
     * @brief Acquire a shared initialized EGL display.
     *
     * @param display Output EGL display. Set to EGL_NO_DISPLAY on failure.
     * @param major Output EGL major version. Optional.
     * @param minor Output EGL minor version. Optional.
     * @param error Output EGL error. Optional.
     *
     * @return true when a display lease was successfully acquired.
     */
    static bool Acquire(
        EGLDisplay* display,
        EGLint* major = nullptr,
        EGLint* minor = nullptr,
        EGLint* error = nullptr);

    /**
     * @brief Release one previously acquired display lease.
     *
     * The supplied display must be the exact display returned
     * by the corresponding successful Acquire().
     *
     * @param display EGL display to release.
     * @param error Output EGL error. Optional.
     *
     * @return true when the lease was released successfully.
     */
    static bool Release(
        EGLDisplay display,
        EGLint* error = nullptr);

    /**
     * @brief Return the current number of active display leases.
     *
     * Intended for diagnostics and tests.
     */
    static unsigned int GetReferenceCount();

    /**
     * @brief Return the currently managed EGL display.
     *
     * Returns EGL_NO_DISPLAY when no lease is active.
     */
    static EGLDisplay GetDisplay();
};
