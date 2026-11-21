#pragma once

#include <EGL/egl.h>

/**
 * @brief Centralized EGLContext lifecycle operations.
 *
 * Responsibilities:
 * - Bind the OpenGL ES client API.
 * - Create an EGL context with an optional shared context.
 * - Make a context current on draw/read surfaces.
 * - Safely detach the current context.
 * - Destroy a context without silently discarding a failed handle.
 *
 * Ownership:
 * - This class does not own EGLDisplay, EGLConfig or EGLSurface.
 * - The caller owns the EGLContext handle returned by Create().
 * - Every successful context creation must be paired with Destroy().
 *
 * Threading:
 * - All operations for a given context must be serialized.
 * - MakeCurrent(), ClearCurrent() and Destroy() must run on the
 *   renderer's EGL/GS execution thread.
 * - EGL current-context state is thread-local.
 */
class CEglContextManager final
{
public:
    CEglContextManager() = delete;
    ~CEglContextManager() = delete;

    CEglContextManager(const CEglContextManager&) = delete;
    CEglContextManager& operator=(const CEglContextManager&) = delete;

    /**
     * @brief Create an OpenGL ES EGL context.
     *
     * @param display Initialized EGL display.
     * @param config Selected EGL configuration.
     * @param clientVersion Requested OpenGL ES version (2 or 3).
     * @param sharedContext Optional context to share resources with.
     * @param context Output context. Set to EGL_NO_CONTEXT on failure.
     * @param error Optional EGL error output.
     */
    static bool Create(
        EGLDisplay display,
        EGLConfig config,
        EGLint clientVersion,
        EGLContext sharedContext,
        EGLContext* context,
        EGLint* error = nullptr);

    /**
     * @brief Make an EGL context current on the calling thread.
     */
    static bool MakeCurrent(
        EGLDisplay display,
        EGLSurface drawSurface,
        EGLSurface readSurface,
        EGLContext context,
        EGLint* error = nullptr);

    /**
     * @brief Detach the current EGL context from the calling thread.
     */
    static bool ClearCurrent(
        EGLDisplay display,
        EGLint* error = nullptr);

    /**
     * @brief Destroy an EGL context.
     *
     * If the context is current on the calling thread, it is
     * detached first. The handle is cleared only after successful
     * destruction.
     */
    static bool Destroy(
        EGLDisplay display,
        EGLContext* context,
        EGLint* error = nullptr);

    /**
     * @brief Check whether the supplied context is current
     *        on the calling thread.
     */
    static bool IsCurrent(EGLContext context);

    /**
     * @brief Return the context current on the calling thread.
     */
    static EGLContext GetCurrent();
};
