package com.virtualapplications.play;

import android.app.Activity;
import android.app.Application;
import android.content.res.Configuration;
import android.os.Bundle;

import java.util.WeakHashMap;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Tracks Activity lifecycle without retaining Activity instances.
 *
 * ApplicationState remains the owner of process visibility state.
 * This controller only observes lifecycle and window-mode transitions.
 */
public final class ActivityLifecycleController
        implements Application.ActivityLifecycleCallbacks
{
    private static final class ActivityRecord
    {
        boolean started;
        boolean resumed;
        boolean inMultiWindow;
        boolean inPictureInPicture;

        int orientation;
        int screenWidthDp;
        int screenHeightDp;
        int uiMode;
    }

    private final Object lock = new Object();

    private final Application application;
    private final ApplicationState applicationState;

    private final WeakHashMap<Activity, ActivityRecord> records =
            new WeakHashMap<>();

    private final AtomicInteger resumedActivityCount =
            new AtomicInteger(0);

    private boolean registered;

    public ActivityLifecycleController(
            Application application,
            ApplicationState applicationState)
    {
        if(application == null || applicationState == null)
        {
            throw new IllegalArgumentException(
                    "Application and ApplicationState must not be null.");
        }

        this.application = application;
        this.applicationState = applicationState;
    }

    public void register()
    {
        synchronized(lock)
        {
            if(registered)
                return;

            application.registerActivityLifecycleCallbacks(this);
            registered = true;
        }
    }

    public boolean isRegistered()
    {
        synchronized(lock)
        {
            return registered;
        }
    }

    public int getResumedActivityCount()
    {
        return resumedActivityCount.get();
    }

    public boolean isActivityStarted(Activity activity)
    {
        synchronized(lock)
        {
            ActivityRecord record = records.get(activity);
            return record != null && record.started;
        }
    }

    public boolean isActivityResumed(Activity activity)
    {
        synchronized(lock)
        {
            ActivityRecord record = records.get(activity);
            return record != null && record.resumed;
        }
    }

    public boolean isInMultiWindowMode(Activity activity)
    {
        synchronized(lock)
        {
            ActivityRecord record = records.get(activity);
            return record != null && record.inMultiWindow;
        }
    }

    public boolean isInPictureInPictureMode(Activity activity)
    {
        synchronized(lock)
        {
            ActivityRecord record = records.get(activity);
            return record != null && record.inPictureInPicture;
        }
    }

    private ActivityRecord getOrCreateRecord(Activity activity)
    {
        ActivityRecord record = records.get(activity);

        if(record == null)
        {
            record = new ActivityRecord();
            records.put(activity, record);
        }

        return record;
    }

    private void decrementResumedCount()
    {
        while(true)
        {
            int current = resumedActivityCount.get();

            if(current <= 0)
                return;

            if(resumedActivityCount.compareAndSet(
                    current, current - 1))
            {
                return;
            }
        }
    }

    @Override
    public void onActivityCreated(
            Activity activity,
            Bundle savedInstanceState)
    {
        synchronized(lock)
        {
            getOrCreateRecord(activity);
        }
    }

    @Override
    public void onActivityStarted(Activity activity)
    {
        boolean notify = false;

        synchronized(lock)
        {
            ActivityRecord record = getOrCreateRecord(activity);

            if(!record.started)
            {
                record.started = true;
                notify = true;
            }
        }

        if(notify)
            applicationState.onActivityStarted();
    }

    @Override
    public void onActivityResumed(Activity activity)
    {
        synchronized(lock)
        {
            ActivityRecord record = getOrCreateRecord(activity);

            if(!record.resumed)
            {
                record.resumed = true;
                resumedActivityCount.incrementAndGet();
            }
        }
    }

    @Override
    public void onActivityPaused(Activity activity)
    {
        boolean notify = false;

        synchronized(lock)
        {
            ActivityRecord record = records.get(activity);

            if(record != null && record.resumed)
            {
                record.resumed = false;
                notify = true;
            }
        }

        if(notify)
            decrementResumedCount();
    }

    @Override
    public void onActivityStopped(Activity activity)
    {
        boolean notify = false;

        synchronized(lock)
        {
            ActivityRecord record = records.get(activity);

            if(record != null && record.started)
            {
                record.started = false;
                notify = true;
            }
        }

        if(notify)
            applicationState.onActivityStopped();
    }

    @Override
    public void onActivitySaveInstanceState(
            Activity activity,
            Bundle outState)
    {
        // Activity-specific state is owned by each Activity.
    }

    @Override
    public void onActivityDestroyed(Activity activity)
    {
        boolean wasStarted = false;
        boolean wasResumed = false;

        synchronized(lock)
        {
            ActivityRecord record = records.remove(activity);

            if(record != null)
            {
                wasStarted = record.started;
                wasResumed = record.resumed;
            }
        }

        if(wasResumed)
            decrementResumedCount();

        if(wasStarted)
            applicationState.onActivityStopped();
    }

    public void onConfigurationChanged(
            Activity activity,
            Configuration configuration)
    {
        if(activity == null || configuration == null)
            return;

        synchronized(lock)
        {
            ActivityRecord record = getOrCreateRecord(activity);

            record.orientation = configuration.orientation;
            record.screenWidthDp = configuration.screenWidthDp;
            record.screenHeightDp = configuration.screenHeightDp;
            record.uiMode = configuration.uiMode;
        }
    }

    public void onMultiWindowModeChanged(
            Activity activity,
            boolean inMultiWindow)
    {
        if(activity == null)
            return;

        synchronized(lock)
        {
            getOrCreateRecord(activity).inMultiWindow =
                    inMultiWindow;
        }
    }

    public void onPictureInPictureModeChanged(
            Activity activity,
            boolean inPictureInPicture)
    {
        if(activity == null)
            return;

        synchronized(lock)
        {
            getOrCreateRecord(activity).inPictureInPicture =
                    inPictureInPicture;
        }
    }

    private static ActivityLifecycleController getController(
            Activity activity)
    {
        if(activity == null)
            return null;

        if(!(activity.getApplication() instanceof PlayApplication))
            return null;

        PlayApplication application =
                (PlayApplication)activity.getApplication();

        ApplicationInitializationCoordinator coordinator =
                application.getInitializationCoordinator();

        if(coordinator == null)
            return null;

        return coordinator.getActivityLifecycleController();
    }

    public static void dispatchConfigurationChanged(
            Activity activity,
            Configuration configuration)
    {
        ActivityLifecycleController controller =
                getController(activity);

        if(controller != null)
        {
            controller.onConfigurationChanged(
                    activity, configuration);
        }
    }

    public static void dispatchMultiWindowModeChanged(
            Activity activity,
            boolean inMultiWindow)
    {
        ActivityLifecycleController controller =
                getController(activity);

        if(controller != null)
        {
            controller.onMultiWindowModeChanged(
                    activity, inMultiWindow);
        }
    }

    public static void dispatchPictureInPictureModeChanged(
            Activity activity,
            boolean inPictureInPicture)
    {
        ActivityLifecycleController controller =
                getController(activity);

        if(controller != null)
        {
            controller.onPictureInPictureModeChanged(
                    activity, inPictureInPicture);
        }
    }
}
