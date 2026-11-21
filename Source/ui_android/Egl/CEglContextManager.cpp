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

    bool IsValidConfig(EGLConfig config)
    {
        return config != nullptr;
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
        return eglGetError();
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
    ClearError(error);

    if(context == nullptr)
    {
        SetError(error, EGL_BAD_PARAMETER);
        return false;
    }

    *context = EGL_NO_CONTEXT;

    if(!IsValidDisplay(display))
    {
        SetError(error, EGL_BAD_DISPLAY);
        return false;
    }

    if(!IsValidConfig(config))
    {
        SetError(error, EGL_BAD_CONFIG);
        return false;
    }

    if(clientVersion != 2 && clientVersion != 3)
    {
        SetError(error, EGL_BAD_ATTRIBUTE);
        return false;
    }

    if(eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE)
    {
        SetError(error, ConsumeEglError());
        return false;
    }

    const EGLint contextAttributes[] =
    {
        EGL_CONTEXT_CLIENT_VERSION,
        clientVersion,
        EGL_NONE
    };

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

    if(!IsValidDisplay(display))
    {
        SetError(error, EGL_BAD_DISPLAY);
        return false;
    }

    if(!IsValidSurface(drawSurface) ||
       !IsValidSurface(readSurface))
    {
        SetError(error, EGL_BAD_SURFACE);
        return false;
    }

    if(!IsValidContext(context))
    {
        SetError(error, EGL_BAD_CONTEXT);
        return false;
    }

    if(eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE)
    {
        SetError(error, ConsumeEglError());
        return false;
    }

    if(eglMakeCurrent(
           display,
           drawSurface,
           readSurface,
           context) != EGL_TRUE)
    {
        SetError(error, ConsumeEglError());
        return false;
    }

    if(eglGetCurrentContext() != context ||
       eglGetCurrentDisplay() != display ||
       eglGetCurrentSurface(EGL_DRAW) != drawSurface ||
       eglGetCurrentSurface(EGL_READ) != readSurface)
    {
        SetError(error, EGL_BAD_MATCH);
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

    if(currentContext == EGL_NO_CONTEXT)
    {
        return true;
    }

    if(currentContext != expectedContext)
    {
        SetError(error, EGL_BAD_CONTEXT);
        return false;
    }

    if(eglGetCurrentDisplay() != display)
    {
        SetError(error, EGL_BAD_DISPLAY);
        return false;
    }

    if(eglMakeCurrent(
           display,
           EGL_NO_SURFACE,
           EGL_NO_SURFACE,
           EGL_NO_CONTEXT) != EGL_TRUE)
    {
        SetError(error, ConsumeEglError());
        return false;
    }

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

    if(eglGetCurrentContext() == contextToDestroy)
    {
        if(!ClearCurrent(
               display,
               contextToDestroy,
               error))
        {
            return false;
        }
    }

    if(eglDestroyContext(display, contextToDestroy) != EGL_TRUE)
    {
        SetError(error, ConsumeEglError());
        return false;
    }

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
