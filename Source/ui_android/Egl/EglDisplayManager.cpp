#include "EglDisplayManager.h"

#include <limits>
#include <mutex>

namespace
{
    struct EglDisplayState
    {
        std::mutex mutex;

        EGLDisplay display = EGL_NO_DISPLAY;

        unsigned int referenceCount = 0;

        EGLint majorVersion = 0;
        EGLint minorVersion = 0;
    };

    EglDisplayState& GetEglDisplayState()
    {
        // Function-local static initialization is thread-safe
        // since C++11.
        static EglDisplayState state;
        return state;
    }

    void SetError(EGLint* output, EGLint error)
    {
        if(output != nullptr)
        {
            *output = error;
        }
    }

    void ClearOutputs(
        EGLDisplay* display,
        EGLint* major,
        EGLint* minor,
        EGLint* error)
    {
        if(display != nullptr)
        {
            *display = EGL_NO_DISPLAY;
        }

        if(major != nullptr)
        {
            *major = 0;
        }

        if(minor != nullptr)
        {
            *minor = 0;
        }

        SetError(error, EGL_SUCCESS);
    }
}

bool CEglDisplayManager::Acquire(
    EGLDisplay* display,
    EGLint* major,
    EGLint* minor,
    EGLint* error)
{
    ClearOutputs(display, major, minor, error);

    if(display == nullptr)
    {
        SetError(error, EGL_BAD_PARAMETER);
        return false;
    }

    EglDisplayState& state = GetEglDisplayState();

    std::lock_guard<std::mutex> lock(state.mutex);

    // ---------------------------------------------------------
    // 1. REUSE AN ALREADY INITIALIZED DISPLAY
    // ---------------------------------------------------------
    if(state.referenceCount != 0)
    {
        if(state.display == EGL_NO_DISPLAY)
        {
            // Internal state corruption: references exist,
            // but the shared display handle is invalid.
            state.referenceCount = 0;
            state.majorVersion = 0;
            state.minorVersion = 0;

            SetError(error, EGL_BAD_DISPLAY);
            return false;
        }

        if(state.referenceCount ==
           std::numeric_limits<unsigned int>::max())
        {
            SetError(error, EGL_BAD_ALLOC);
            return false;
        }

        ++state.referenceCount;

        *display = state.display;

        if(major != nullptr)
        {
            *major = state.majorVersion;
        }

        if(minor != nullptr)
        {
            *minor = state.minorVersion;
        }

        return true;
    }

    // ---------------------------------------------------------
    // 2. GET THE DEFAULT ANDROID EGL DISPLAY
    // ---------------------------------------------------------
    EGLDisplay newDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if(newDisplay == EGL_NO_DISPLAY)
    {
        const EGLint eglError = eglGetError();

        SetError(error, eglError);
        return false;
    }

    // ---------------------------------------------------------
    // 3. INITIALIZE EGL EXACTLY ONCE FOR OUR SHARED USERS
    // ---------------------------------------------------------
    EGLint newMajorVersion = 0;
    EGLint newMinorVersion = 0;

    if(eglInitialize(
           newDisplay,
           &newMajorVersion,
           &newMinorVersion) != EGL_TRUE)
    {
        const EGLint eglError = eglGetError();

        SetError(error, eglError);
        return false;
    }

    // ---------------------------------------------------------
    // 4. COMMIT THE SHARED STATE
    // ---------------------------------------------------------
    state.display = newDisplay;
    state.majorVersion = newMajorVersion;
    state.minorVersion = newMinorVersion;
    state.referenceCount = 1;

    *display = state.display;

    if(major != nullptr)
    {
        *major = state.majorVersion;
    }

    if(minor != nullptr)
    {
        *minor = state.minorVersion;
    }

    return true;
}

bool CEglDisplayManager::Release(
    EGLDisplay display,
    EGLint* error)
{
    SetError(error, EGL_SUCCESS);

    if(display == EGL_NO_DISPLAY)
    {
        SetError(error, EGL_BAD_DISPLAY);
        return false;
    }

    EglDisplayState& state = GetEglDisplayState();

    std::lock_guard<std::mutex> lock(state.mutex);

    // ---------------------------------------------------------
    // 1. VALIDATE DISPLAY LEASE
    // ---------------------------------------------------------

    if(state.referenceCount == 0 ||
       state.display == EGL_NO_DISPLAY ||
       state.display != display)
    {
        SetError(error, EGL_BAD_DISPLAY);
        return false;
    }

    // ---------------------------------------------------------
    // 2. RELEASE NON-FINAL LEASE
    // ---------------------------------------------------------

    if(state.referenceCount > 1)
    {
        --state.referenceCount;
        return true;
    }

    // ---------------------------------------------------------
    // 3. FINAL LEASE: TERMINATE EGL
    // ---------------------------------------------------------

    const EGLDisplay displayToTerminate = state.display;

    if(eglTerminate(displayToTerminate) != EGL_TRUE)
    {
        const EGLint eglError = eglGetError();

        // IMPORTANT:
        // Keep the display and its final reference alive.
        // The caller must retain its handle and may retry Release().
        //
        // Do not reset:
        // - state.display
        // - state.referenceCount
        // - state.majorVersion
        // - state.minorVersion

        SetError(error, eglError);
        return false;
    }

    // ---------------------------------------------------------
    // 4. RESET STATE ONLY AFTER SUCCESS
    // ---------------------------------------------------------

    state.display = EGL_NO_DISPLAY;
    state.referenceCount = 0;
    state.majorVersion = 0;
    state.minorVersion = 0;

    return true;
}

unsigned int CEglDisplayManager::GetReferenceCount()
{
    EglDisplayState& state = GetEglDisplayState();

    std::lock_guard<std::mutex> lock(state.mutex);

    return state.referenceCount;
}

EGLDisplay CEglDisplayManager::GetDisplay()
{
    EglDisplayState& state = GetEglDisplayState();

    std::lock_guard<std::mutex> lock(state.mutex);

    return state.display;
}
