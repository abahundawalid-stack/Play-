#pragma once

#include <vector>

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

    // Destroys a surface without losing its handle on failure.
    bool DestroySurfaceHandle(
        EGLSurface surface,
        EGLint* error = nullptr);

    // Retries destruction of surfaces that previously failed cleanup.
    void CleanupRetiredSurfaces();

    // Transfer unresolved EGL resources to the process-wide
    // deferred cleanup manager without invoking EGL from this method.
    bool DeferOutstandingEglResources();

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

    // Native visual ID belonging to the selected EGLConfig.
    // Used to match the Android ANativeWindow pixel format.
    EGLint m_nativeVisualId = 0;

    EGLDisplay m_display = EGL_NO_DISPLAY;
	EGLContext m_context = EGL_NO_CONTEXT;
	EGLSurface m_surface = EGL_NO_SURFACE;
    
    // One retained native-window reference belongs to each EGL surface.
    // The reference must not be released until eglDestroySurface succeeds.
    struct RetiredSurface
    {
        EGLSurface surface = EGL_NO_SURFACE;
        NativeWindowType window = nullptr;
    };

    // Native-window reference associated with the active EGL surface.
    // This is independent from m_window.
    NativeWindowType m_surfaceWindow = nullptr;

    // EGL surfaces that could not be destroyed.
    // Each record retains the native window used to create that surface.
    std::vector<RetiredSurface> m_retiredSurfaces;

};
