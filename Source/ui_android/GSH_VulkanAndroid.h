#pragma once

#include "../gs/GSH_Vulkan/GSH_Vulkan.h"
#include "NativeWindowUpdateListener.h"

class CGSH_VulkanAndroid : public CGSH_Vulkan, public INativeWindowUpdateListener
{
public:
	CGSH_VulkanAndroid(ANativeWindow*);
	virtual ~CGSH_VulkanAndroid();

	void SetWindow(ANativeWindow*) override;

	static FactoryFunction GetFactoryFunction(ANativeWindow*);

	void InitializeImpl() override;
	void ReleaseImpl() override;
	void PresentBackbuffer() override;

private:
	void CreateSurface();
	void UpdateViewport();

	// Owns the transferred ANativeWindow reference. nullptr means detached.
ANativeWindow* m_window = nullptr;

};
