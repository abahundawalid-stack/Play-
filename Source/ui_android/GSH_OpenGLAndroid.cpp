#include <cassert>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <memory>
#include <string>

#include "opengl/OpenGlDef.h"
#include "GSH_OpenGLAndroid.h"
#include "AppConfig.h"
#include "NativeShared.h"
#include "Log.h"

CGSH_OpenGLAndroid::CGSH_OpenGLAndroid(NativeWindowType window)
    : m_window(window)
{
}

CGSH_OpenGLAndroid::~CGSH_OpenGLAndroid()
{
	if(m_window != nullptr)
	{
		ANativeWindow_release(m_window);
		m_window = nullptr;
	}
}

CGSHandler::FactoryFunction CGSH_OpenGLAndroid::GetFactoryFunction(
    NativeWindowType window)
{
    auto windowOwner = std::shared_ptr<ANativeWindow>(
        window,
        [](ANativeWindow* ownedWindow) {
            if(ownedWindow != nullptr)
            {
                ANativeWindow_release(ownedWindow);
            }
        });

    return [windowOwner]() {
        ANativeWindow* handlerWindow = windowOwner.get();

        if(handlerWindow != nullptr)
        {
            ANativeWindow_acquire(handlerWindow);
        }

        return new CGSH_OpenGLAndroid(handlerWindow);
    };
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

    static const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_NONE
    };

    static const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if(m_display == EGL_NO_DISPLAY)
    {
        EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] eglGetDisplay failed error=0x%04X\r\n",
            error);

        return;
    }

    if(eglInitialize(m_display, nullptr, nullptr) != EGL_TRUE)
    {
        EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] eglInitialize failed error=0x%04X\r\n",
            error);

        m_display = EGL_NO_DISPLAY;
        return;
    }

    EGLint numConfigs = 0;
	
    if(eglChooseConfig(
           m_display, configAttribs, &m_config, 1, &numConfigs) != EGL_TRUE ||
       numConfigs == 0)
    {
        EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] eglChooseConfig failed count=%d error=0x%04X\r\n",
            numConfigs,
            error);

        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        m_config = nullptr;
        return;
    }

    m_context = eglCreateContext(
        m_display, m_config, EGL_NO_CONTEXT, contextAttribs);

    if(m_context == EGL_NO_CONTEXT)
    {
        EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] eglCreateContext failed error=0x%04X\r\n",
            error);

        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        m_config = nullptr;
        return;
    }

    if(!SetupContext())
    {
		CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] SetupContext failed during initialization.\r\n");
    
		eglDestroyContext(m_display, m_context);
        m_context = EGL_NO_CONTEXT;

        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        m_config = nullptr;
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
    ClearGraphicsDiagnostics();

    // Release OpenGL renderer resources while the renderer still owns
    // its EGL session. This resets GL wrapper objects and caches.
    if(m_rendererInitialized)
    {
        CGSH_OpenGL::ReleaseImpl();
        m_rendererInitialized = false;
    }

    if(m_display != EGL_NO_DISPLAY)
    {
        // DestroySurface unbinds this context when it is current.
        DestroySurface();

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

        if(eglTerminate(m_display) != EGL_TRUE)
        {
            const EGLint error = eglGetError();

            CLog::GetInstance().Print(
                "glesandroid",
                "[ORBIT2SX_EGL] eglTerminate failed "
                "error=0x%04X display=%p\r\n",
                error,
                m_display);
        }

        m_display = EGL_NO_DISPLAY;
    }
    else
    {
        m_surface = EGL_NO_SURFACE;
        m_context = EGL_NO_CONTEXT;
    }

    m_config = nullptr;
    m_surfaceRecoveryPending = true;
}

void CGSH_OpenGLAndroid::RecoverEglSession()
{
    if(m_contextRecoveryInProgress)
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] recovery already in progress.\r\n");
        return;
    }

    m_contextRecoveryInProgress = true;

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] rebuilding EGL session.\r\n");

    // Reset renderer wrappers and destroy the old EGL session.
    ReleaseImpl();

    // A destroyed Android surface cannot be recreated until
    // NativeInterop supplies a new ANativeWindow reference.
    if(m_window != nullptr)
    {
        InitializeImpl();
    }
    else
    {
        m_surfaceRecoveryPending = true;
    }

    const bool recovered =
        m_rendererInitialized &&
        m_display != EGL_NO_DISPLAY &&
        m_context != EGL_NO_CONTEXT &&
        m_surface != EGL_NO_SURFACE;

    m_contextRecoveryInProgress = false;

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] EGL recovery %s "
        "display=%p context=%p surface=%p window=%p\r\n",
        recovered ? "successful" : "failed",
        m_display,
        m_context,
        m_surface,
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
