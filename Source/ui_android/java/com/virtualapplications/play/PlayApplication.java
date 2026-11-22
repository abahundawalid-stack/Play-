package com.virtualapplications.play;

import android.app.Application;

/** Android process entry point for Play! application-level services. */
public final class PlayApplication extends Application
{
    private ApplicationServiceRegistry services;
    private ApplicationState applicationState;
    private ApplicationShutdownCoordinator shutdownCoordinator;
    private ApplicationInitializationCoordinator initializationCoordinator;

    @Override
    public void onCreate()
    {
        super.onCreate();

        services = new ApplicationServiceRegistry();
        applicationState = new ApplicationState();
        shutdownCoordinator = new ApplicationShutdownCoordinator();
        initializationCoordinator =
            new ApplicationInitializationCoordinator(this);

        initializationCoordinator.initialize();
    }

    void initializeCoreServices()
    {
        ApplicationConfiguration configuration =
            new ApplicationConfiguration(this);
        ApplicationCompatibility compatibility =
            new ApplicationCompatibility(this);
        ApplicationVersion version = new ApplicationVersion(this);

        services.registerComponent(PlayApplication.class, this);
        services.register(ApplicationServiceRegistry.class, services);
        services.register(ApplicationState.class, applicationState);
        services.register(ApplicationConfiguration.class, configuration);
        services.register(ApplicationCompatibility.class, compatibility);
        services.register(ApplicationVersion.class, version);
        services.register(
            ApplicationShutdownCoordinator.class, shutdownCoordinator);
    }

    public ApplicationServiceRegistry getServices()
    {
        return services;
    }

    public ApplicationState getApplicationState()
    {
        return applicationState;
    }

    public ApplicationShutdownCoordinator getShutdownCoordinator()
    {
        return shutdownCoordinator;
    }

    public ApplicationInitializationCoordinator getInitializationCoordinator()
    {
        return initializationCoordinator;
    }

    @Override
    public void onTerminate()
    {
        if(applicationState != null)
            applicationState.markShuttingDown();

        if(shutdownCoordinator != null)
            shutdownCoordinator.shutdown();

        if(services != null)
            services.clear();

        super.onTerminate();
    }
}
