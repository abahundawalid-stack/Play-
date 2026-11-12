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

CGSHandler::FactoryFunction CGSH_VulkanAndroid::GetFactoryFunction(
    ANativeWindow* window)
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
            // Handler receives its own reference.
            ANativeWindow_acquire(handlerWindow);
        }

        return new CGSH_VulkanAndroid(handlerWindow);
    };
}

void CGSH_VulkanAndroid::InitializeImpl()
{
    // Queue/device initialization needs a valid Android surface.
    // A later SetWindow() will retry initialization when one arrives.
    if(m_window == nullptr)
    {
        return;
    }

    m_instance = CreateInstance(true);

    CreateSurface();

    if(m_context == nullptr ||
       m_context->surface == VK_NULL_HANDLE)
    {
        return;
    }

    CGSH_Vulkan::InitializeImpl();

    if(!m_context->device.IsEmpty())
    {
        UpdateViewport();
    }
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

void CGSH_VulkanAndroid::SetWindow(ANativeWindow* window)
{
    SendGSCall(
        [this, window]() {
            ANativeWindow* previousWindow = m_window;
            m_window = window;

            // Handler belum pernah diinisialisasi karena belum
            // menerima window pertama.
            if(m_instance.IsEmpty())
            {
                if(m_window != nullptr)
                {
                    InitializeImpl();
                }

                if(previousWindow != nullptr)
                {
                    ANativeWindow_release(previousWindow);
                }
                return;
            }

            // Stop GPU work and destroy CPresent before old surface.
            OnSurfaceChanging();

            // CreateSurface destroys the old VkSurfaceKHR first.
            CreateSurface();

            if(m_window != nullptr &&
               m_context != nullptr &&
               m_context->surface != VK_NULL_HANDLE)
            {
                OnSurfaceChanged();

                if(m_present)
                {
                    UpdateViewport();
                }
            }

            // Release the previous native window only after the old
            // Vulkan surface and presentation resources are gone.
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

