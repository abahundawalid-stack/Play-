#include <cassert>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <memory>

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
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] InitializeImpl aborted: native window is null.\r\n");

        return;
    }

    if(m_display != EGL_NO_DISPLAY)
    {
        return;
    }

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
       m_window == nullptr)
    {
        return failRecovery();
    }

    // Buat surface baru terlebih dahulu.
    // Jangan hancurkan surface lama sebelum surface baru valid.
    EGLSurface newSurface = eglCreateWindowSurface(
        m_display,
        m_config,
        m_window,
        nullptr);

    if(newSurface == EGL_NO_SURFACE)
    {
        EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] create replacement surface failed "
            "error=0x%04X window=%p\r\n",
            error,
            m_window);

        return failRecovery();
    }

    // Aktifkan surface baru menggunakan context yang sama.
    if(eglMakeCurrent(
           m_display,
           newSurface,
           newSurface,
           m_context) != EGL_TRUE)
    {
        EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] make replacement surface current failed "
            "error=0x%04X\r\n",
            error);

        eglDestroySurface(m_display, newSurface);
        return failRecovery();
    }

    EGLint width = 0;
    EGLint height = 0;

    if(eglQuerySurface(
           m_display, newSurface, EGL_WIDTH, &width) != EGL_TRUE ||
       eglQuerySurface(
           m_display, newSurface, EGL_HEIGHT, &height) != EGL_TRUE ||
       width <= 0 ||
       height <= 0)
    {
        EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] replacement surface dimensions invalid "
            "width=%d height=%d error=0x%04X\r\n",
            width,
            height,
            error);

        // Kembalikan context ke surface lama jika masih tersedia.
        if(m_surface != EGL_NO_SURFACE)
        {
            eglMakeCurrent(
                m_display,
                m_surface,
                m_surface,
                m_context);
        }
        else
        {
            eglMakeCurrent(
                m_display,
                EGL_NO_SURFACE,
                EGL_NO_SURFACE,
                EGL_NO_CONTEXT);
        }

        eglDestroySurface(m_display, newSurface);
        return failRecovery();
    }

    EGLSurface previousSurface = m_surface;
    m_surface = newSurface;

    // Surface baru sudah aktif. Sekarang surface lama aman dihancurkan.
    if(previousSurface != EGL_NO_SURFACE &&
       previousSurface != newSurface)
    {
        eglDestroySurface(m_display, previousSurface);
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

    PRESENTATION_PARAMS presentationParams;

    presentationParams.mode =
        static_cast<CGSHandler::PRESENTATION_MODE>(
            CAppConfig::GetInstance().GetPreferenceInteger(
                PREF_CGSHANDLER_PRESENTATION_MODE));

    presentationParams.windowWidth = width;
    presentationParams.windowHeight = height;

    SetPresentationParams(presentationParams);

    if(eglSwapInterval(m_display, 0) != EGL_TRUE)
    {
        EGLint error = eglGetError();

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] swap interval configuration failed "
            "error=0x%04X\r\n",
            error);
    }

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] replacement surface ready "
        "width=%d height=%d context=%p surface=%p\r\n",
        width,
        height,
        m_context,
        m_surface);

	// Surface and context are valid and ready for rendering.
    m_surfaceRecoveryPending = false;

    CLog::GetInstance().Print(
        "glesandroid",
        "[ORBIT2SX_EGL] SetupContext completed successfully "
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

void CGSH_OpenGLAndroid::ReleaseImpl()
{
	    ClearGraphicsDiagnostics();
	
    if(m_rendererInitialized)
    {
        CGSH_OpenGL::ReleaseImpl();
        m_rendererInitialized = false;
    }

    if(m_display != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(
            m_display,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT);

        if(m_surface != EGL_NO_SURFACE)
        {
            eglDestroySurface(m_display, m_surface);
            m_surface = EGL_NO_SURFACE;
        }

        if(m_context != EGL_NO_CONTEXT)
        {
            eglDestroyContext(m_display, m_context);
            m_context = EGL_NO_CONTEXT;
        }

        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
    }

    m_config = 0;
}

void CGSH_OpenGLAndroid::PresentBackbuffer()
{
    if(m_display == EGL_NO_DISPLAY ||
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
    {
        // Surface rusak, tetapi context belum dinyatakan hilang.
        // Coba buat surface pengganti menggunakan context yang sama.
        if(m_window != nullptr)
        {
            if(!SetupContext())
            {
                CLog::GetInstance().Print(
                    "glesandroid",
                    "[ORBIT2SX_EGL] surface recovery failed; "
                    "waiting for next window update.\r\n");
            }
        }
        break;
    }

    case EGL_BAD_NATIVE_WINDOW:
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] native window invalid; "
            "invalidating surface and waiting for replacement.\r\n");

        if(m_display != EGL_NO_DISPLAY &&
           m_surface != EGL_NO_SURFACE)
        {
            // Detach the invalid surface from the current context.
            eglMakeCurrent(
                m_display,
                EGL_NO_SURFACE,
                EGL_NO_SURFACE,
                EGL_NO_CONTEXT);

            EGLSurface invalidSurface = m_surface;

            // Clear the handle before attempting destruction.
            // This prevents subsequent frames from reusing it.
            m_surface = EGL_NO_SURFACE;

            eglDestroySurface(
                m_display,
                invalidSurface);
        }

        m_surfaceRecoveryPending = true;

        break;
    }

    case EGL_CONTEXT_LOST:
    {
        if(m_contextRecoveryInProgress)
        {
            CLog::GetInstance().Print(
                "glesandroid",
                "[ORBIT2SX_EGL] context recovery already in progress.\r\n");
            return;
        }

        if(m_window == nullptr)
        {
            CLog::GetInstance().Print(
                "glesandroid",
                "[ORBIT2SX_EGL] context lost without native window; "
                "recovery deferred.\r\n");
            return;
        }

        m_contextRecoveryInProgress = true;

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] context lost; rebuilding renderer.\r\n");

        // ReleaseImpl melepas resource renderer, surface,
        // context, dan display EGL.
        ReleaseImpl();

        // InitializeImpl membuat context dan surface baru,
        // kemudian menginisialisasi ulang renderer OpenGL.
        InitializeImpl();

        const bool recovered =
            m_rendererInitialized &&
            m_display != EGL_NO_DISPLAY &&
            m_context != EGL_NO_CONTEXT &&
            m_surface != EGL_NO_SURFACE;

        m_contextRecoveryInProgress = false;

        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] context recovery %s "
            "display=%p context=%p surface=%p\r\n",
            recovered ? "successful" : "failed",
            m_display,
            m_context,
            m_surface);

        break;
    }

    case EGL_BAD_DISPLAY:
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] invalid EGL display; "
            "renderer recovery required.\r\n");
        break;
    }

    default:
    {
        CLog::GetInstance().Print(
            "glesandroid",
            "[ORBIT2SX_EGL] unhandled presentation error "
            "0x%04X.\r\n",
            error);
        break;
    }
    }
}

void CGSH_OpenGLAndroid::SetWindow(NativeWindowType window)
{
    SendGSCall(
        [this, window]() {
            // Setiap pemanggilan mentransfer satu reference.
            // Pointer sama tidak berarti reference baru boleh diabaikan:
            // reference baru harus dilepas setelah retry yang diperlukan.
            if(window == m_window)
            {
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
                        SetupContext();
                    }

                    ANativeWindow_release(window);
                }

                return;
			}
            NativeWindowType previousWindow = m_window;
            m_window = window;

            if(m_display == EGL_NO_DISPLAY)
            {
                if(m_window != nullptr)
                {
                    InitializeImpl();
                }
            }
            else if(m_window != nullptr)
            {
                if(!SetupContext())
                {
                    m_surfaceRecoveryPending = true;

                    CLog::GetInstance().Print(
                        "glesandroid",
                        "[ORBIT2SX_EGL] replacement window setup failed; "
                        "recovery remains pending.\r\n");
                }
            }
            else if(m_surface != EGL_NO_SURFACE)
            {
                eglMakeCurrent(
                    m_display,
                    EGL_NO_SURFACE,
                    EGL_NO_SURFACE,
                    EGL_NO_CONTEXT);

                eglDestroySurface(m_display, m_surface);
                m_surface = EGL_NO_SURFACE;
            }

            if(previousWindow != nullptr)
            {
                ANativeWindow_release(previousWindow);
            }
        },
        true);
}
