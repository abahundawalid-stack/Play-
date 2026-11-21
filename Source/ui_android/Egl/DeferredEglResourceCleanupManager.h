#pragma once

#include <EGL/egl.h>
#include <android/native_window.h>

#include <cstddef>
#include <vector>

/**
 * @brief Process-wide owner of EGL resources whose cleanup failed.
 *
 * Threading contract:
 * - Adopt() only transfers ownership. It performs no EGL calls.
 * - Drain() MUST be called from an Android GS renderer execution
 *   callback, never from the Android UI thread.
 * - The renderer must detach its context before transferring
 *   resources whenever possible.
 *
 * Ownership contract:
 * - Every adopted EGL surface retains its paired ANativeWindow ref.
 * - The EGL context is destroyed only after all surfaces are gone.
 * - The EGL display lease is released only after the context and
 *   all surfaces have been destroyed.
 *
 * Lifetime:
 * - The manager intentionally survives renderer destruction.
 * - Unresolved resources remain owned until a later GS callback
 *   successfully cleans them or the process exits.
 */
class CDeferredEglResourceCleanupManager final
{
public:
    struct SurfaceResource
    {
        EGLSurface surface = EGL_NO_SURFACE;
        ANativeWindow* window = nullptr;
    };

    struct ResourceBundle
    {
        EGLDisplay display = EGL_NO_DISPLAY;
        EGLContext context = EGL_NO_CONTEXT;

        // True only when this bundle owns one lease obtained
        // from CEglDisplayManager::Acquire().
        bool ownsDisplayLease = false;

        std::vector<SurfaceResource> surfaces;
    };

    struct DrainResult
    {
        std::size_t bundlesVisited = 0;
        std::size_t surfacesDestroyed = 0;
        std::size_t contextsDestroyed = 0;
        std::size_t displaysReleased = 0;
        std::size_t bundlesCompleted = 0;
        std::size_t bundlesDeferred = 0;
    };

    CDeferredEglResourceCleanupManager() = delete;
    ~CDeferredEglResourceCleanupManager() = delete;

    CDeferredEglResourceCleanupManager(
        const CDeferredEglResourceCleanupManager&) = delete;

    CDeferredEglResourceCleanupManager& operator=(
        const CDeferredEglResourceCleanupManager&) = delete;

    /**
     * @brief Transfer ownership of unresolved EGL resources.
     *
     * This function does not call EGL and can therefore be used
     * to transfer ownership without performing cleanup from the
     * destructor/UI thread.
     *
     * On success, the caller must clear its local handles.
     * On failure, the caller retains ownership.
     */
    static bool Adopt(ResourceBundle&& bundle);

    /**
     * @brief Retry cleanup of all pending bundles.
     *
     * MUST be called from the GS renderer execution path.
     */
    static DrainResult Drain();

    /**
     * @brief Number of pending resource bundles.
     */
    static std::size_t GetPendingBundleCount();
};
