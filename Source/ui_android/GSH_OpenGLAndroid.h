#pragma once

#include "../gs/GSH_OpenGL/GSH_OpenGL.h"
#include "opengl/OpenGlDef.h"
#include "NativeWindowUpdateListener.h"

class CGSH_OpenGLAndroid : public CGSH_OpenGL, public INativeWindowUpdateListener
{
public:
	CGSH_OpenGLAndroid(NativeWindowType);
	virtual ~CGSH_OpenGLAndroid();

	void SetWindow(NativeWindowType) override;

	static FactoryFunction GetFactoryFunction(NativeWindowType);

	void InitializeImpl() override;
	void ReleaseImpl() override;
	void PresentBackbuffer() override;

private:
    bool SetupContext();

    // Release exactly one shared EGL display lease.
    void ReleaseEglDisplay();

    // Detach and destroy the current EGL surface safely.
    void DestroySurface();

    // Recreate the EGL display/context and OpenGL renderer.
    void RecoverEglSession();

    // Mencegah pemulihan EGL berulang ketika context sedang dibuat ulang.
    bool m_rendererInitialized = false;
    bool m_contextRecoveryInProgress = false;

    // Indicates that the current EGL surface needs replacement.
    bool m_surfaceRecoveryPending = false;

	// Owns the transferred ANativeWindow reference. nullptr means detached.
    NativeWindowType m_window = nullptr;

	EGLConfig m_config = nullptr;
	EGLDisplay m_display = EGL_NO_DISPLAY;
	EGLContext m_context = EGL_NO_CONTEXT;
	EGLSurface m_surface = EGL_NO_SURFACE;
};
