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
    // ---------------------------------------------------------
    // 1. RELEASE GRAPHICS RESOURCES ON THE GS EXECUTION THREAD
    // ---------------------------------------------------------
    // Release() dispatches ReleaseImpl() through the GS handler's
    // synchronization mechanism and waits for completion.
    //
    // This ensures that OpenGL resources, EGL surface, EGL context,
    // and EGL display are released before the native window reference.
    //
    // ReleaseImpl() is designed to tolerate an already released
    // or partially initialized EGL session.
    Release();

    // ---------------------------------------------------------
    // 2. DETACH THE OWNED NATIVE WINDOW REFERENCE
    // ---------------------------------------------------------
    NativeWindowType windowToRelease = m_window;

    // Clear the member before releasing the reference.
    // This prevents accidental reuse of a reference that is
    // no longer owned by this renderer.
    m_window = nullptr;

    // ---------------------------------------------------------
    // 3. RELEASE THE NATIVE WINDOW EXACTLY ONCE
    // ---------------------------------------------------------
    if(windowToRelease != nullptr)
    {
        ANativeWindow_release(windowToRelease);
        windowToRelease = nullptr;
    }

    // No EGL or OpenGL operation should be performed after this point.
}

CGSHandler::FactoryFunction CGSH_OpenGLAndroid::GetFactoryFunction(
    NativeWindowType window)
{
    // ---------------------------------------------------------
    // 1. DEFINE NATIVE WINDOW OWNERSHIP
    // ---------------------------------------------------------
    // The incoming reference is transferred to this factory.
    //
    // The shared_ptr owns that original reference and guarantees
    // that it remains alive while any copy of the factory exists.
    //
    // Its custom deleter releases the reference exactly once,
    // when the final shared owner is destroyed.

    auto releaseNativeWindow = [](ANativeWindow* ownedWindow) noexcept
    {
        if(ownedWindow != nullptr)
        {
            ANativeWindow_release(ownedWindow);
        }
    };

    std::shared_ptr<ANativeWindow> windowOwner;

    if(window != nullptr)
    {
        windowOwner = std::shared_ptr<ANativeWindow>(
            window,
            releaseNativeWindow);
    }

    // ---------------------------------------------------------
    // 2. RETURN A COPYABLE, OWNERSHIP-SAFE FACTORY
    // ---------------------------------------------------------
    // Capturing shared_ptr makes the factory safe to copy and
    // keeps the original native window reference alive until
    // the factory and all its copies have released ownership.

    return [windowOwner, releaseNativeWindow]() -> CGSHandler*
    {
        // -----------------------------------------------------
        // 3. HANDLE DETACHED WINDOW
        // -----------------------------------------------------
        // A null window is valid. The renderer can be created
        // without a surface and wait for SetWindow() later.

        if(!windowOwner)
        {
            return new CGSH_OpenGLAndroid(nullptr);
        }

        ANativeWindow* handlerWindow = windowOwner.get();

        // -----------------------------------------------------
        // 4. ACQUIRE AN INDEPENDENT REFERENCE FOR THE RENDERER
        // -----------------------------------------------------
        // The renderer must own its own reference because its
        // lifetime is independent from the factory's lifetime.

        ANativeWindow_acquire(handlerWindow);

        // Keep the acquired reference under temporary RAII
        // ownership until renderer construction succeeds.
        std::unique_ptr<ANativeWindow, decltype(releaseNativeWindow)>
            handlerWindowOwner(
                handlerWindow,
                releaseNativeWindow);

        // -----------------------------------------------------
        // 5. CONSTRUCT RENDERER
        // -----------------------------------------------------
        // CGSH_OpenGLAndroid takes ownership of the transferred
        // reference. If construction throws, unique_ptr releases
        // the reference automatically.

        auto* renderer = new CGSH_OpenGLAndroid(
            handlerWindowOwner.get());

        // -----------------------------------------------------
        // 6. TRANSFER OWNERSHIP TO THE RENDERER
        // -----------------------------------------------------
        // The renderer destructor is now responsible for releasing
        // this reference. Prevent the temporary unique_ptr from
        // releasing it a second time.

        handlerWindowOwner.release();

        return renderer;
    };
}

void CGSH_OpenGLAndroid::ReleaseEglDisplay()
{
    if(m_display == EGL_NO_DISPLAY)
    {
        return;
    }

    // Preserve the exact handle associated with this lease.
    const EGLDisplay displayToRelease = m_display;

    // Clear the renderer's handle before releasing the lease.
    // This prevents accidental duplicate Release() calls.
    m_display = EGL_NO_DISPLAY;

    EGLint error = EGL_SUCCESS;

    if(!CEglDisplayManager::Release(displayToRelease, &error))
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] shared display release failed "
            "error=0x%04X display=%p references=%u\r\n",
            error,
            displayToRelease,
            CEglDisplayManager::GetReferenceCount());
    }
    else
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] shared display lease released "
            "display=%p remainingReferences=%u\r\n",
            displayToRelease,
            CEglDisplayManager::GetReferenceCount());
    }
}

void CGSH_OpenGLAndroid::InitializeImpl()
{ 
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

    static const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    
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

	if(eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE)
    {
        const EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] eglBindAPI failed "
            "error=0x%04X\r\n",
            error);

        m_config = nullptr;
        m_nativeVisualId = 0;

        ReleaseEglDisplay();

        m_surfaceRecoveryPending = true;
        return;
    }

    // ---------------------------------------------------------
    // CREATE OPENGL ES 3 EGL CONTEXT
    // ---------------------------------------------------------

    m_context = eglCreateContext(
        m_display,
        m_config,
        EGL_NO_CONTEXT,
        contextAttribs);

    if(m_context == EGL_NO_CONTEXT)
    {
        const EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] eglCreateContext failed "
            "error=0x%04X display=%p config=%p\r\n",
            error,
            m_display,
            m_config);

        m_context = EGL_NO_CONTEXT;
        m_config = nullptr;
        m_nativeVisualId = 0;

        ReleaseEglDisplay();

        m_surfaceRecoveryPending = true;
        return;
    }

    if(!SetupContext())
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] SetupContext failed during initialization.\r\n");

        if(m_context != EGL_NO_CONTEXT)
        {
            if(eglDestroyContext(m_display, m_context) != EGL_TRUE)
            {
                const EGLint error = eglGetError();

                CLog::GetInstance().Print(
                    "glesandroid",
                    "[ORBIT2SX_EGL] eglDestroyContext after "
                    "initialization failure failed "
                    "error=0x%04X context=%p\r\n",
                    error,
                    m_context);
            }

            m_context = EGL_NO_CONTEXT;
        }

        m_config = nullptr;
        ReleaseEglDisplay();

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

    // ---------------------------------------------------------
    // MATCH ANDROID NATIVE WINDOW FORMAT WITH EGL CONFIG
    // ---------------------------------------------------------

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

        m_surfaceRecoveryPending = true;
        return false;
    }
	
    // Create a replacement surface without destroying the old one.
    EGLSurface newSurface = eglCreateWindowSurface(
        m_display,
        m_config,
        m_window,
        nullptr);

    if(newSurface == EGL_NO_SURFACE)
    {
        const EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] eglCreateWindowSurface failed "
            "error=0x%04X window=%p\r\n",
            error,
            m_window);

        return failRecovery();
    }

    // Query dimensions before switching the current EGL surface.
    EGLint width = 0;
    EGLint height = 0;

    if(eglQuerySurface(
           m_display, newSurface, EGL_WIDTH, &width) != EGL_TRUE ||
       eglQuerySurface(
           m_display, newSurface, EGL_HEIGHT, &height) != EGL_TRUE ||
       width <= 0 ||
       height <= 0)
    {
        const EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] replacement surface has invalid dimensions "
            "width=%d height=%d error=0x%04X\r\n",
            width,
            height,
            error);

        eglDestroySurface(m_display, newSurface);
        return failRecovery();
    }

    // Switch to the new surface only after it has been created and queried.
    if(eglMakeCurrent(
           m_display,
           newSurface,
           newSurface,
           m_context) != EGL_TRUE)
    {
        const EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] eglMakeCurrent replacement failed "
            "error=0x%04X\r\n",
            error);

        // The previous current surface remains the active one when
        // eglMakeCurrent fails. Destroy only the unbound new surface.
        eglDestroySurface(m_display, newSurface);
        return failRecovery();
    }

    EGLSurface previousSurface = m_surface;
    m_surface = newSurface;

    // The new surface is now current. The old surface can be destroyed.
    if(previousSurface != EGL_NO_SURFACE &&
       previousSurface != newSurface)
    {
        if(eglDestroySurface(m_display, previousSurface) != EGL_TRUE)
        {
            const EGLint error = eglGetError();

            CLog::GetInstance().Print(
                "glesandroid",
                "[ORBIT2SX_EGL] old surface destruction failed "
                "error=0x%04X old=%p\r\n",
                error,
                previousSurface);
        }
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
        "width=%d height=%d\r\n",
        m_display,
        m_context,
        m_surface,
        m_window,
        width,
        height);

    return true;
}

void CGSH_OpenGLAndroid::DestroySurface()
{
    m_surfaceRecoveryPending = true;

    if(m_surface == EGL_NO_SURFACE)
    {
        return;
    }

    EGLSurface surfaceToDestroy = m_surface;

    // Clear the member first so no later frame can reuse this handle.
    m_surface = EGL_NO_SURFACE;

    if(m_display == EGL_NO_DISPLAY)
    {
        return;
    }

    // Unbind only when this renderer's context is current.
    if(eglGetCurrentContext() == m_context)
    {
        if(eglMakeCurrent(
               m_display,
               EGL_NO_SURFACE,
               EGL_NO_SURFACE,
               EGL_NO_CONTEXT) != EGL_TRUE)
        {
            const EGLint error = eglGetError();

            CLog::GetInstance().Print(
                "glesandroid",
                "[ORBIT2SX_EGL] surface detach failed "
                "error=0x%04X\r\n",
                error);
        }
    }

    if(eglDestroySurface(m_display, surfaceToDestroy) != EGL_TRUE)
    {
        const EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] eglDestroySurface failed "
            "error=0x%04X surface=%p\r\n",
            error,
            surfaceToDestroy);
    }
}

void CGSH_OpenGLAndroid::ReleaseImpl()
{
    // ---------------------------------------------------------
    // 1. CLEAR GRAPHICS DIAGNOSTICS
    // ---------------------------------------------------------
    ClearGraphicsDiagnostics();

    // ---------------------------------------------------------
    // 2. RELEASE OPENGL RESOURCES FIRST
    // ---------------------------------------------------------
    // The EGL context must remain valid while CGSH_OpenGL
    // releases its GL objects, caches, shaders and buffers.
    if(m_rendererInitialized)
    {
        CGSH_OpenGL::ReleaseImpl();
        m_rendererInitialized = false;
    }

    // ---------------------------------------------------------
    // 3. RELEASE EGL SURFACE
    // ---------------------------------------------------------
    if(m_display != EGL_NO_DISPLAY)
    {
        DestroySurface();

        // -----------------------------------------------------
        // 4. RELEASE EGL CONTEXT
        // -----------------------------------------------------
        if(m_context != EGL_NO_CONTEXT)
        {
            if(eglDestroyContext(m_display, m_context) != EGL_TRUE)
            {
                const EGLint error = eglGetError();

                CLog::GetInstance().Print(
                    "glesandroid",
                    "[ORBIT2SX_EGL] eglDestroyContext failed "
                    "error=0x%04X context=%p\r\n",
                    error,
                    m_context);
            }

            m_context = EGL_NO_CONTEXT;
        }

        // -----------------------------------------------------
        // 5. RELEASE ONE SHARED DISPLAY LEASE
        // -----------------------------------------------------
        // eglTerminate() is now owned exclusively by
        // CEglDisplayManager.
        //
        // If another renderer still holds a lease, the display
        // remains initialized.
        //
        // Only the final Release() terminates the display.
        ReleaseEglDisplay();
    }
    else
    {
        // No display lease exists. Clear stale local handles.
        m_surface = EGL_NO_SURFACE;
        m_context = EGL_NO_CONTEXT;
    }

    // ---------------------------------------------------------
    // 6. RESET LOCAL EGL CONFIGURATION
    // ---------------------------------------------------------
    m_config = nullptr;
    m_nativeVisualId = 0;

    // ---------------------------------------------------------
    // 7. MARK SURFACE AS REQUIRING REINITIALIZATION
    // ---------------------------------------------------------
    m_surfaceRecoveryPending = true;

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] renderer EGL resources released "
        "display=%p context=%p surface=%p "
        "remainingDisplayReferences=%u\r\n",
        m_display,
        m_context,
        m_surface,
        CEglDisplayManager::GetReferenceCount());
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
