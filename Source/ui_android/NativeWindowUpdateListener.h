#pragma once

class ANativeWindow;

class INativeWindowUpdateListener
{
public:
    virtual ~INativeWindowUpdateListener() = default;

    // Takes ownership of the reference returned by
    // ANativeWindow_fromSurface(). nullptr releases the current window.
    virtual void SetWindow(ANativeWindow*) = 0;
};
