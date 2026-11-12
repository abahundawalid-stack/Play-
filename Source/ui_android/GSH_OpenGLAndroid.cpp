#include <cassert>
#include <android/native_window.h>
#include <memory>

#include "opengl/OpenGlDef.h"
#include "GSH_OpenGLAndroid.h"
#include "AppConfig.h"

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
    if(m_window == nullptr || m_display != EGL_NO_DISPLAY)
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
        return;
    }

    if(eglInitialize(m_display, nullptr, nullptr) != EGL_TRUE)
    {
        m_display = EGL_NO_DISPLAY;
        return;
    }

    EGLint numConfigs = 0;
    if(eglChooseConfig(
           m_display, configAttribs, &m_config, 1, &numConfigs) != EGL_TRUE ||
       numConfigs == 0)
    {
        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        m_config = nullptr;
        return;
    }

    m_context = eglCreateContext(
        m_display, m_config, EGL_NO_CONTEXT, contextAttribs);

    if(m_context == EGL_NO_CONTEXT)
    {
        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        m_config = nullptr;
        return;
    }

    if(!SetupContext())
    {
        eglDestroyContext(m_display, m_context);
        m_context = EGL_NO_CONTEXT;

        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        m_config = nullptr;
        return;
    }

    CGSH_OpenGL::InitializeImpl();
    m_rendererInitialized = true;
}

bool CGSH_OpenGLAndroid::SetupContext()
{
    if(m_display == EGL_NO_DISPLAY ||
       m_context == EGL_NO_CONTEXT ||
       m_window == nullptr)
    {
        return false;
    }

    if(m_surface != EGL_NO_SURFACE)
    {
        eglMakeCurrent(
            m_display,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT);

        eglDestroySurface(m_display, m_surface);
        m_surface = EGL_NO_SURFACE;
    }

    EGLSurface newSurface = eglCreateWindowSurface(
        m_display,
        m_config,
        m_window,
        nullptr);

    if(newSurface == EGL_NO_SURFACE)
    {
        return false;
    }

    if(eglMakeCurrent(
           m_display,
           newSurface,
           newSurface,
           m_context) != EGL_TRUE)
    {
        eglDestroySurface(m_display, newSurface);
        return false;
    }

    EGLint width = 0;
    EGLint height = 0;

    if(eglQuerySurface(m_display, newSurface, EGL_WIDTH, &width) != EGL_TRUE ||
       eglQuerySurface(m_display, newSurface, EGL_HEIGHT, &height) != EGL_TRUE ||
       width <= 0 || height <= 0)
    {
        eglMakeCurrent(
            m_display,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT);

        eglDestroySurface(m_display, newSurface);
        return false;
    }

    m_surface = newSurface;

    PRESENTATION_PARAMS presentationParams;
    presentationParams.mode =
        static_cast<CGSHandler::PRESENTATION_MODE>(
            CAppConfig::GetInstance().GetPreferenceInteger(
                PREF_CGSHANDLER_PRESENTATION_MODE));

    presentationParams.windowWidth = width;
    presentationParams.windowHeight = height;

    SetPresentationParams(presentationParams);

    eglSwapInterval(m_display, 0);
    return true;
}

void CGSH_OpenGLAndroid::ReleaseImpl()
{
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
	if(m_display == EGL_NO_DISPLAY || m_surface == EGL_NO_SURFACE)
	{
		return;
	}

	eglSwapBuffers(m_display, m_surface);
}

void CGSH_OpenGLAndroid::SetWindow(NativeWindowType window)
{
    SendGSCall(
        [this, window]() {
            // Setiap pemanggilan SetWindow menerima ownership satu
            // reference dari pemanggil, termasuk jika pointer sama.
            //
            // Jika pointer sama, pertahankan reference yang sudah dimiliki
            // handler dan lepaskan reference baru yang dialihkan.
            if(window == m_window)
            {
                if(window != nullptr)
                {
                    ANativeWindow_release(window);
                }
                return;
            }

            NativeWindowType previousWindow = m_window;

            // Ambil alih reference window baru.
            m_window = window;

            if(m_display == EGL_NO_DISPLAY)
            {
                // Surface pertama: buat EGL dan renderer.
                if(m_window != nullptr)
                {
                    InitializeImpl();
                }
            }
            else
            {
                if(m_window != nullptr)
                {
                    // Ganti EGL window surface dengan window baru.
                    SetupContext();
                }
                else
                {
                    // Window dilepas: detach EGL surface.
                    if(m_surface != EGL_NO_SURFACE)
                    {
                        eglMakeCurrent(
                            m_display,
                            EGL_NO_SURFACE,
                            EGL_NO_SURFACE,
                            EGL_NO_CONTEXT);

                        eglDestroySurface(m_display, m_surface);
                        m_surface = EGL_NO_SURFACE;
                    }
                }
            }

            // Lepaskan reference window lama setelah EGL tidak lagi
            // menggunakan surface yang terhubung ke window tersebut.
            if(previousWindow != nullptr)
            {
                ANativeWindow_release(previousWindow);
            }
        },
        true);
}

