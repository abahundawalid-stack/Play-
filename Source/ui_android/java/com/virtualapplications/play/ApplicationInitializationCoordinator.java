package com.virtualapplications.play;

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Performs process initialization once and registers Activity lifecycle
 * observation.
 */
public final class ApplicationInitializationCoordinator
{
    private final PlayApplication application;

    private final ActivityLifecycleController activityLifecycleController;

    private final AtomicBoolean initialized =
            new AtomicBoolean(false);

    ApplicationInitializationCoordinator(
            PlayApplication application)
    {
        if(application == null)
        {
            throw new IllegalArgumentException(
                    "Application must not be null.");
        }

        this.application = application;

        this.activityLifecycleController =
                new ActivityLifecycleController(
                        application,
                        application.getApplicationState());
    }

    public void initialize()
    {
        if(!initialized.compareAndSet(false, true))
            return;

        try
        {
            application.initializeCoreServices();

            AppExitDiagnostics.installUncaughtExceptionLogger(
                    application);

            AppExitDiagnostics.capturePreviousExit(
                    application);

            activityLifecycleController.register();

            ApplicationLogger.info(
                    Constants.TAG,
                    "Application Core initialized.");
        }
        catch(RuntimeException error)
        {
            initialized.set(false);

            ApplicationLogger.error(
                    Constants.TAG,
                    "Application Core initialization failed.",
                    error);

            throw error;
        }
        catch(Error error)
        {
            initialized.set(false);

            ApplicationLogger.error(
                    Constants.TAG,
                    "Fatal Application Core initialization error.",
                    error);

            throw error;
        }
    }

    public boolean isInitialized()
    {
        return initialized.get();
    }

    public ActivityLifecycleController
            getActivityLifecycleController()
    {
        return activityLifecycleController;
    }
}
