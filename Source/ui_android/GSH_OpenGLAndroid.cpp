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

CGSHandler::FactoryFunction CGSH_OpenGLAndroid::GetFactoryFunction(NativeWindowType window)
{
	return [window]() {
		return new CGSH_OpenGLAndroid(window);
	};
}

void CGSH_OpenGLAndroid::InitializeImpl()
{
	if(m_window == nullptr)
	{
		return;
	}

	static const EGLint configAttribs[] =
	    {
	        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	        EGL_BLUE_SIZE, 8,
	        EGL_GREEN_SIZE, 8,
	        EGL_RED_SIZE, 8,
	        EGL_NONE};

	static const EGLint contextAttribs[] =
	    {
	        EGL_CONTEXT_CLIENT_VERSION, 3,
	        EGL_NONE};

	m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	assert(m_display != EGL_NO_DISPLAY);

	EGLBoolean initialized = eglInitialize(m_display, 0, 0);
	assert(initialized != EGL_FALSE);

	EGLint numConfigs = 0;
	EGLBoolean chosen = eglChooseConfig(
	    m_display,
	    configAttribs,
	    &m_config,
	    1,
	    &numConfigs);

	assert(chosen != EGL_FALSE);
	assert(numConfigs > 0);

	m_context = eglCreateContext(
	    m_display,
	    m_config,
	    EGL_NO_CONTEXT,
	    contextAttribs);

	assert(m_context != EGL_NO_CONTEXT);

	SetupContext();

	CGSH_OpenGL::InitializeImpl();
}

void CGSH_OpenGLAndroid::ReleaseImpl()
{
	// Release the base renderer while its EGL context still exists.
	CGSH_OpenGL::ReleaseImpl();

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
    // Ownership of window transfers to this handler.
    // nullptr is valid and means detach the current surface.

    SendGSCall(
        [this, window]() {
            auto previousWindow = m_window;
            m_window = window;

            // Destroys the old EGL surface before releasing its window.
            // Also handles nullptr by detaching the EGL surface.
            SetupContext();

            if(previousWindow != nullptr)
            {
                ANativeWindow_release(previousWindow);
            }
        },
        true);
}

void CGSH_OpenGLAndroid::SetupContext()
{
    if(m_display == EGL_NO_DISPLAY)
    {
        return;
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

    // A null window means the Android surface was detached.
    if(m_context == EGL_NO_CONTEXT || m_window == nullptr)
    {
        return;
    }

    m_surface = eglCreateWindowSurface(
        m_display,
        m_config,
        m_window,
        nullptr);

    assert(m_surface != EGL_NO_SURFACE);

    auto makeCurrentResult = eglMakeCurrent(
        m_display,
        m_surface,
        m_surface,
        m_context);

    assert(makeCurrentResult != EGL_FALSE);

    GLint w = 0;
    GLint h = 0;

    eglQuerySurface(m_display, m_surface, EGL_WIDTH, &w);
    eglQuerySurface(m_display, m_surface, EGL_HEIGHT, &h);

    PRESENTATION_PARAMS presentationParams;
    presentationParams.mode =
        static_cast<CGSHandler::PRESENTATION_MODE>(
            CAppConfig::GetInstance().GetPreferenceInteger(
                PREF_CGSHANDLER_PRESENTATION_MODE));

    presentationParams.windowWidth = w;
    presentationParams.windowHeight = h;

    SetPresentationParams(presentationParams);

    eglSwapInterval(m_display, 0);
}

