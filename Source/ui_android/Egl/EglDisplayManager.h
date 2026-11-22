#pragma once

#include <EGL/egl.h>

#include <cstdint>
#include <string>

/**
 * @brief Process-wide owner of the initialized EGLDisplay.
 *
 * Responsibilities:
 * - Acquire and initialize the Android EGL display.
 * - Share one initialized display between renderer instances.
 * - Track legacy references and identity-based display leases.
 * - Terminate EGL only after the final reference is released.
 * - Expose EGL version, vendor, client API and extensions.
 * - Provide thread-safe display diagnostics.
 *
 * Non-responsibilities:
 * - EGLConfig selection.
 * - EGLContext creation or current-context management.
 * - EGLSurface creation or destruction.
 * - OpenGL ES capability detection.
 *
 * Threading:
 * - Manager state and display lifecycle are protected internally.
 * - Context and surface operations remain the responsibility
 *   of the renderer's EGL/GS execution thread.
 */
class CEglDisplayManager final
{
public:
    struct DisplayInfo
    {
        EGLint majorVersion = 0;
        EGLint minorVersion = 0;

        std::string version;
        std::string vendor;
        std::string clientApis;
        std::string extensions;

        std::uint64_t generation = 0;

        bool initialized = false;
    };

    struct Diagnostics
    {
        EGLDisplay display = EGL_NO_DISPLAY;

        unsigned int legacyReferences = 0;
        unsigned int leaseReferences = 0;
        unsigned int totalReferences = 0;

        std::uint64_t generation = 0;
        std::uint64_t initializationCount = 0;
        std::uint64_t terminationCount = 0;

        EGLint lastError = EGL_SUCCESS;

        bool initialized = false;
    };

    CEglDisplayManager() = delete;
    ~CEglDisplayManager() = delete;

    CEglDisplayManager(const CEglDisplayManager&) = delete;
    CEglDisplayManager& operator=(
        const CEglDisplayManager&) = delete;

    /**
     * @brief Acquire an identity-based display lease.
     *
     * Every successful call produces one unique lease ID.
     *
     * The caller must release the lease exactly once by calling
     * ReleaseLease() with the same display and lease ID.
     */
    static bool AcquireLease(
        EGLDisplay* display,
        std::uint64_t* leaseId,
        EGLint* major = nullptr,
        EGLint* minor = nullptr,
        EGLint* error = nullptr);

    /**
     * @brief Release an identity-based display lease.
     *
     * The lease ID is cleared only after successful release.
     * If eglTerminate() fails, the lease remains active and
     * the caller can retry the operation.
     */
    static bool ReleaseLease(
        EGLDisplay display,
        std::uint64_t* leaseId,
        EGLint* error = nullptr);

    /**
     * @brief Legacy display acquisition API.
     *
     * Kept for compatibility with existing callers.
     * New renderer code should use AcquireLease().
     */
    static bool Acquire(
        EGLDisplay* display,
        EGLint* major = nullptr,
        EGLint* minor = nullptr,
        EGLint* error = nullptr);

    /**
     * @brief Release one legacy reference.
     *
     * This function cannot release an identity-based lease.
     */
    static bool Release(
        EGLDisplay display,
        EGLint* error = nullptr);

    static unsigned int GetReferenceCount();

    static EGLDisplay GetDisplay();

    /**
     * @brief Copy the currently cached display information.
     */
    static bool GetDisplayInfo(DisplayInfo* info);

    /**
     * @brief Check an EGL extension using exact token matching.
     */
    static bool HasExtension(const char* extension);

    /**
     * @brief Retrieve thread-safe manager diagnostics.
     */
    static Diagnostics GetDiagnostics();
};
