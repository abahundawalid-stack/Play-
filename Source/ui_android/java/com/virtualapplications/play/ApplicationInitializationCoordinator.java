package com.virtualapplications.play;

import android.app.Activity;
import android.app.Application;
import android.os.Bundle;
import java.util.concurrent.atomic.AtomicBoolean;

/** Performs process initialization once and wires Android Activity lifecycle. */
public final class ApplicationInitializationCoordinator
{
    private final PlayApplication application;
    private final AtomicBoolean initialized = new AtomicBoolean(false);

    ApplicationInitializationCoordinator(PlayApplication application)
    {
        if(application == null)
            throw new IllegalArgumentException(
                "Application must not be null.");
        this.application = application;
    }

    public void initialize()
    {
        if(!initialized.compareAndSet(false, true))
            return;

        try
        {
            application.initializeCoreServices();

            AppExitDiagnostics.installUncaughtExceptionLogger(application);
            AppExitDiagnostics.capturePreviousExit(application);

            application.registerActivityLifecycleCallbacks(
                new Application.ActivityLifecycleCallbacks()
                {
                    @Override
                    public void onActivityCreated(Activity activity, Bundle state) {}

                    @Override
                    public void onActivityStarted(Activity activity)
                    {
                        application.getApplicationState().onActivityStarted();
                    }

                    @Override
                    public void onActivityResumed(Activity activity) {}

                    @Override
                    public void onActivityPaused(Activity activity) {}

                    @Override
                    public void onActivityStopped(Activity activity)
                    {
                        application.getApplicationState().onActivityStopped();
                    }

                    @Override
                    public void onActivitySaveInstanceState(
                        Activity activity, Bundle state) {}

                    @Override
                    public void onActivityDestroyed(Activity activity) {}
                });

            ApplicationLogger.info(
                Constants.TAG, "Application Core initialized.");
        }
        catch(RuntimeException error)
        {
            initialized.set(false);
            ApplicationLogger.error(
                Constants.TAG, "Application Core initialization failed.",
                error);
            throw error;
        }
        catch(Error error)
        {
            initialized.set(false);
            ApplicationLogger.error(
                Constants.TAG, "Fatal Application Core initialization error.",
                error);
            throw error;
        }
    }

    public boolean isInitialized()
    {
        return initialized.get();
    }
}
