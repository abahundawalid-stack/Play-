#include <android/native_window.h>

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

CGSHandler::FactoryFunction CGSH_VulkanAndroid::GetFactoryFunction(ANativeWindow* window)
{
	return [window]() {
		return new CGSH_VulkanAndroid(window);
	};
}

void CGSH_VulkanAndroid::InitializeImpl()
{
	if(m_window == nullptr)
	{
		return;
	}

	m_instance = CreateInstance(true);

	CreateSurface();

	CGSH_Vulkan::InitializeImpl();

	UpdateViewport();
}

void CGSH_VulkanAndroid::ReleaseImpl()
{
	CGSH_Vulkan::ReleaseImpl();

	if(m_context != nullptr &&
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

            // Lepaskan objek presentasi dan swapchain sebelum
            // menghancurkan VkSurfaceKHR yang lama.
            OnSurfaceChanging();

            m_window = window;

            // Jika Vulkan belum diinisialisasi, simpan window saja.
            // InitializeImpl akan menggunakan window tersebut.
            if(m_instance.IsEmpty())
            {
                if(previousWindow != nullptr)
                {
                    ANativeWindow_release(previousWindow);
                }
                return;
            }

            // Hancurkan surface lama dan buat surface baru.
            CreateSurface();

            if(m_window != nullptr &&
               m_context->surface != VK_NULL_HANDLE)
            {
                // Buat ulang presentasi berdasarkan surface baru.
                OnSurfaceChanged();

                if(m_present)
                {
                    UpdateViewport();
                }
            }

            // Referensi window lama baru dilepas setelah surface
            // Vulkan lama dan objek presentasinya selesai dihancurkan.
            if(previousWindow != nullptr)
            {
                ANativeWindow_release(previousWindow);
            }
        },
        true);
}

void CGSH_VulkanAndroid::CreateSurface()
{
    if(m_instance.IsEmpty())
    {
        return;
    }

    if(m_context != nullptr &&
       m_context->surface != VK_NULL_HANDLE)
    {
        m_instance.vkDestroySurfaceKHR(
            m_instance,
            m_context->surface,
            nullptr);

        m_context->surface = VK_NULL_HANDLE;
    }

    if(m_window == nullptr || m_context == nullptr)
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
