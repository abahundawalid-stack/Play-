package com.virtualapplications.play;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Binder;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import java.util.concurrent.Future;
import java.util.concurrent.Callable;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Android foreground service for an active ORBIT2SX emulator session.
 *
 * IMPORTANT:
 * This service does not own the virtual machine, graphics renderer,
 * EGL resources, native Surface, or GS thread.
 */
public final class EmulatorService extends Service
{
    public static final String ACTION_START =
        "com.virtualapplications.play.action.START_EMULATOR_SERVICE";

    public static final String ACTION_STOP =
        "com.virtualapplications.play.action.STOP_EMULATOR_SERVICE";

    public static final String EXTRA_GAME_TITLE =
        "com.virtualapplications.play.extra.GAME_TITLE";

    private static final String TAG =
        "PlayEmulatorService";

    private static final String CHANNEL_ID =
        "play_emulator_session";

    private static final int NOTIFICATION_ID =
        4101;

    private final IBinder binder =
        new LocalBinder();

    private final AtomicBoolean shutdownRequested =
        new AtomicBoolean(false);

    private final BackgroundTaskCoordinator backgroundTasks =
        new BackgroundTaskCoordinator();

    private volatile boolean emulatorRunning = false;

    private volatile boolean foregroundStarted = false;

    private volatile String gameTitle =
    "ORBIT2SX Emulator";
    
    public final class LocalBinder extends Binder
    {
        public EmulatorService getService()
        {
            return EmulatorService.this;
        }
    }

    @Override
    public void onCreate()
    {
        super.onCreate();

        createNotificationChannel();

        Log.i(TAG, "EmulatorService created.");
    }

    @Override
    public int onStartCommand(
        Intent intent,
        int flags,
        int startId)
    {
        if(intent != null)
        {
            String action = intent.getAction();

            if(ACTION_STOP.equals(action))
            {
                stopSession();
                return START_NOT_STICKY;
            }

            String requestedTitle =
                intent.getStringExtra(EXTRA_GAME_TITLE);

            if(requestedTitle != null &&
               !requestedTitle.trim().isEmpty())
            {
                gameTitle = requestedTitle.trim();
            }
        }

        if(shutdownRequested.get())
        {
            stopSelf(startId);
            return START_NOT_STICKY;
        }

        try
        {
            startForegroundSession();
        }
        catch(SecurityException error)
        {
            Log.e(
                TAG,
                "Foreground service permission/start failure.",
                error);

            stopSelf(startId);
        }
        catch(RuntimeException error)
        {
            Log.e(
                TAG,
                "Unable to start foreground notification.",
                error);

            stopSelf(startId);
        }

        /*
         * Do not recreate a stale native emulator session after
         * process termination. The Activity must establish a new
         * valid session and bind again.
         */
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent)
    {
        return binder;
    }

    @Override
    public boolean onUnbind(Intent intent)
    {
        return super.onUnbind(intent);
    }

    @Override
    public void onTaskRemoved(Intent rootIntent)
    {
        Log.i(
            TAG,
            "Application task removed. Stopping emulator service.");

        stopSession();

        super.onTaskRemoved(rootIntent);
    }
    
    public void setEmulatorState(
        boolean running,
        String title)
    {
        if(shutdownRequested.get())
        {
            return;
        }

        emulatorRunning = running;

        if(title != null && !title.trim().isEmpty())
        {
            gameTitle = title.trim();
        }

        updateNotification();
    }

    public boolean isEmulatorRunning()
    {
        return emulatorRunning;
    }

    public String getGameTitle()
    {
        return gameTitle;
    }

    public int getQueuedBackgroundTaskCount()
    {
        return backgroundTasks.getQueuedTaskCount();
    }

    public Future<?> submitBackgroundTask(Runnable task)
    {
        if(shutdownRequested.get())
        {
            throw new IllegalStateException(
                "EmulatorService is shutting down.");
        }

        return backgroundTasks.submit(task);
    }

    public <T> Future<T> submitBackgroundTask(
        Callable<T> task)
    {
        if(shutdownRequested.get())
        {
            throw new IllegalStateException(
                "EmulatorService is shutting down.");
        }

        return backgroundTasks.submit(task);
    }

    /**
     * Explicit service shutdown.
     *
     * Does not pause, destroy, or release native VM/GS/EGL resources.
     * Their existing owners remain responsible for those operations.
     */
    public void stopSession()
    {
        if(!shutdownRequested.compareAndSet(false, true))
        {
            return;
        }

        emulatorRunning = false;

        stopForeground(true);

        NotificationManager manager =
            (NotificationManager)getSystemService(
                Context.NOTIFICATION_SERVICE);

        if(manager != null)
        {
            manager.cancel(NOTIFICATION_ID);
        }

        stopSelf();
    }

    private void startForegroundSession()
    {
        Notification notification =
            buildNotification();

        if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
        {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        }
        else
        {
            startForeground(
                NOTIFICATION_ID,
                notification);
        }

        foregroundStarted = true;
    }

    private void createNotificationChannel()
    {
        if(Build.VERSION.SDK_INT < Build.VERSION_CODES.O)
        {
            return;
        }

        NotificationManager manager =
            (NotificationManager)getSystemService(
                Context.NOTIFICATION_SERVICE);

        if(manager == null)
        {
            Log.e(TAG, "NotificationManager unavailable.");
            return;
        }

        NotificationChannel channel =
            new NotificationChannel(
                CHANNEL_ID,
                "ORBIT2SX Emulator",
                NotificationManager.IMPORTANCE_LOW);

        channel.setDescription(
            "Status of the active ORBIT2SX emulator session.");

        channel.setShowBadge(false);

        manager.createNotificationChannel(channel);
    }

    private Notification buildNotification()
    {
        Intent activityIntent =
            new Intent(this, EmulatorActivity.class);

        activityIntent.addFlags(
            Intent.FLAG_ACTIVITY_CLEAR_TOP |
            Intent.FLAG_ACTIVITY_SINGLE_TOP);

        int pendingFlags =
            PendingIntent.FLAG_UPDATE_CURRENT;

        if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
        {
            pendingFlags |= PendingIntent.FLAG_IMMUTABLE;
        }

        PendingIntent contentIntent =
            PendingIntent.getActivity(
                this,
                NOTIFICATION_ID,
                activityIntent,
                pendingFlags);

        String statusText = emulatorRunning
            ? "Emulator session running"
            : "Emulator session paused";

        Notification.Builder builder;

        if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
        {
            builder = new Notification.Builder(
                this,
                CHANNEL_ID);
        }
        else
        {
            builder = new Notification.Builder(this);
            builder.setPriority(Notification.PRIORITY_LOW);
        }

        builder
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle(gameTitle)
            .setContentText(statusText)
            .setCategory(Notification.CATEGORY_SERVICE)
            .setContentIntent(contentIntent)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setShowWhen(false);

        return builder.build();
    }

    private void updateNotification()
    {
        if(!foregroundStarted ||
           shutdownRequested.get())
        {
            return;
        }

        NotificationManager manager =
            (NotificationManager)getSystemService(
                Context.NOTIFICATION_SERVICE);

        if(manager == null)
        {
            return;
        }

        manager.notify(
            NOTIFICATION_ID,
            buildNotification());
    }

    @Override
    public void onDestroy()
    {
        emulatorRunning = false;

        backgroundTasks.shutdownNow();

        foregroundStarted = false;

        stopForeground(true);

        NotificationManager manager =
            (NotificationManager)getSystemService(
                Context.NOTIFICATION_SERVICE);

        if(manager != null)
        {
            manager.cancel(NOTIFICATION_ID);
        }

        Log.i(TAG, "EmulatorService destroyed.");

        super.onDestroy();
    }
}
