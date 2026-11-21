#include "CEglContextManager.h"

namespace
{
    void SetError(EGLint* output, EGLint value)
    {
        if(output != nullptr)
        {
            *output = value;
        }
    }

    void ClearError(EGLint* error)
    {
        SetError(error, EGL_SUCCESS);
    }

    bool IsValidDisplay(EGLDisplay display)
    {
        return display != EGL_NO_DISPLAY;
    }

    bool IsValidContext(EGLContext context)
    {
        return context != EGL_NO_CONTEXT;
    }

    bool IsValidSurface(EGLSurface surface)
    {
        return surface != EGL_NO_SURFACE;
    }

    EGLint ConsumeEglError()
    {
        const EGLint error = eglGetError();

        return error == EGL_SUCCESS ? EGL_BAD_ACCESS : error;
    }
}

bool CEglContextManager::Create(
    EGLDisplay display,
    EGLConfig config,
    EGLint clientVersion,
    EGLContext sharedContext,
    EGLContext* context,
    EGLint* error)
{
    if(context != nullptr)
    {
        *context = EGL_NO_CONTEXT;
    }

    ClearError(error);

    // ---------------------------------------------------------
    // 1. VALIDATE INPUT
    // ---------------------------------------------------------

    if(!IsValidDisplay(display) ||
       config == nullptr ||
       context == nullptr ||
       (clientVersion != 2 && clientVersion != 3))
    {
        SetError(error, EGL_BAD_PARAMETER);
        return false;
    }

    // ---------------------------------------------------------
    // 2. BIND OPENGL ES API TO THE CALLING THREAD
    // ---------------------------------------------------------

    if(eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE)
    {
        SetError(error, ConsumeEglError());
        return false;
    }

    // ---------------------------------------------------------
    // 3. BUILD CONTEXT ATTRIBUTES
    // ---------------------------------------------------------

    const EGLint contextAttributes[] =
    {
        EGL_CONTEXT_CLIENT_VERSION,
        clientVersion,
        EGL_NONE
    };

    // ---------------------------------------------------------
    // 4. CREATE CONTEXT
    // ---------------------------------------------------------

    EGLContext newContext = eglCreateContext(
        display,
        config,
        sharedContext,
        contextAttributes);

    if(!IsValidContext(newContext))
    {
        SetError(error, ConsumeEglError());
        return false;
    }

    // ---------------------------------------------------------
    // 5. COMMIT OUTPUT ONLY AFTER SUCCESS
    // ---------------------------------------------------------

    *context = newContext;

    return true;
}

bool CEglContextManager::MakeCurrent(
    EGLDisplay display,
    EGLSurface drawSurface,
    EGLSurface readSurface,
    EGLContext context,
    EGLint* error)
{
    ClearError(error);

    // ---------------------------------------------------------
    // 1. VALIDATE INPUT
    // ---------------------------------------------------------

    if(!IsValidDisplay(display) ||
       !IsValidSurface(drawSurface) ||
       !IsValidSurface(readSurface) ||
       !IsValidContext(context))
    {
        SetError(error, EGL_BAD_PARAMETER);
        return false;
    }

    // ---------------------------------------------------------
    // 2. ENSURE OPENGL ES API IS SELECTED ON THIS THREAD
    // ---------------------------------------------------------

    if(eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE)
    {
        SetError(error, ConsumeEglError());
        return false;
    }

    // ---------------------------------------------------------
    // 3. BIND CONTEXT AND SURFACES
    // ---------------------------------------------------------

    if(eglMakeCurrent(
           display,
           drawSurface,
           readSurface,
           context) != EGL_TRUE)
    {
        SetError(error, ConsumeEglError());
        return false;
    }

    // ---------------------------------------------------------
    // 4. VERIFY CURRENT CONTEXT
    // ---------------------------------------------------------

    if(eglGetCurrentContext() != context)
    {
        SetError(error, EGL_BAD_CONTEXT);
        return false;
    }

    return true;
}

bool CEglContextManager::ClearCurrent(
    EGLDisplay display,
    EGLContext expectedContext,
    EGLint* error)
{
    ClearError(error);

    if(!IsValidDisplay(display))
    {
        SetError(error, EGL_BAD_DISPLAY);
        return false;
    }

    if(!IsValidContext(expectedContext))
    {
        SetError(error, EGL_BAD_CONTEXT);
        return false;
    }

    const EGLContext currentContext = eglGetCurrentContext();

    // Tidak ada context yang aktif pada thread ini.
    if(currentContext == EGL_NO_CONTEXT)
    {
        return true;
    }

    // Jangan pernah melepas context milik renderer lain.
    if(currentContext != expectedContext)
    {
        SetError(error, EGL_BAD_CONTEXT);
        return false;
    }

    // Pastikan context aktif memang berasal dari display ini.
    const EGLDisplay currentDisplay = eglGetCurrentDisplay();

    if(currentDisplay != display)
    {
        SetError(error, EGL_BAD_DISPLAY);
        return false;
    }

    // Lepaskan hanya context yang sudah diverifikasi.
    if(eglMakeCurrent(
           display,
           EGL_NO_SURFACE,
           EGL_NO_SURFACE,
           EGL_NO_CONTEXT) != EGL_TRUE)
    {
        SetError(error, eglGetError());
        return false;
    }

    // Verifikasi bahwa context benar-benar sudah dilepas.
    if(eglGetCurrentContext() != EGL_NO_CONTEXT)
    {
        SetError(error, EGL_BAD_CONTEXT);
        return false;
    }

    return true;
}

bool CEglContextManager::Destroy(
    EGLDisplay display,
    EGLContext* context,
    EGLint* error)
{
    ClearError(error);

    if(context == nullptr)
    {
        SetError(error, EGL_BAD_PARAMETER);
        return false;
    }

    if(!IsValidContext(*context))
    {
        return true;
    }

    if(!IsValidDisplay(display))
    {
        SetError(error, EGL_BAD_DISPLAY);
        return false;
    }

    const EGLContext contextToDestroy = *context;

    // ---------------------------------------------------------
    // 1. DETACH IF CURRENT ON THIS THREAD
    // ---------------------------------------------------------

    if(eglGetCurrentContext() == contextToDestroy)
    {
        if(!ClearCurrent(
               display,
               contextToDestroy,
               error))
        {
            return false;
        }

    // ---------------------------------------------------------
    // 2. DESTROY CONTEXT
    // ---------------------------------------------------------

    if(eglDestroyContext(display, contextToDestroy) != EGL_TRUE)
    {
        SetError(error, ConsumeEglError());
        return false;
    }

    // ---------------------------------------------------------
    // 3. CLEAR HANDLE ONLY AFTER SUCCESS
    // ---------------------------------------------------------

    *context = EGL_NO_CONTEXT;

    return true;
}

bool CEglContextManager::IsCurrent(EGLContext context)
{
    if(!IsValidContext(context))
    {
        return false;
    }

    return eglGetCurrentContext() == context;
}

EGLContext CEglContextManager::GetCurrent()
{
    return eglGetCurrentContext();
}
