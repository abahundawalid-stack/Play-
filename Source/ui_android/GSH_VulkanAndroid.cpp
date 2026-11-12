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
	if(window == nullptr)
	{
		return;
	}

	// Ownership of the newly acquired ANativeWindow reference transfers here.
	auto previousWindow = m_window;
	m_window = window;

	SendGSCall(
	    [this]() {
		    CreateSurface();
		    UpdateViewport();
	    },
	    true);

	if(previousWindow != nullptr)
	{
		ANativeWindow_release(previousWindow);
	}
}

void CGSH_VulkanAndroid::CreateSurface()
{
	if(m_window == nullptr || m_context == nullptr)
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
