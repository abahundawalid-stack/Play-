#pragma once

#include <android/native_window.h>

/**
 * @brief Receives native-window updates for Android GS backends.
 *
 * Ownership contract:
 *
 * 1. A non-null window passed to SetWindow() represents exactly one
 *    owned ANativeWindow reference.
 *
 * 2. The caller transfers ownership of that reference to the listener.
 *    The caller must not release or reuse that transferred reference.
 *
 * 3. The listener must consume the transferred reference exactly once:
 *    - retain it while the window is in use, or
 *    - release it when it is no longer needed.
 *
 * 4. Passing nullptr requests detachment of the current native window.
 *    nullptr does not represent an owned reference.
 *
 * 5. If the incoming pointer is identical to the currently retained
 *    pointer, the incoming reference is still a separate reference.
 *    The listener must release that extra reference while preserving
 *    the previously retained reference.
 *
 * Threading contract:
 *
 * 1. SetWindow() may be called outside the GS rendering thread.
 *
 * 2. Implementations are responsible for serializing renderer and
 *    surface operations on the appropriate GS thread.
 *
 * 3. Ownership transfers when SetWindow() accepts the argument.
 *    Actual renderer processing may be deferred by the implementation.
 *
 * 4. This interface does not guarantee that renderer processing has
 *    completed when SetWindow() returns.
 *
 * Android reference source:
 * ANativeWindow_fromSurface() acquires a reference that must eventually
 * be released with ANativeWindow_release().
 */
class INativeWindowUpdateListener
{
public:
    virtual ~INativeWindowUpdateListener() = default;

    /**
     * @brief Updates or detaches the Android native window.
     *
     * @param window One transferred ANativeWindow reference, or nullptr
     *               to detach the current window.
     */
    virtual void SetWindow(ANativeWindow* window) = 0;
};
