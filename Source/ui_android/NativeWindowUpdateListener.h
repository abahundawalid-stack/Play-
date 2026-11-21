#pragma once

#include <android/native_window.h>

/**
 * @brief Receives native-window updates for Android GS backends.
 *
 * This interface defines the ownership and threading contract between
 * Android JNI lifecycle code and Android GS renderer implementations.
 *
 * OWNERSHIP CONTRACT
 * ------------------
 *
 * 1. Every non-null ANativeWindow* passed to SetWindow() represents
 *    exactly one acquired reference.
 *
 * 2. Ownership of that reference is transferred to the listener
 *    when SetWindow() is called.
 *
 * 3. The caller MUST NOT release the transferred reference after
 *    calling SetWindow().
 *
 * 4. The implementation MUST consume the transferred reference
 *    exactly once:
 *
 *       - retain it while the renderer uses the window;
 *       - release it when replacing or detaching the window;
 *       - release it when the listener is destroyed.
 *
 * 5. Passing nullptr requests detachment of the currently retained
 *    window. nullptr does not represent an acquired reference.
 *
 * 6. Passing the same pointer again does not cancel the ownership
 *    transfer. The new call still carries a separate reference.
 *    The implementation MUST release that additional reference
 *    while preserving its previously retained reference.
 *
 * 7. Ownership must remain valid while renderer work is queued.
 *    Implementations must not capture an unowned ANativeWindow*
 *    in deferred work.
 *
 * THREADING CONTRACT
 * ------------------
 *
 * 1. SetWindow() may be called from the Android UI thread or another
 *    thread outside the GS rendering thread.
 *
 * 2. Renderer, EGL, Vulkan surface, and window-state operations
 *    MUST be serialized on the appropriate GS execution thread.
 *
 * 3. Implementations are responsible for preserving the listener
 *    object's lifetime while deferred GS work is pending.
 *
 * 4. This interface does not promise that renderer processing has
 *    completed when SetWindow() returns.
 *
 * 5. Implementations requiring completion before returning must
 *    explicitly request and enforce synchronous GS dispatch.
 *
 * LIFECYCLE CONTRACT
 * ------------------
 *
 * 1. A window replacement must stop using the previous window
 *    before releasing its retained reference.
 *
 * 2. A detach operation must stop presentation and release the
 *    renderer's native surface before releasing the retained window.
 *
 * 3. Renderer destruction must release all resources associated
 *    with the retained window before releasing that window.
 *
 * ANDROID REFERENCE CONTRACT
 * --------------------------
 *
 * ANativeWindow_fromSurface() returns an acquired reference.
 * Every acquired reference must eventually be released with
 * ANativeWindow_release().
 */
class INativeWindowUpdateListener
{
public:
    virtual ~INativeWindowUpdateListener() noexcept = default;

    /**
     * @brief Replaces, updates, or detaches the Android native window.
     *
     * @param window One transferred ANativeWindow reference, or
     *               nullptr to detach the current window.
     *
     * @note The implementation assumes ownership of the supplied
     *       reference. The caller must not release it afterward.
     *
     * @note Renderer operations must be serialized by the
     *       implementation on the appropriate GS execution thread.
     */
    virtual void SetWindow(ANativeWindow* window) = 0;
};
