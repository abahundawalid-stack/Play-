#include <android/native_window.h>
#include <memory>

#include "GSH_VulkanAndroid.h"
#include "vulkan/StructDefs.h"

CGSH_VulkanAndroid::CGSH_VulkanAndroid(ANativeWindow* window)
    : m_window(window)
{
}

CGSH_VulkanAndroid::~CGSH_VulkanAndroid()
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
            ANativeWindow_acquire(handlerWindow);
        }

        return new CGSH_VulkanAndroid(handlerWindow);
    };
}

void CGSH_VulkanAndroid::InitializeImpl()
{
    if(m_rendererInitialized || m_window == nullptr)
    {
        return;
    }

    if(m_instance.IsEmpty())
    {
        m_instance = CreateInstance(true);
    }

    if(m_instance.IsEmpty())
    {
        return;
    }

    CreateSurface();

    if(m_context == nullptr ||
       m_context->surface == VK_NULL_HANDLE)
    {
        return;
    }

    CGSH_Vulkan::InitializeImpl();

    if(!m_context->device.IsEmpty())
    {
        m_rendererInitialized = true;
        UpdateViewport();
    }
}

void CGSH_VulkanAndroid::ReleaseImpl()
{
    if(m_rendererInitialized)
    {
        CGSH_Vulkan::ReleaseImpl();
        m_rendererInitialized = false;
    }

    if(!m_instance.IsEmpty() &&
       m_context != nullptr &&
       m_context->surface != VK_NULL_HANDLE)
    {
        m_instance.vkDestroySurfaceKHR(
            m_instance,
            m_context->surface,
            nullptr);

        m_context->surface = VK_NULL_HANDLE;
    }
}

void CGSH_VulkanAndroid::PresentBackbuffer()
{
}

void CGSH_VulkanAndroid::SetWindow(ANativeWindow* window)
{
    SendGSCall(
        [this, window]() {
            ANativeWindow* previousWindow = m_window;

            if(previousWindow == window)
            {
                if(window != nullptr)
                {
                    ANativeWindow_release(window);
                }
                return;
            }

            // Selesaikan pekerjaan GPU dan lepaskan objek presentasi lama.
            if(m_rendererInitialized)
            {
                OnSurfaceChanging();
            }

            m_window = window;

            // Inisialisasi pertama atau inisialisasi tertunda.
            if(!m_rendererInitialized)
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

            // Renderer sudah aktif: ganti atau lepaskan surface.
            CreateSurface();

            if(m_context != nullptr &&
               m_context->surface != VK_NULL_HANDLE)
            {
                OnSurfaceChanged();
                UpdateViewport();
            }

            // Window lama dilepas setelah surface lamanya dihancurkan.
            if(previousWindow != nullptr)
            {
                ANativeWindow_release(previousWindow);
            }
        },
        true);
}

void CGSH_VulkanAndroid::CreateSurface()
{
    if(m_instance.IsEmpty() || m_context == nullptr)
    {
        return;
    }

    if(m_context->surface != VK_NULL_HANDLE)
    {
        m_instance.vkDestroySurfaceKHR(
            m_instance,
            m_context->surface,
            nullptr);

        m_context->surface = VK_NULL_HANDLE;
    }

    if(m_window == nullptr)
    {
        return;
    }

    auto surfaceCreateInfo =
        Framework::Vulkan::AndroidSurfaceCreateInfoKHR();

    surfaceCreateInfo.window = m_window;

    auto result = m_instance.vkCreateAndroidSurfaceKHR(
        m_instance,
        &surfaceCreateInfo,
        nullptr,
        &m_context->surface);

    CHECKVULKANERROR(result);
}

void CGSH_VulkanAndroid::UpdateViewport()
{
	if(m_window == nullptr)
	{
		return;
	}

	PRESENTATION_PARAMS presentationParams;
	presentationParams.mode = PRESENTATION_MODE_FIT;
	presentationParams.windowWidth = ANativeWindow_getWidth(m_window);
	presentationParams.windowHeight = ANativeWindow_getHeight(m_window);

	SetPresentationParams(presentationParams);
}
