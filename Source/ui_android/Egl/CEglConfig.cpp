#include "CEglConfig.h"

#include <EGL/eglext.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace
{
    constexpr EGLint REQUIRED_COLOR_BITS = 8;

    constexpr long long SCORE_NATIVE_VISUAL_MATCH = 1000000LL;
    constexpr long long SCORE_RGBA8888 = 10000LL;
    constexpr long long SCORE_RGB888 = 5000LL;
    constexpr long long SCORE_ALPHA8 = 1000LL;
    constexpr long long SCORE_NO_MSAA = 500LL;
    constexpr long long SCORE_NO_DEPTH = 200LL;
    constexpr long long SCORE_NO_STENCIL = 200LL;
    constexpr long long SCORE_NO_CAVEAT = 100LL;

    struct Candidate
    {
        EGLConfig config = nullptr;

        CEglConfig::SelectionInfo info;
    };

    void SetError(EGLint* output, EGLint value)
    {
        if(output != nullptr)
        {
            *output = value;
        }
    }

    bool ReadConfigAttribute(
        EGLDisplay display,
        EGLConfig config,
        EGLint attribute,
        EGLint* value)
    {
        if(value == nullptr)
        {
            return false;
        }

        *value = 0;

        if(eglGetConfigAttrib(
               display,
               config,
               attribute,
               value) != EGL_TRUE)
        {
            return false;
        }

        return true;
    }

    bool ReadCandidate(
        EGLDisplay display,
        EGLConfig config,
        EGLint nativeWindowFormat,
        Candidate* candidate)
    {
        if(candidate == nullptr)
        {
            return false;
        }

        Candidate result;
        result.config = config;

        EGLint surfaceType = 0;
        EGLint renderableType = 0;
        EGLint colorBufferType = 0;

        // All mandatory properties must be readable.
        if(!ReadConfigAttribute(
               display, config, EGL_SURFACE_TYPE, &surfaceType) ||
           !ReadConfigAttribute(
               display, config, EGL_RENDERABLE_TYPE, &renderableType) ||
           !ReadConfigAttribute(
               display, config, EGL_COLOR_BUFFER_TYPE, &colorBufferType) ||
           !ReadConfigAttribute(
               display, config, EGL_CONFIG_ID, &result.info.configId) ||
           !ReadConfigAttribute(
               display, config, EGL_NATIVE_VISUAL_ID,
               &result.info.nativeVisualId) ||
           !ReadConfigAttribute(
               display, config, EGL_RED_SIZE, &result.info.redSize) ||
           !ReadConfigAttribute(
               display, config, EGL_GREEN_SIZE, &result.info.greenSize) ||
           !ReadConfigAttribute(
               display, config, EGL_BLUE_SIZE, &result.info.blueSize) ||
           !ReadConfigAttribute(
               display, config, EGL_ALPHA_SIZE, &result.info.alphaSize) ||
           !ReadConfigAttribute(
               display, config, EGL_DEPTH_SIZE, &result.info.depthSize) ||
           !ReadConfigAttribute(
               display, config, EGL_STENCIL_SIZE,
               &result.info.stencilSize) ||
           !ReadConfigAttribute(
               display, config, EGL_SAMPLE_BUFFERS,
               &result.info.sampleBuffers) ||
           !ReadConfigAttribute(
               display, config, EGL_SAMPLES, &result.info.samples) ||
           !ReadConfigAttribute(
               display, config, EGL_BUFFER_SIZE,
               &result.info.bufferSize) ||
           !ReadConfigAttribute(
               display, config, EGL_CONFIG_CAVEAT,
               &result.info.caveat))
        {
            return false;
        }

        // -----------------------------------------------------
        // 1. HARD REQUIREMENTS
        // -----------------------------------------------------

        if((surfaceType & EGL_WINDOW_BIT) == 0)
        {
            return false;
        }

        if((renderableType & EGL_OPENGL_ES3_BIT_KHR) == 0)
        {
            return false;
        }

        if(colorBufferType != EGL_RGB_BUFFER)
        {
            return false;
        }

        if(result.info.redSize < REQUIRED_COLOR_BITS ||
           result.info.greenSize < REQUIRED_COLOR_BITS ||
           result.info.blueSize < REQUIRED_COLOR_BITS)
        {
            return false;
        }

        // Negative values are invalid for these reported properties.
        if(result.info.alphaSize < 0 ||
           result.info.depthSize < 0 ||
           result.info.stencilSize < 0 ||
           result.info.sampleBuffers < 0 ||
           result.info.samples < 0)
        {
            return false;
        }

        result.info.nativeWindowFormat = nativeWindowFormat;

        // -----------------------------------------------------
        // 2. SCORE THE CANDIDATE
        // -----------------------------------------------------

        long long score = 0;

        // Prefer exact Android native visual compatibility.
        if(nativeWindowFormat > 0 &&
           result.info.nativeVisualId == nativeWindowFormat)
        {
            score += SCORE_NATIVE_VISUAL_MATCH;
        }

        // Prefer RGBA8888 for a high-quality Android framebuffer.
        if(result.info.redSize == 8 &&
           result.info.greenSize == 8 &&
           result.info.blueSize == 8 &&
           result.info.alphaSize == 8)
        {
            score += SCORE_RGBA8888;
        }
        else if(result.info.redSize == 8 &&
                result.info.greenSize == 8 &&
                result.info.blueSize == 8)
        {
            score += SCORE_RGB888;
        }

        if(result.info.alphaSize >= 8)
        {
            score += SCORE_ALPHA8;
        }

        // The emulator's GS output is normally rendered into its
        // own render targets. Avoid unnecessary default-surface MSAA.
        if(result.info.sampleBuffers == 0 &&
           result.info.samples == 0)
        {
            score += SCORE_NO_MSAA;
        }
        else
        {
            score -=
                static_cast<long long>(result.info.sampleBuffers) * 500LL;

            score -=
                static_cast<long long>(result.info.samples) * 100LL;
        }

        // Prefer a lightweight presentation surface.
        if(result.info.depthSize == 0)
        {
            score += SCORE_NO_DEPTH;
        }
        else
        {
            score -=
                static_cast<long long>(result.info.depthSize) * 10LL;
        }

        if(result.info.stencilSize == 0)
        {
            score += SCORE_NO_STENCIL;
        }
        else
        {
            score -=
                static_cast<long long>(result.info.stencilSize) * 10LL;
        }

        if(result.info.caveat == EGL_NONE)
        {
            score += SCORE_NO_CAVEAT;
        }
        else if(result.info.caveat == EGL_SLOW_CONFIG)
        {
            score -= 1000LL;
        }

        // Prefer the conventional 32-bit RGBA framebuffer.
        if(result.info.bufferSize == 32)
        {
            score += 500LL;
        }

        result.info.score = score;

        *candidate = result;
        return true;
    }

    bool IsBetterCandidate(
        const Candidate& candidate,
        const Candidate& currentBest)
    {
        if(candidate.info.score != currentBest.info.score)
        {
            return candidate.info.score > currentBest.info.score;
        }

        // Stable deterministic tie-breaker.
        return candidate.info.configId < currentBest.info.configId;
    }
}

bool CEglConfig::ChooseWindowConfig(
    EGLDisplay display,
    ANativeWindow* window,
    EGLConfig* config,
    SelectionInfo* info,
    EGLint* error)
{
    if(config != nullptr)
    {
        *config = nullptr;
    }

    if(info != nullptr)
    {
        *info = SelectionInfo{};
    }

    SetError(error, EGL_SUCCESS);

    if(display == EGL_NO_DISPLAY ||
       window == nullptr ||
       config == nullptr)
    {
        SetError(error, EGL_BAD_PARAMETER);
        return false;
    }

    // ---------------------------------------------------------
    // 1. READ THE CURRENT ANDROID WINDOW FORMAT
    // ---------------------------------------------------------

    const EGLint nativeWindowFormat =
        static_cast<EGLint>(ANativeWindow_getFormat(window));

    // ---------------------------------------------------------
    // 2. ENUMERATE ALL AVAILABLE EGL CONFIGURATIONS
    // ---------------------------------------------------------

    EGLint totalConfigs = 0;

    if(eglGetConfigs(
           display,
           nullptr,
           0,
           &totalConfigs) != EGL_TRUE)
    {
        SetError(error, eglGetError());
        return false;
    }

    if(totalConfigs <= 0)
    {
        SetError(error, EGL_CONFIG_NOT_SUPPORTED);
        return false;
    }

    // Protect against unreasonable or corrupted driver responses.
    constexpr EGLint MAX_CONFIGS = 4096;

    if(totalConfigs > MAX_CONFIGS)
    {
        SetError(error, EGL_BAD_ALLOC);
        return false;
    }

    std::vector<EGLConfig> configs(
        static_cast<size_t>(totalConfigs),
        nullptr);

    EGLint returnedConfigs = 0;

    if(eglGetConfigs(
           display,
           configs.data(),
           totalConfigs,
           &returnedConfigs) != EGL_TRUE)
    {
        SetError(error, eglGetError());
        return false;
    }

    if(returnedConfigs <= 0)
    {
        SetError(error, EGL_CONFIG_NOT_SUPPORTED);
        return false;
    }

    if(returnedConfigs > totalConfigs)
    {
        SetError(error, EGL_BAD_CONFIG);
        return false;
    }

    configs.resize(static_cast<size_t>(returnedConfigs));

    // ---------------------------------------------------------
    // 3. FILTER AND SCORE CANDIDATES
    // ---------------------------------------------------------

    Candidate bestCandidate;
    bool hasBestCandidate = false;

    EGLint eligibleConfigs = 0;

    for(const EGLConfig currentConfig : configs)
    {
        if(currentConfig == nullptr)
        {
            continue;
        }

        Candidate candidate;

        if(!ReadCandidate(
               display,
               currentConfig,
               nativeWindowFormat,
               &candidate))
        {
            continue;
        }

        ++eligibleConfigs;

        if(!hasBestCandidate ||
           IsBetterCandidate(candidate, bestCandidate))
        {
            bestCandidate = candidate;
            hasBestCandidate = true;
        }
    }

    if(!hasBestCandidate)
    {
        SetError(error, EGL_CONFIG_NOT_SUPPORTED);
        return false;
    }

    // ---------------------------------------------------------
    // 4. COMMIT THE SELECTED CONFIGURATION
    // ---------------------------------------------------------

    bestCandidate.info.enumeratedConfigs = returnedConfigs;
    bestCandidate.info.eligibleConfigs = eligibleConfigs;

    *config = bestCandidate.config;

    if(info != nullptr)
    {
        *info = bestCandidate.info;
    }

    return true;
}

bool CEglConfig::ApplyNativeVisual(
    ANativeWindow* window,
    EGLint nativeVisualId,
    EGLint* error)
{
    SetError(error, EGL_SUCCESS);

    if(window == nullptr || nativeVisualId < 0)
    {
        SetError(error, EGL_BAD_PARAMETER);
        return false;
    }

    // A zero visual ID means the EGL implementation does not
    // provide a usable native visual constraint.
    if(nativeVisualId == 0)
    {
        return true;
    }

    const EGLint currentFormat =
        static_cast<EGLint>(ANativeWindow_getFormat(window));

    // Do not modify a window that already has the selected format.
    if(currentFormat == nativeVisualId)
    {
        return true;
    }

    // Width and height remain unchanged.
    // Only the native pixel format is adjusted to match EGL.
    const int32_t result = ANativeWindow_setBuffersGeometry(
        window,
        0,
        0,
        nativeVisualId);

    if(result != 0)
    {
        // ANativeWindow functions return a negative errno value.
        SetError(error, EGL_BAD_NATIVE_WINDOW);
        return false;
    }

    return true;
}
