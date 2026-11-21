#include <cassert>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <memory>
#include <string>

#include "opengl/OpenGlDef.h"
#include "GSH_OpenGLAndroid.h"
#include "Egl/EglDisplayManager.h"
#include "Egl/CEglConfig.h"
#include "Egl/CEglContextManager.h"
#include "Egl/DeferredEglResourceCleanupManager.h"
#include "AppConfig.h"
#include "NativeShared.h"
#include "Log.h"

CGSH_OpenGLAndroid::CGSH_OpenGLAndroid(NativeWindowType window)
    : m_window(window)
{
    // Ownership of the supplied ANativeWindow reference is transferred
    // to this renderer instance.
    //
    // The reference must remain alive for as long as EGL may use it.
    // A null window is valid and represents a detached renderer.
}

CGSH_OpenGLAndroid::~CGSH_OpenGLAndroid()
{
    // Release graphics resources through the GS synchronization path.
    Release();

    // If EGL cleanup failed, do not release native-window references
    // that may still be backing live EGL surfaces.
    //
    // This is a last-resort lifetime safeguard. It intentionally
    // retains unresolved native-window references rather than risking
    // a use-after-free inside the EGL driver.
    const bool unresolvedSurfaceOwnership =
        m_surface != EGL_NO_SURFACE ||
        m_surfaceWindow != nullptr ||
        !m_retiredSurfaces.empty();

    if(unresolvedSurfaceOwnership)
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] CRITICAL: renderer destroyed with "
            "unresolved EGL surface ownership. Native-window "
            "references are intentionally not released. "
            "display=%p context=%p surface=%p surfaceWindow=%p "
            "retiredSurfaces=%u\r\n",
            m_display,
            m_context,
            m_surface,
            m_surfaceWindow,
            static_cast<unsigned int>(m_retiredSurfaces.size()));

        // Do not call ANativeWindow_release() for m_surfaceWindow
        // or any retired record's window here.
        //
        // The outstanding native references are deliberately left
        // acquired as a safety fallback if EGL still uses them.
        m_surfaceWindow = nullptr;

        for(auto& retired : m_retiredSurfaces)
        {
            retired.window = nullptr;
        }

        m_retiredSurfaces.clear();
    }

    // Release the renderer's ordinary window reference.
    // Surface-specific references are independent.
    NativeWindowType windowToRelease = m_window;
    m_window = nullptr;

    if(windowToRelease != nullptr)
    {
        ANativeWindow_release(windowToRelease);
        windowToRelease = nullptr;
    }
}

bool CGSH_OpenGLAndroid::DeferOutstandingEglResources()
{
    using CleanupManager =
        CDeferredEglResourceCleanupManager;

    CleanupManager::ResourceBundle bundle;

    bundle.display = m_display;
    bundle.context = m_context;

    // This renderer owns exactly one lease for every successful
    // CEglDisplayManager::Acquire().
    bundle.ownsDisplayLease =
        (m_display != EGL_NO_DISPLAY);

    // Reserve before transferring any ownership.
    // If allocation fails, the renderer retains all its handles.
    try
    {
        bundle.surfaces.reserve(
            m_retiredSurfaces.size() + 1);

        if(m_surface != EGL_NO_SURFACE ||
           m_surfaceWindow != nullptr)
        {
            CleanupManager::SurfaceResource active;

            active.surface = m_surface;
            active.window = m_surfaceWindow;

            bundle.surfaces.emplace_back(active);
        }

        for(const auto& retired : m_retiredSurfaces)
        {
            if(retired.surface == EGL_NO_SURFACE &&
               retired.window == nullptr)
            {
                continue;
            }

            CleanupManager::SurfaceResource resource;

            resource.surface = retired.surface;
            resource.window = retired.window;

            bundle.surfaces.emplace_back(resource);
        }
    }
    catch(...)
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] deferred cleanup bundle allocation failed. "
            "Original renderer ownership retained. "
            "display=%p context=%p surface=%p "
            "retiredSurfaces=%u\r\n",
            m_display,
            m_context,
            m_surface,
            static_cast<unsigned int>(
                m_retiredSurfaces.size()));

        return false;
    }

    const bool hasResources =
        bundle.display != EGL_NO_DISPLAY ||
        bundle.context != EGL_NO_CONTEXT ||
        bundle.ownsDisplayLease ||
        !bundle.surfaces.empty();

    if(!hasResources)
    {
        return true;
    }

    // Adopt() only moves ownership into the manager.
    // It does not execute EGL calls on this thread.
    if(!CleanupManager::Adopt(std::move(bundle)))
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] deferred cleanup adoption failed. "
            "Renderer retains original ownership. "
            "display=%p context=%p surface=%p\r\n",
            m_display,
            m_context,
            m_surface);

        return false;
    }

    // Ownership has now transferred successfully.
    // The renderer must never release these resources again.
    m_display = EGL_NO_DISPLAY;
    m_context = EGL_NO_CONTEXT;
    m_surface = EGL_NO_SURFACE;

    m_surfaceWindow = nullptr;

    m_retiredSurfaces.clear();

    m_config = nullptr;
    m_nativeVisualId = 0;

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] unresolved EGL resources transferred "
        "to deferred cleanup manager. "
        "pendingBundles=%u\r\n",
        static_cast<unsigned int>(
            CleanupManager::GetPendingBundleCount()));

    return true;
}

void CGSH_OpenGLAndroid::ReleaseEglDisplay()
{
    if(m_display == EGL_NO_DISPLAY)
    {
        return;
    }

    // Do not release the display while any EGL resource
    // owned by this renderer is still alive.
    if(m_context != EGL_NO_CONTEXT ||
       m_surface != EGL_NO_SURFACE ||
       m_surfaceWindow != nullptr ||
       !m_retiredSurfaces.empty())

    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] display release blocked: "
            "EGL resources remain alive. "
            "context=%p surface=%p surfaceWindow=%p retiredSurfaces=%u "
            "display=%p\r\n",
            m_context,
            m_surface,
            m_surfaceWindow,
            static_cast<unsigned int>(m_retiredSurfaces.size()),
            m_display);

        return;
    }

    const EGLDisplay displayToRelease = m_display;

    EGLint error = EGL_SUCCESS;

    if(!CEglDisplayManager::Release(
           displayToRelease,
           &error))
    {
        // Preserve m_display so the same lease can be retried.
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] shared display release failed; "
            "display handle retained. "
            "error=0x%04X display=%p references=%u\r\n",
            error,
            displayToRelease,
            CEglDisplayManager::GetReferenceCount());

        return;
    }

    // Clear the local handle only after successful lease release.
    m_display = EGL_NO_DISPLAY;

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] shared display lease released "
        "display=%p remainingReferences=%u\r\n",
        displayToRelease,
        CEglDisplayManager::GetReferenceCount());
}

void CGSH_OpenGLAndroid::InitializeImpl()
{ 
        const auto cleanupResult =
    CDeferredEglResourceCleanupManager::Drain();

    FRAMEWORK_MAYBE_UNUSED cleanupResult;
	
    if(m_window == nullptr)
    {
        m_surfaceRecoveryPending = true;
		
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] InitializeImpl aborted: native window is null.\r\n");

        return;
    }

    if(m_display != EGL_NO_DISPLAY)
    {
        return;
    }

	m_surfaceRecoveryPending = true;

    // ---------------------------------------------------------
    // ACQUIRE SHARED EGL DISPLAY
    // ---------------------------------------------------------
    EGLint eglMajorVersion = 0;
    EGLint eglMinorVersion = 0;
    EGLint eglError = EGL_SUCCESS;

    if(!CEglDisplayManager::Acquire(
           &m_display,
           &eglMajorVersion,
           &eglMinorVersion,
           &eglError))
    {
        m_display = EGL_NO_DISPLAY;

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] shared display acquisition failed "
            "error=0x%04X\r\n",
            eglError);

        m_surfaceRecoveryPending = true;
        return;
    }

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] shared display acquired "
        "display=%p EGL=%d.%d references=%u\r\n",
        m_display,
        eglMajorVersion,
        eglMinorVersion,
        CEglDisplayManager::GetReferenceCount()); 
	
    // ---------------------------------------------------------
    // SELECT THE BEST ANDROID EGL CONFIGURATION
    // ---------------------------------------------------------

    CEglConfig::SelectionInfo configInfo{};
    EGLint configError = EGL_SUCCESS;

    if(!CEglConfig::ChooseWindowConfig(
           m_display,
           m_window,
           &m_config,
           &configInfo,
           &configError))
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] EGL config selection failed "
            "error=0x%04X window=%p\r\n",
            configError,
            m_window);

        m_config = nullptr;
        m_nativeVisualId = 0;

        ReleaseEglDisplay();

        m_surfaceRecoveryPending = true;
        return;
    }
 
    m_nativeVisualId = configInfo.nativeVisualId;

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] EGL config selected "
        "id=%d visual=%d nativeFormat=%d "
        "RGBA=%d/%d/%d/%d "
        "depth=%d stencil=%d "
        "samples=%d sampleBuffers=%d "
        "score=%lld "
        "candidates=%d/%d\r\n",
        configInfo.configId,
        configInfo.nativeVisualId,
        configInfo.nativeWindowFormat,
        configInfo.redSize,
        configInfo.greenSize,
        configInfo.blueSize,
        configInfo.alphaSize,
        configInfo.depthSize,
        configInfo.stencilSize,
        configInfo.samples,
        configInfo.sampleBuffers,
        configInfo.score,
        configInfo.eligibleConfigs,
        configInfo.enumeratedConfigs);

    // ---------------------------------------------------------
    // CREATE OPENGL ES 3 CONTEXT THROUGH CONTEXT MANAGER
    // ---------------------------------------------------------

    EGLint contextError = EGL_SUCCESS;

    if(!CEglContextManager::Create(
           m_display,
           m_config,
           3,
           EGL_NO_CONTEXT,
           &m_context,
           &contextError))
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] context creation failed "
            "error=0x%04X display=%p config=%p\r\n",
            contextError,
            m_display,
            m_config);

        m_context = EGL_NO_CONTEXT;
        m_config = nullptr;
        m_nativeVisualId = 0;

        ReleaseEglDisplay();

        m_surfaceRecoveryPending = true;
        return;
    }

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] OpenGL ES context created "
        "context=%p display=%p config=%p version=3\r\n",
        m_context,
        m_display,
        m_config);
    
    if(!SetupContext())
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] SetupContext failed during initialization.\r\n");

        // Retry cleanup of any replacement surface that failed
        // during SetupContext().
        CleanupRetiredSurfaces();

        const bool surfacesClean =
            m_surface == EGL_NO_SURFACE &&
            m_surfaceWindow == nullptr &&
            m_retiredSurfaces.empty();

        // Destroy the context only after all surfaces are clean.
        if(surfacesClean &&
           m_context != EGL_NO_CONTEXT)
        {
            EGLint destroyError = EGL_SUCCESS;

            if(!CEglContextManager::Destroy(
                   m_display,
                   &m_context,
                   &destroyError))
            {
                CLog::GetInstance().Print(
                    "glesandroid",
                    "[ORBIT2SX_EGL] initialization context cleanup failed; "
                    "context retained. "
                    "error=0x%04X context=%p\r\n",
                    destroyError,
                    m_context);
            }
        }

        const bool contextClean =
            m_context == EGL_NO_CONTEXT;

        const bool allResourcesClean =
            surfacesClean && contextClean;

        // Release the display only when every EGL resource
        // owned by this renderer has been destroyed.
        if(allResourcesClean)
        {
            ReleaseEglDisplay();
        }
        else
        {
            CLog::GetInstance().Print(
                "glesandroid",
                "[ORBIT2SX_EGL] initialization cleanup deferred. "
                "display=%p context=%p surface=%p "
                "retiredSurfaces=%u\r\n",
                m_display,
                m_context,
                m_surface,
                static_cast<unsigned int>(m_retiredSurfaces.size()));
        }

        // Reset config only when display release succeeded.
        if(m_display == EGL_NO_DISPLAY)
        {
            m_config = nullptr;
            m_nativeVisualId = 0;
        }

        m_surfaceRecoveryPending = true;
        return;
    }
	
    CGSH_OpenGL::InitializeImpl();
    m_rendererInitialized = true;

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] renderer initialized display=%p context=%p surface=%p window=%p\r\n",
        m_display,
        m_context,
        m_surface,
        m_window);
}

bool CGSH_OpenGLAndroid::SetupContext()
{
    auto failRecovery = [this]() -> bool
    {
        m_surfaceRecoveryPending = true;
        return false;
    };

    if(m_display == EGL_NO_DISPLAY ||
       m_context == EGL_NO_CONTEXT ||
       m_config == nullptr ||
       m_window == nullptr)
    {
        return failRecovery();
    }

    // Retry previously failed surface cleanup.
    CleanupRetiredSurfaces();

    // Match the Android native-window format to the EGL config.
    EGLint nativeVisualError = EGL_SUCCESS;

    if(!CEglConfig::ApplyNativeVisual(
           m_window,
           m_nativeVisualId,
           &nativeVisualError))
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] native visual application failed "
            "error=0x%04X visual=%d window=%p\r\n",
            nativeVisualError,
            m_nativeVisualId,
            m_window);

        return failRecovery();
    }

    // Acquire an independent reference for the new EGL surface.
    // This reference is not the same ownership as m_window.
    ANativeWindow_acquire(m_window);
    NativeWindowType newSurfaceWindow = m_window;

    EGLSurface newSurface = eglCreateWindowSurface(
        m_display,
        m_config,
        newSurfaceWindow,
        nullptr);

    if(newSurface == EGL_NO_SURFACE)
    {
        const EGLint error = eglGetError();

        ANativeWindow_release(newSurfaceWindow);
        newSurfaceWindow = nullptr;

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] eglCreateWindowSurface failed "
            "error=0x%04X window=%p\r\n",
            error,
            m_window);

        return failRecovery();
    }

    // Query width and height separately so the EGL error is captured
    // immediately after the failing operation.
    EGLint width = 0;
    EGLint height = 0;
    EGLint queryError = EGL_SUCCESS;

    const EGLBoolean widthOk = eglQuerySurface(
        m_display,
        newSurface,
        EGL_WIDTH,
        &width);

    if(widthOk != EGL_TRUE)
    {
        queryError = eglGetError();
    }

    EGLBoolean heightOk = EGL_FALSE;

    if(widthOk == EGL_TRUE)
    {
        heightOk = eglQuerySurface(
            m_display,
            newSurface,
            EGL_HEIGHT,
            &height);

        if(heightOk != EGL_TRUE)
        {
            queryError = eglGetError();
        }
    }

    if(widthOk != EGL_TRUE ||
       heightOk != EGL_TRUE ||
       width <= 0 ||
       height <= 0)
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] replacement surface dimensions invalid "
            "width=%d height=%d error=0x%04X surface=%p\r\n",
            width,
            height,
            queryError,
            newSurface);

        EGLint cleanupError = EGL_SUCCESS;

        if(DestroySurfaceHandle(newSurface, &cleanupError))
        {
            ANativeWindow_release(newSurfaceWindow);
            newSurfaceWindow = nullptr;
        }
        else
        {
            // Transfer both resources into the retired-surface list.
            RetiredSurface retired;
            retired.surface = newSurface;
            retired.window = newSurfaceWindow;

            m_retiredSurfaces.push_back(retired);
            newSurfaceWindow = nullptr;

            CLog::GetInstance().Print(
                "glesandroid",
                "[ORBIT2SX_EGL] invalid replacement surface "
                "retained for cleanup. error=0x%04X surface=%p "
                "window=%p\r\n",
                cleanupError,
                newSurface,
                retired.window);
        }

        return failRecovery();
    }

    // Activate the new surface before replacing the active handles.
    EGLint makeCurrentError = EGL_SUCCESS;

    if(!CEglContextManager::MakeCurrent(
           m_display,
           newSurface,
           newSurface,
           m_context,
           &makeCurrentError))
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] context activation failed "
            "error=0x%04X context=%p surface=%p\r\n",
            makeCurrentError,
            m_context,
            newSurface);

        EGLint cleanupError = EGL_SUCCESS;

        if(DestroySurfaceHandle(newSurface, &cleanupError))
        {
            ANativeWindow_release(newSurfaceWindow);
            newSurfaceWindow = nullptr;
        }
        else
        {
            RetiredSurface retired;
            retired.surface = newSurface;
            retired.window = newSurfaceWindow;

            m_retiredSurfaces.push_back(retired);
            newSurfaceWindow = nullptr;

            CLog::GetInstance().Print(
                "glesandroid",
                "[ORBIT2SX_EGL] failed replacement surface "
                "retained for cleanup. error=0x%04X surface=%p "
                "window=%p\r\n",
                cleanupError,
                newSurface,
                retired.window);
        }

        return failRecovery();
    }

    // Preserve the previous surface and its native-window reference.
    EGLSurface previousSurface = m_surface;
    NativeWindowType previousSurfaceWindow = m_surfaceWindow;

    // Commit the replacement only after successful MakeCurrent().
    m_surface = newSurface;
    m_surfaceWindow = newSurfaceWindow;
    newSurfaceWindow = nullptr;

    // The new surface is current. The previous surface can now be retired.
    if(previousSurface != EGL_NO_SURFACE &&
       previousSurface != newSurface)
    {
        EGLint cleanupError = EGL_SUCCESS;

        if(DestroySurfaceHandle(previousSurface, &cleanupError))
        {
            if(previousSurfaceWindow != nullptr)
            {
                ANativeWindow_release(previousSurfaceWindow);
                previousSurfaceWindow = nullptr;
            }
        }
        else
        {
            RetiredSurface retired;
            retired.surface = previousSurface;
            retired.window = previousSurfaceWindow;

            m_retiredSurfaces.push_back(retired);
            previousSurfaceWindow = nullptr;

            CLog::GetInstance().Print(
                "glesandroid",
                "[ORBIT2SX_EGL] old surface destruction failed; "
                "surface/window pair retained. error=0x%04X "
                "surface=%p window=%p\r\n",
                cleanupError,
                previousSurface,
                retired.window);
        }
    }
    else if(previousSurface == EGL_NO_SURFACE &&
            previousSurfaceWindow != nullptr)
    {
        // Defensive recovery from an inconsistent old state.
        ANativeWindow_release(previousSurfaceWindow);
        previousSurfaceWindow = nullptr;
    }

    auto readGlString = [](GLenum name) -> std::string
    {
        const GLubyte* value = glGetString(name);

        if(value == nullptr)
        {
            return {};
        }

        return reinterpret_cast<const char*>(value);
    };

    SetGraphicsDiagnostics(
        readGlString(GL_VENDOR),
        readGlString(GL_RENDERER),
        readGlString(GL_VERSION),
        readGlString(GL_SHADING_LANGUAGE_VERSION));

    PRESENTATION_PARAMS presentationParams = {};

    presentationParams.mode =
        static_cast<CGSHandler::PRESENTATION_MODE>(
            CAppConfig::GetInstance().GetPreferenceInteger(
                PREF_CGSHANDLER_PRESENTATION_MODE));

    presentationParams.windowWidth = width;
    presentationParams.windowHeight = height;

    SetPresentationParams(presentationParams);

    if(eglSwapInterval(m_display, 0) != EGL_TRUE)
    {
        const EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] eglSwapInterval failed "
            "error=0x%04X\r\n",
            error);
    }

    m_surfaceRecoveryPending = false;

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] surface ready "
        "display=%p context=%p surface=%p window=%p "
        "surfaceWindow=%p width=%d height=%d "
        "retiredSurfaces=%u\r\n",
        m_display,
        m_context,
        m_surface,
        m_window,
        m_surfaceWindow,
        width,
        height,
        static_cast<unsigned int>(m_retiredSurfaces.size()));

    return true;
}

bool CGSH_OpenGLAndroid::DestroySurfaceHandle(
    EGLSurface surface,
    EGLint* error)
{
    if(error != nullptr)
    {
        *error = EGL_SUCCESS;
    }

    if(surface == EGL_NO_SURFACE)
    {
        return true;
    }

    if(m_display == EGL_NO_DISPLAY)
    {
        if(error != nullptr)
        {
            *error = EGL_BAD_DISPLAY;
        }

        return false;
    }

    const EGLContext currentContext = eglGetCurrentContext();
    const EGLDisplay currentDisplay = eglGetCurrentDisplay();

    const EGLSurface currentDraw =
        eglGetCurrentSurface(EGL_DRAW);

    const EGLSurface currentRead =
        eglGetCurrentSurface(EGL_READ);

    const bool surfaceIsCurrent =
        currentDraw == surface ||
        currentRead == surface;

    // A current surface must belong to this renderer's EGL display.
    if(surfaceIsCurrent &&
       currentContext != EGL_NO_CONTEXT &&
       currentDisplay != m_display)
    {
        if(error != nullptr)
        {
            *error = EGL_BAD_DISPLAY;
        }

        return false;
    }

    // This surface is current on another context.
    // Do not destroy it while that context is using it.
    if(surfaceIsCurrent &&
       currentContext != EGL_NO_CONTEXT &&
       currentContext != m_context)
    {
        if(error != nullptr)
        {
            *error = EGL_BAD_ACCESS;
        }

        return false;
    }

    // Detach our own context only if the target surface
    // is actually current on this thread.
    if(surfaceIsCurrent &&
       currentContext == m_context &&
       m_context != EGL_NO_CONTEXT)
    {
        EGLint detachError = EGL_SUCCESS;

        if(!CEglContextManager::ClearCurrent(
               m_display,
               m_context,
               &detachError))
        {
            if(error != nullptr)
            {
                *error = detachError;
            }

            return false;
        }
    }

    if(eglDestroySurface(m_display, surface) != EGL_TRUE)
    {
        const EGLint destroyError = eglGetError();

        if(error != nullptr)
        {
            *error = destroyError;
        }

        return false;
    }

    return true;
}

void CGSH_OpenGLAndroid::CleanupRetiredSurfaces()
{
    if(m_display == EGL_NO_DISPLAY)
    {
        return;
    }

    auto iterator = m_retiredSurfaces.begin();

    while(iterator != m_retiredSurfaces.end())
    {
        const EGLSurface surface = iterator->surface;
        NativeWindowType window = iterator->window;

        EGLint error = EGL_SUCCESS;

        if(DestroySurfaceHandle(surface, &error))
        {
            // Release the paired native-window reference only after
            // EGL confirms that the corresponding surface is destroyed.
            if(window != nullptr)
            {
                ANativeWindow_release(window);
                window = nullptr;
            }

            CLog::GetInstance().Print(
                "glesandroid",
                "[ORBIT2SX_EGL] retired surface cleanup succeeded "
                "surface=%p window=%p\r\n",
                surface,
                iterator->window);

            iterator = m_retiredSurfaces.erase(iterator);
        }
        else
        {
            CLog::GetInstance().Print(
                "glesandroid",
                "[ORBIT2SX_EGL] retired surface cleanup failed "
                "error=0x%04X surface=%p window=%p\r\n",
                error,
                surface,
                window);

            ++iterator;
        }
    }
}

void CGSH_OpenGLAndroid::DestroySurface()
{
    m_surfaceRecoveryPending = true;

    if(m_surface == EGL_NO_SURFACE)
    {
        // No EGL surface means there must be no active surface-window
        // ownership left behind.
        if(m_surfaceWindow != nullptr)
        {
            ANativeWindow_release(m_surfaceWindow);
            m_surfaceWindow = nullptr;
        }

        return;
    }

    if(m_display == EGL_NO_DISPLAY)
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] cannot destroy surface: "
            "display unavailable. surface=%p window=%p\r\n",
            m_surface,
            m_surfaceWindow);

        // Keep both handles intact. They are still owned.
        return;
    }

    const EGLSurface surfaceToDestroy = m_surface;
    NativeWindowType windowToRelease = m_surfaceWindow;

    EGLint error = EGL_SUCCESS;

    if(!DestroySurfaceHandle(surfaceToDestroy, &error))
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] surface destruction failed; "
            "surface/window ownership retained. "
            "error=0x%04X surface=%p window=%p context=%p\r\n",
            error,
            surfaceToDestroy,
            windowToRelease,
            m_context);

        return;
    }

    // Clear the EGL handle only after successful destruction.
    m_surface = EGL_NO_SURFACE;
    m_surfaceWindow = nullptr;

    // The EGL surface no longer uses this retained reference.
    if(windowToRelease != nullptr)
    {
        ANativeWindow_release(windowToRelease);
        windowToRelease = nullptr;
    }

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] surface and paired native-window "
        "reference released successfully. surface=%p\r\n",
        surfaceToDestroy);
}

void CGSH_OpenGLAndroid::ReleaseImpl()
{
        const auto cleanupResult =
    CDeferredEglResourceCleanupManager::Drain();

    FRAMEWORK_MAYBE_UNUSED cleanupResult;
	
    // ---------------------------------------------------------
    // 1. CLEAR GRAPHICS DIAGNOSTICS
    // ---------------------------------------------------------

    ClearGraphicsDiagnostics();

    // ---------------------------------------------------------
    // 2. RELEASE OPENGL RESOURCES
    // ---------------------------------------------------------

    if(m_rendererInitialized)
    {
        // The EGL context must remain available while
        // the OpenGL renderer releases its resources.
        CGSH_OpenGL::ReleaseImpl();

        m_rendererInitialized = false;
    }

    // ---------------------------------------------------------
    // 3. VALIDATE DISPLAY
    // ---------------------------------------------------------

    if(m_display == EGL_NO_DISPLAY)
    {
        // Without a valid display, EGL handles cannot be safely
        // destroyed through the normal cleanup path.
        m_surfaceRecoveryPending = true;

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] release deferred: "
            "EGL display is unavailable. "
            "context=%p surface=%p retiredSurfaces=%u\r\n",
            m_context,
            m_surface,
            static_cast<unsigned int>(m_retiredSurfaces.size()));

        return;
    }

    // ---------------------------------------------------------
    // 4. DESTROY ACTIVE SURFACE
    // ---------------------------------------------------------

    DestroySurface();

    // ---------------------------------------------------------
    // 5. RETRY RETIRED SURFACES
    // ---------------------------------------------------------

    CleanupRetiredSurfaces();

    const bool allSurfacesDestroyed =
        m_surface == EGL_NO_SURFACE &&
        m_surfaceWindow == nullptr &&
        m_retiredSurfaces.empty();

    if(!allSurfacesDestroyed)
    {
        // Do not destroy the context or release the display
        // while surfaces remain uncleaned.
        m_surfaceRecoveryPending = true;

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] context cleanup deferred: "
            "one or more EGL surfaces remain alive. "
            "surface=%p retiredSurfaces=%u "
            "context=%p display=%p\r\n",
            m_surface,
            static_cast<unsigned int>(m_retiredSurfaces.size()),
            m_context,
            m_display);

        return;
    }

    // ---------------------------------------------------------
    // 6. DESTROY EGL CONTEXT
    // ---------------------------------------------------------

    if(m_context != EGL_NO_CONTEXT)
    {
        EGLint destroyError = EGL_SUCCESS;

        if(!CEglContextManager::Destroy(
               m_display,
               &m_context,
               &destroyError))
        {
            CLog::GetInstance().Print(
                "glesandroid",
                "[ORBIT2SX_EGL] context destruction failed; "
                "context handle retained. "
                "error=0x%04X context=%p display=%p\r\n",
                destroyError,
                m_context,
                m_display);

            m_surfaceRecoveryPending = true;
            return;
        }
    }

    // ---------------------------------------------------------
    // 7. RELEASE EGL DISPLAY LEASE
    // ---------------------------------------------------------

    if(m_context == EGL_NO_CONTEXT &&
       m_surface == EGL_NO_SURFACE &&
       m_surfaceWindow == nullptr &&
       m_retiredSurfaces.empty())
    {
        ReleaseEglDisplay();
    }

    // ---------------------------------------------------------
    // 8. RESET CONFIGURATION ONLY AFTER DISPLAY RELEASE
    // ---------------------------------------------------------

    if(m_display == EGL_NO_DISPLAY)
    {
        m_config = nullptr;
        m_nativeVisualId = 0;
    }

    // ---------------------------------------------------------
    // 9. UPDATE RECOVERY STATE
    // ---------------------------------------------------------

    m_surfaceRecoveryPending =
        m_display != EGL_NO_DISPLAY ||
        m_context != EGL_NO_CONTEXT ||
        m_surface != EGL_NO_SURFACE ||
        m_surfaceWindow != nullptr ||
        !m_retiredSurfaces.empty();

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] release completed "
        "display=%p context=%p surface=%p surfaceWindow=%p "
        "retiredSurfaces=%u config=%p "
        "remainingDisplayReferences=%u recoveryPending=%u\r\n",
        m_display,
        m_context,
        m_surface,
        m_surfaceWindow,
        static_cast<unsigned int>(m_retiredSurfaces.size()),
        m_config,
        CEglDisplayManager::GetReferenceCount(),
        m_surfaceRecoveryPending ? 1 : 0);
}

void CGSH_OpenGLAndroid::RecoverEglSession()
{
    // ---------------------------------------------------------
    // 1. REENTRANCY GUARD
    // ---------------------------------------------------------
    // Jangan izinkan proses recovery berjalan bersamaan atau
    // dipanggil kembali ketika recovery sebelumnya belum selesai.
    if(m_contextRecoveryInProgress)
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] recovery skipped: "
            "another recovery is already in progress.\r\n");

        return;
    }

    // Aktifkan guard sebelum operasi EGL atau OpenGL dilakukan.
    m_contextRecoveryInProgress = true;

    // Pastikan guard selalu dilepas, termasuk jika terjadi
    // exception selama proses recovery.
    struct RecoveryGuard
    {
        bool& recoveryFlag;

        explicit RecoveryGuard(bool& flag)
            : recoveryFlag(flag)
        {
        }

        ~RecoveryGuard()
        {
            recoveryFlag = false;
        }
    };

    RecoveryGuard recoveryGuard(m_contextRecoveryInProgress);

    // ---------------------------------------------------------
    // 2. CAPTURE INITIAL STATE
    // ---------------------------------------------------------
    const bool hadNativeWindow = (m_window != nullptr);
    const bool hadRenderer = m_rendererInitialized;

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] recovery started "
        "renderer=%u display=%p context=%p "
        "surface=%p window=%p\r\n",
        hadRenderer ? 1 : 0,
        m_display,
        m_context,
        m_surface,
        m_window);

    // Selama recovery, presentation harus dihentikan.
    m_surfaceRecoveryPending = true;

    // ---------------------------------------------------------
    // 3. RELEASE OLD EGL SESSION
    // ---------------------------------------------------------
    // ReleaseImpl melakukan:
    // - membersihkan diagnostics;
    // - melepas resource OpenGL;
    // - menghancurkan EGL surface;
    // - menghancurkan EGL context;
    // - menghentikan EGL display.
    //
    // ANativeWindow tidak dilepas oleh ReleaseImpl.
    // Referensi window tetap dimiliki oleh objek ini.
    ReleaseImpl();

    // ---------------------------------------------------------
    // 4. VERIFY OLD SESSION WAS RELEASED
    // ---------------------------------------------------------
    const bool oldSessionReleased =
        m_display == EGL_NO_DISPLAY &&
        m_context == EGL_NO_CONTEXT &&
        m_surface == EGL_NO_SURFACE &&
        m_surfaceWindow == nullptr &&
        m_retiredSurfaces.empty() &&
        m_config == nullptr &&
        !m_rendererInitialized;

    if(!oldSessionReleased)
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] recovery aborted: "
            "old EGL session was not fully released. "
            "display=%p context=%p surface=%p "
            "config=%p renderer=%u\r\n",
            m_display,
            m_context,
            m_surface,
            m_config,
            m_rendererInitialized ? 1 : 0);

        m_surfaceRecoveryPending = true;
        return;
    }

    // ---------------------------------------------------------
    // 5. VALIDATE NATIVE WINDOW
    // ---------------------------------------------------------
    // EGL session dapat dibuat ulang hanya jika Android masih
    // menyediakan ANativeWindow yang valid dan dimiliki objek.
    if(!hadNativeWindow || m_window == nullptr)
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] recovery deferred: "
            "native window is unavailable. "
            "Waiting for SetWindow().\r\n");

        m_surfaceRecoveryPending = true;
        return;
    }

    // ---------------------------------------------------------
    // 6. REINITIALIZE EGL AND OPENGL
    // ---------------------------------------------------------
    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] creating replacement EGL session.\r\n");

    InitializeImpl();

    // ---------------------------------------------------------
    // 7. VALIDATE RECOVERED SESSION
    // ---------------------------------------------------------
    const bool recovered =
        m_rendererInitialized &&
        m_display != EGL_NO_DISPLAY &&
        m_context != EGL_NO_CONTEXT &&
        m_surface != EGL_NO_SURFACE &&
        m_config != nullptr &&
        m_window != nullptr &&
        !m_surfaceRecoveryPending;

    // ---------------------------------------------------------
    // 8. FAILED RECOVERY CLEANUP
    // ---------------------------------------------------------
    if(!recovered)
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] replacement session validation failed. "
            "Cleaning up incomplete EGL state.\r\n");

        // Hanya membersihkan state EGL yang mungkin masih tersisa.
        // ReleaseImpl juga memastikan flag surface recovery aktif.
        ReleaseImpl();

        m_surfaceRecoveryPending = true;

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] recovery failed. "
            "display=%p context=%p surface=%p "
            "config=%p renderer=%u window=%p\r\n",
            m_display,
            m_context,
            m_surface,
            m_config,
            m_rendererInitialized ? 1 : 0,
            m_window);

        return;
    }

    // ---------------------------------------------------------
    // 9. RECOVERY SUCCESS
    // ---------------------------------------------------------
    // SetupContext() sudah mengaktifkan surface dan memperbarui
    // presentation parameters. Jangan mengubahnya lagi di sini.
    m_surfaceRecoveryPending = false;

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] recovery successful. "
        "previousRenderer=%u "
        "display=%p context=%p surface=%p "
        "config=%p window=%p\r\n",
        hadRenderer ? 1 : 0,
        m_display,
        m_context,
        m_surface,
        m_config,
        m_window);
}

void CGSH_OpenGLAndroid::PresentBackbuffer()
{
    if(m_contextRecoveryInProgress ||
       m_display == EGL_NO_DISPLAY ||
       m_context == EGL_NO_CONTEXT ||
       m_surface == EGL_NO_SURFACE ||
       m_surfaceRecoveryPending)
    {
       return;
	}    

    if(eglSwapBuffers(m_display, m_surface) == EGL_TRUE)
    {
        return;
    }

    // eglGetError must be read immediately after the failed EGL call.
    const EGLint error = eglGetError();

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] eglSwapBuffers failed "
        "error=0x%04X display=%p context=%p "
        "surface=%p window=%p\r\n",
        error,
        m_display,
        m_context,
        m_surface,
        m_window);

    switch(error)
    {
    case EGL_BAD_SURFACE:
    case EGL_BAD_CURRENT_SURFACE:
    {
        // Try to replace a damaged surface while retaining the context.
        if(m_window != nullptr && SetupContext())
        {
            return;
        }

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] surface recovery failed; "
            "destroying stale surface and waiting for window update.\r\n");

        DestroySurface();
        return;
    }

    case EGL_BAD_NATIVE_WINDOW:
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] native window is invalid; "
            "waiting for a replacement window.\r\n");

        DestroySurface();
        return;
    }

    case EGL_CONTEXT_LOST:
    case EGL_BAD_CONTEXT:
    case EGL_BAD_DISPLAY:
    {
        // These errors invalidate more than the window surface.
        // Rebuild the complete EGL session and the GL renderer.
        RecoverEglSession();
        return;
    }

    case EGL_BAD_MATCH:
    case EGL_BAD_PARAMETER:
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] unrecoverable swap configuration error "
            "error=0x%04X; presentation suspended.\r\n",
            error);

        DestroySurface();
        return;
    }

    default:
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] unhandled eglSwapBuffers error "
            "error=0x%04X; frame skipped.\r\n",
            error);
        return;
    }
    }
}
          
void CGSH_OpenGLAndroid::SetWindow(NativeWindowType window)
{
    SendGSCall(
        [this, window]() {
            // Every non-null call transfers exactly one ANativeWindow
            // reference to this listener.

            if(window == m_window)
            {
                // The incoming reference is additional, even when
                // the native pointer is identical.
                if(window != nullptr)
                {
                    if(m_display == EGL_NO_DISPLAY)
                    {
                        InitializeImpl();
                    }
                    else if(
                        m_surface == EGL_NO_SURFACE ||
                        m_surfaceRecoveryPending)
                    {
                        if(!SetupContext())
                        {
                            DestroySurface();
                        }
                    }

                    ANativeWindow_release(window);
                }

                return;
            }

            NativeWindowType previousWindow = m_window;

            // Adopt the new reference before attempting surface creation.
            m_window = window;

            if(m_window == nullptr)
            {
                // SurfaceHolder.surfaceDestroyed().
                // Keep the EGL context alive but stop presentation.
                DestroySurface();
            }
            else if(m_display == EGL_NO_DISPLAY)
            {
                InitializeImpl();
            }
            else
            {
                if(!SetupContext())
                {
                    CLog::GetInstance().Print(
                        "glesandroid",
                        "[ORBIT2SX_EGL] replacement window setup failed; "
                        "discarding stale surface.\r\n");

                    DestroySurface();
                }
            }

            // Release the previous window only after the replacement
            // surface has either succeeded or the stale surface is gone.
            if(previousWindow != nullptr)
            {
                ANativeWindow_release(previousWindow);
            }
        },
        true);
}
