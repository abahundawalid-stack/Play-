#include "EglDisplayManager.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <unordered_set>
#include <utility>

namespace
{
    struct EglDisplayState
    {
        std::mutex mutex;

        EGLDisplay display = EGL_NO_DISPLAY;

        unsigned int legacyReferences = 0;

        std::unordered_set<std::uint64_t> leases;

        std::uint64_t nextLeaseId = 1;
        std::uint64_t generation = 0;

        std::uint64_t initializationCount = 0;
        std::uint64_t terminationCount = 0;

        EGLint majorVersion = 0;
        EGLint minorVersion = 0;

        EGLint lastError = EGL_SUCCESS;

        CEglDisplayManager::DisplayInfo info;
    };

    EglDisplayState& GetState()
    {
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
        std::uint64_t* leaseId,
        EGLint* major,
        EGLint* minor,
        EGLint* error)
    {
        if(display != nullptr)
        {
            *display = EGL_NO_DISPLAY;
        }

        if(leaseId != nullptr)
        {
            *leaseId = 0;
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

    std::string ReadDisplayString(
        EGLDisplay display,
        EGLint name)
    {
        const char* value = eglQueryString(display, name);

        if(value == nullptr)
        {
            return {};
        }

        return std::string(value);
    }

    bool HasExtensionToken(
        const std::string& extensions,
        const char* extension)
    {
        if(extension == nullptr || extension[0] == '\0')
        {
            return false;
        }

        // EGL extension names are separated by spaces.
        // Reject partial-token matches.
        const std::string requested(extension);

        if(requested.find(' ') != std::string::npos)
        {
            return false;
        }

        std::size_t position = 0;

        while((position = extensions.find(
                  requested,
                  position)) != std::string::npos)
        {
            const bool leftBoundary =
                position == 0 ||
                extensions[position - 1] == ' ';

            const std::size_t end =
                position + requested.size();

            const bool rightBoundary =
                end == extensions.size() ||
                extensions[end] == ' ';

            if(leftBoundary && rightBoundary)
            {
                return true;
            }

            position = end;
        }

        return false;
    }

    std::uint64_t GetTotalReferences(
        const EglDisplayState& state)
    {
        return
            static_cast<std::uint64_t>(
                state.legacyReferences) +
            static_cast<std::uint64_t>(
                state.leases.size());
    }

    void ResetDisplayState(EglDisplayState& state)
    {
        state.display = EGL_NO_DISPLAY;

        state.majorVersion = 0;
        state.minorVersion = 0;

        state.info = CEglDisplayManager::DisplayInfo{};
    }

    bool InitializeDisplayLocked(
        EglDisplayState& state,
        EGLint* error)
    {
        if(GetTotalReferences(state) != 0)
        {
            if(state.display == EGL_NO_DISPLAY ||
               !state.info.initialized)
            {
                SetError(error, EGL_BAD_DISPLAY);
                state.lastError = EGL_BAD_DISPLAY;
                return false;
            }

            return true;
        }

        // A non-empty display state with no references is invalid.
        if(state.display != EGL_NO_DISPLAY)
        {
            SetError(error, EGL_BAD_ACCESS);
            state.lastError = EGL_BAD_ACCESS;
            return false;
        }

        EGLDisplay newDisplay =
            eglGetDisplay(EGL_DEFAULT_DISPLAY);

        if(newDisplay == EGL_NO_DISPLAY)
        {
            const EGLint eglError = eglGetError();

            SetError(error, eglError);
            state.lastError = eglError;
            return false;
        }

        EGLint newMajor = 0;
        EGLint newMinor = 0;

        if(eglInitialize(
               newDisplay,
               &newMajor,
               &newMinor) != EGL_TRUE)
        {
            const EGLint eglError = eglGetError();

            SetError(error, eglError);
            state.lastError = eglError;

            return false;
        }

        if(newMajor <= 0 || newMinor < 0)
        {
            // Initialization succeeded, but the returned version
            // is not a valid EGL version tuple.
            const EGLint terminateResult =
                eglTerminate(newDisplay);

            const EGLint terminateError =
                terminateResult == EGL_TRUE ?
                    EGL_SUCCESS : eglGetError();

            const EGLint reportedError =
                terminateResult == EGL_TRUE ?
                    EGL_BAD_DISPLAY : terminateError;

            SetError(error, reportedError);
            state.lastError = reportedError;

            return false;
        }

        CEglDisplayManager::DisplayInfo info;

        info.majorVersion = newMajor;
        info.minorVersion = newMinor;

        info.version = ReadDisplayString(
            newDisplay,
            EGL_VERSION);

        info.vendor = ReadDisplayString(
            newDisplay,
            EGL_VENDOR);

        info.clientApis = ReadDisplayString(
            newDisplay,
            EGL_CLIENT_APIS);

        info.extensions = ReadDisplayString(
            newDisplay,
            EGL_EXTENSIONS);

        info.initialized = true;

        ++state.generation;
        ++state.initializationCount;

        info.generation = state.generation;

        state.display = newDisplay;
        state.majorVersion = newMajor;
        state.minorVersion = newMinor;
        state.info = std::move(info);

        state.lastError = EGL_SUCCESS;

        return true;
    }

    bool TerminateDisplayLocked(
        EglDisplayState& state,
        EGLint* error)
    {
        if(GetTotalReferences(state) != 0)
        {
            SetError(error, EGL_BAD_ACCESS);
            state.lastError = EGL_BAD_ACCESS;
            return false;
        }

        if(state.display == EGL_NO_DISPLAY)
        {
            SetError(error, EGL_BAD_DISPLAY);
            state.lastError = EGL_BAD_DISPLAY;
            return false;
        }

        const EGLDisplay displayToTerminate =
            state.display;

        if(eglTerminate(displayToTerminate) != EGL_TRUE)
        {
            const EGLint eglError = eglGetError();

            SetError(error, eglError);
            state.lastError = eglError;

            // Do not clear the display state.
            // The caller must be able to retry termination.
            return false;
        }

        ++state.terminationCount;

        ResetDisplayState(state);

        state.lastError = EGL_SUCCESS;

        return true;
    }

    bool CreateLeaseIdLocked(
        EglDisplayState& state,
        std::uint64_t* leaseId)
    {
        if(leaseId == nullptr)
        {
            return false;
        }

        // Zero is reserved as an invalid lease ID.
        // Avoid reusing a live ID after integer wraparound.
        for(std::uint64_t attempt = 0;
            attempt < std::numeric_limits<std::uint64_t>::max();
            ++attempt)
        {
            std::uint64_t candidate =
                state.nextLeaseId++;

            if(candidate == 0)
            {
                candidate = state.nextLeaseId++;
            }

            if(candidate == 0)
            {
                continue;
            }

            if(state.leases.find(candidate) ==
               state.leases.end())
            {
                *leaseId = candidate;
                return true;
            }
        }

        return false;
    }
}

bool CEglDisplayManager::AcquireLease(
    EGLDisplay* display,
    std::uint64_t* leaseId,
    EGLint* major,
    EGLint* minor,
    EGLint* error)
{
    ClearOutputs(
        display,
        leaseId,
        major,
        minor,
        error);

    if(display == nullptr || leaseId == nullptr)
    {
        SetError(error, EGL_BAD_PARAMETER);
        return false;
    }

    EglDisplayState& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);

    if(GetTotalReferences(state) >=
       std::numeric_limits<unsigned int>::max())
    {
        SetError(error, EGL_BAD_ALLOC);
        state.lastError = EGL_BAD_ALLOC;
        return false;
    }

    std::uint64_t newLeaseId = 0;

    if(!CreateLeaseIdLocked(state, &newLeaseId))
    {
        SetError(error, EGL_BAD_ALLOC);
        state.lastError = EGL_BAD_ALLOC;
        return false;
    }

    // Insert the identity before initializing EGL. If allocation
    // fails, the display must not be initialized without an owner.
    try
    {
        state.leases.insert(newLeaseId);
    }
    catch(...)
    {
        SetError(error, EGL_BAD_ALLOC);
        state.lastError = EGL_BAD_ALLOC;
        return false;
    }

    if(!InitializeDisplayLocked(state, error))
    {
        state.leases.erase(newLeaseId);
        return false;
    }

    *display = state.display;
    *leaseId = newLeaseId;

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

bool CEglDisplayManager::ReleaseLease(
    EGLDisplay display,
    std::uint64_t* leaseId,
    EGLint* error)
{
    SetError(error, EGL_SUCCESS);

    if(leaseId == nullptr || *leaseId == 0 ||
       display == EGL_NO_DISPLAY)
    {
        SetError(error, EGL_BAD_PARAMETER);
        return false;
    }

    EglDisplayState& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);

    if(state.display == EGL_NO_DISPLAY ||
       state.display != display ||
       state.info.initialized == false)
    {
        SetError(error, EGL_BAD_DISPLAY);
        state.lastError = EGL_BAD_DISPLAY;
        return false;
    }

    const auto leaseIterator =
        state.leases.find(*leaseId);

    if(leaseIterator == state.leases.end())
    {
        SetError(error, EGL_BAD_ACCESS);
        state.lastError = EGL_BAD_ACCESS;
        return false;
    }

    if(GetTotalReferences(state) > 1)
    {
        state.leases.erase(leaseIterator);

        *leaseId = 0;
        state.lastError = EGL_SUCCESS;

        return true;
    }

    // The final lease remains registered if eglTerminate fails.
    // The caller retains its ID and can retry.
    if(!TerminateDisplayLocked(state, error))
    {
        return false;
    }

    state.leases.erase(leaseIterator);

    *leaseId = 0;

    return true;
}

bool CEglDisplayManager::Acquire(
    EGLDisplay* display,
    EGLint* major,
    EGLint* minor,
    EGLint* error)
{
    ClearOutputs(
        display,
        nullptr,
        major,
        minor,
        error);

    if(display == nullptr)
    {
        SetError(error, EGL_BAD_PARAMETER);
        return false;
    }

    EglDisplayState& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);

    if(GetTotalReferences(state) >=
       std::numeric_limits<unsigned int>::max())
    {
        SetError(error, EGL_BAD_ALLOC);
        state.lastError = EGL_BAD_ALLOC;
        return false;
    }

    if(!InitializeDisplayLocked(state, error))
    {
        return false;
    }

    ++state.legacyReferences;

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

    EglDisplayState& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);

    if(state.display == EGL_NO_DISPLAY ||
       state.display != display ||
       !state.info.initialized)
    {
        SetError(error, EGL_BAD_DISPLAY);
        state.lastError = EGL_BAD_DISPLAY;
        return false;
    }

    // Legacy Release must never consume an identity-based lease.
    if(state.legacyReferences == 0)
    {
        SetError(error, EGL_BAD_ACCESS);
        state.lastError = EGL_BAD_ACCESS;
        return false;
    }

    if(GetTotalReferences(state) > 1)
    {
        --state.legacyReferences;
        state.lastError = EGL_SUCCESS;
        return true;
    }

    if(!TerminateDisplayLocked(state, error))
    {
        return false;
    }

    --state.legacyReferences;

    return true;
}

unsigned int CEglDisplayManager::GetReferenceCount()
{
    EglDisplayState& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);

    const std::uint64_t total =
        GetTotalReferences(state);

    if(total >
       std::numeric_limits<unsigned int>::max())
    {
        return std::numeric_limits<unsigned int>::max();
    }

    return static_cast<unsigned int>(total);
}

EGLDisplay CEglDisplayManager::GetDisplay()
{
    EglDisplayState& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);

    return state.display;
}

bool CEglDisplayManager::GetDisplayInfo(DisplayInfo* info)
{
    if(info == nullptr)
    {
        return false;
    }

    EglDisplayState& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);

    *info = state.info;

    return state.info.initialized;
}

bool CEglDisplayManager::HasExtension(const char* extension)
{
    if(extension == nullptr || extension[0] == '\0')
    {
        return false;
    }

    EglDisplayState& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);

    if(!state.info.initialized)
    {
        return false;
    }

    return HasExtensionToken(
        state.info.extensions,
        extension);
}

CEglDisplayManager::Diagnostics
CEglDisplayManager::GetDiagnostics()
{
    EglDisplayState& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);

    Diagnostics diagnostics;

    diagnostics.display = state.display;

    diagnostics.legacyReferences =
        state.legacyReferences;

    diagnostics.leaseReferences =
        static_cast<unsigned int>(
            state.leases.size());

    const std::uint64_t total =
        GetTotalReferences(state);

    diagnostics.totalReferences =
        total > std::numeric_limits<unsigned int>::max() ?
            std::numeric_limits<unsigned int>::max() :
            static_cast<unsigned int>(total);

    diagnostics.generation = state.generation;

    diagnostics.initializationCount =
        state.initializationCount;

    diagnostics.terminationCount =
        state.terminationCount;

    diagnostics.lastError = state.lastError;

    diagnostics.initialized =
        state.info.initialized;

    return diagnostics;
}
