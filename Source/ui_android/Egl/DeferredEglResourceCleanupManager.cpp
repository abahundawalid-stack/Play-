#include "DeferredEglResourceCleanupManager.h"

#include "CEglContextManager.h"
#include "EglDisplayManager.h"

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

#include <EGL/egl.h>

namespace
{
    struct DeferredEglState
    {
        std::mutex mutex;

        std::vector<
            CDeferredEglResourceCleanupManager::ResourceBundle
        > pending;

        // Number of renderer instances currently registered.
        std::size_t activeRenderers = 0;

        // True when the final renderer has been unregistered.
        bool shutdownRequested = false;

        // True only when shutdown was requested and no deferred
        // EGL resources remain.
        bool shutdownComplete = false;
    };

    DeferredEglState& GetState()
    {
        // Intentionally process-lifetime storage.
        // This prevents pending EGL resources from being destroyed
        // by static destruction on an arbitrary shutdown thread.
        static DeferredEglState* state = new DeferredEglState();
        return *state;
    }

    bool HasSurface(
        const CDeferredEglResourceCleanupManager::ResourceBundle& bundle)
    {
        for(const auto& resource : bundle.surfaces)
        {
            if(resource.surface != EGL_NO_SURFACE)
            {
                return true;
            }
        }

        return false;
    }

    bool IsBundleComplete(
        const CDeferredEglResourceCleanupManager::ResourceBundle& bundle)
    {
        return
            bundle.context == EGL_NO_CONTEXT &&
            !HasSurface(bundle) &&
            !bundle.ownsDisplayLease;
    }

    bool IsSurfaceCurrentOnCallingThread(
        EGLSurface surface)
    {
        if(surface == EGL_NO_SURFACE)
        {
            return false;
        }

        return
            eglGetCurrentSurface(EGL_DRAW) == surface ||
            eglGetCurrentSurface(EGL_READ) == surface;
    }

    bool DetachBundleContext(
        CDeferredEglResourceCleanupManager::ResourceBundle& bundle,
        EGLint& error)
    {
        error = EGL_SUCCESS;

        if(bundle.context == EGL_NO_CONTEXT)
        {
            return true;
        }

        const EGLContext currentContext = eglGetCurrentContext();

        if(currentContext == EGL_NO_CONTEXT)
        {
            return true;
        }

        if(currentContext != bundle.context)
        {
            // Never detach a context owned by another renderer.
            // If one of this bundle's surfaces is current under
            // another context, defer the entire bundle.
            for(const auto& resource : bundle.surfaces)
            {
                if(IsSurfaceCurrentOnCallingThread(resource.surface))
                {
                    error = EGL_BAD_ACCESS;
                    return false;
                }
            }

            return true;
        }

        const EGLDisplay currentDisplay = eglGetCurrentDisplay();

        if(currentDisplay != bundle.display)
        {
            error = EGL_BAD_DISPLAY;
            return false;
        }

        return CEglContextManager::ClearCurrent(
            bundle.display,
            bundle.context,
            &error);
    }

    bool DestroySurfaceResource(
        EGLDisplay display,
        CDeferredEglResourceCleanupManager::SurfaceResource& resource,
        EGLint& error)
    {
        error = EGL_SUCCESS;

        if(resource.surface == EGL_NO_SURFACE)
        {
            // No EGL surface remains, so its paired window reference
            // can safely be released.
            if(resource.window != nullptr)
            {
                ANativeWindow_release(resource.window);
                resource.window = nullptr;
            }

            return true;
        }

        if(display == EGL_NO_DISPLAY)
        {
            error = EGL_BAD_DISPLAY;
            return false;
        }

        // A surface current on another context must not be destroyed
        // through this bundle.
        if(IsSurfaceCurrentOnCallingThread(resource.surface))
        {
            error = EGL_BAD_ACCESS;
            return false;
        }

        if(eglDestroySurface(display, resource.surface) != EGL_TRUE)
        {
            error = eglGetError();
            return false;
        }

        // Clear the handle only after EGL confirms destruction.
        resource.surface = EGL_NO_SURFACE;

        // Release exactly the reference paired with this surface.
        if(resource.window != nullptr)
        {
            ANativeWindow_release(resource.window);
            resource.window = nullptr;
        }

        return true;
    }

    bool DestroyBundleContext(
        CDeferredEglResourceCleanupManager::ResourceBundle& bundle,
        EGLint& error)
    {
        error = EGL_SUCCESS;

        if(bundle.context == EGL_NO_CONTEXT)
        {
            return true;
        }

        if(HasSurface(bundle))
        {
            error = EGL_BAD_ACCESS;
            return false;
        }

        if(!CEglContextManager::Destroy(
               bundle.display,
               &bundle.context,
               &error))
        {
            return false;
        }

        return true;
    }

    bool ReleaseBundleDisplay(
        CDeferredEglResourceCleanupManager::ResourceBundle& bundle,
        EGLint& error)
    {
        error = EGL_SUCCESS;

        if(!bundle.ownsDisplayLease)
        {
            return true;
        }

        if(bundle.display == EGL_NO_DISPLAY)
        {
            error = EGL_BAD_DISPLAY;
            return false;
        }

        if(bundle.context != EGL_NO_CONTEXT || HasSurface(bundle))
        {
            error = EGL_BAD_ACCESS;
            return false;
        }

        if(!CEglDisplayManager::Release(bundle.display, &error))
        {
            return false;
        }

        bundle.display = EGL_NO_DISPLAY;
        bundle.ownsDisplayLease = false;

        return true;
    }

    void RemoveCompletedSurfaces(
        CDeferredEglResourceCleanupManager::ResourceBundle& bundle)
    {
        auto& surfaces = bundle.surfaces;

        surfaces.erase(
            std::remove_if(
                surfaces.begin(),
                surfaces.end(),
                [](const auto& resource)
                {
                    return
                        resource.surface == EGL_NO_SURFACE &&
                        resource.window == nullptr;
                }),
            surfaces.end());
    }
}

bool CDeferredEglResourceCleanupManager::Adopt(
    ResourceBundle&& bundle)
{
    const bool hasAnyResource =
        bundle.display != EGL_NO_DISPLAY ||
        bundle.context != EGL_NO_CONTEXT ||
        bundle.ownsDisplayLease ||
        !bundle.surfaces.empty();

    if(!hasAnyResource)
    {
        return true;
    }

    DeferredEglState& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);

    try
    {
        state.pending.emplace_back(std::move(bundle));
    }
    catch(...)
    {
        // The caller retains ownership when adoption fails.
        return false;
    }

    return true;
}

CDeferredEglResourceCleanupManager::DrainResult
CDeferredEglResourceCleanupManager::Drain()
{
    DrainResult result;

    DeferredEglState& state = GetState();

    // Keep the queue serialized while EGL resources are processed.
    // Drain is called only from the GS execution path.
    std::lock_guard<std::mutex> lock(state.mutex);

    auto iterator = state.pending.begin();

    while(iterator != state.pending.end())
    {
        ResourceBundle& bundle = *iterator;

        ++result.bundlesVisited;

        // Release orphaned window references only when their
        // corresponding EGL surface handle is already absent.
        for(auto& resource : bundle.surfaces)
        {
            if(resource.surface == EGL_NO_SURFACE &&
               resource.window != nullptr)
            {
                ANativeWindow_release(resource.window);
                resource.window = nullptr;
            }
        }

        RemoveCompletedSurfaces(bundle);

        // A bundle with no remaining EGL resource and no display
        // lease can be removed without calling EGL.
        if(IsBundleComplete(bundle))
        {
            ++result.bundlesCompleted;
            iterator = state.pending.erase(iterator);
            continue;
        }

        if(bundle.display == EGL_NO_DISPLAY)
        {
            // Without a valid display, EGL handles must remain owned.
            ++result.bundlesDeferred;
            ++iterator;
            continue;
        }

        EGLint error = EGL_SUCCESS;

        // First detach the context if it is current on this thread.
        // This is essential before destroying its surfaces.
        if(!DetachBundleContext(bundle, error))
        {
            ++result.bundlesDeferred;
            ++iterator;
            continue;
        }

        bool surfacesClean = true;

        for(auto& resource : bundle.surfaces)
        {
            const bool wasAlive =
                resource.surface != EGL_NO_SURFACE;

            if(!DestroySurfaceResource(
                   bundle.display,
                   resource,
                   error))
            {
                surfacesClean = false;
                continue;
            }

            if(wasAlive)
            {
                ++result.surfacesDestroyed;
            }
        }

        RemoveCompletedSurfaces(bundle);

        if(!surfacesClean || HasSurface(bundle))
        {
            ++result.bundlesDeferred;
            ++iterator;
            continue;
        }

        // Context destruction is strictly ordered after surface cleanup.
        if(bundle.context != EGL_NO_CONTEXT)
        {
            if(!DestroyBundleContext(bundle, error))
            {
                ++result.bundlesDeferred;
                ++iterator;
                continue;
            }

            ++result.contextsDestroyed;
        }

        // The display lease belongs to this bundle until its final
        // EGL resource has been successfully destroyed.
        if(bundle.ownsDisplayLease)
        {
            if(!ReleaseBundleDisplay(bundle, error))
            {
                ++result.bundlesDeferred;
                ++iterator;
                continue;
            }

            ++result.displaysReleased;
        }
        else
        {
            bundle.display = EGL_NO_DISPLAY;
        }

        if(IsBundleComplete(bundle))
        {
            ++result.bundlesCompleted;
            iterator = state.pending.erase(iterator);
        }
        else
        {
            ++result.bundlesDeferred;
            ++iterator;
        }
    }

    return result;
}

std::size_t CDeferredEglResourceCleanupManager::GetPendingBundleCount()
{
    DeferredEglState& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);

    return state.pending.size();
}
