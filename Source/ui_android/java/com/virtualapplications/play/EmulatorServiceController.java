package com.virtualapplications.play;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import java.util.concurrent.Callable;
import java.util.concurrent.Future;

/**
 * Activity-side controller for starting, binding, communicating with,
 * and shutting down EmulatorService.
 *
 * Uses application Context so it does not retain an Activity.
 */
public final class EmulatorServiceController
    implements ServiceConnection
{
    private static final String TAG =
        "PlayEmulatorServiceCtl";

    private final Context applicationContext;

    private volatile EmulatorService service;

    private volatile boolean bound = false;

    private volatile boolean pendingRunning = false;

    private volatile String gameTitle =
        "Play! Emulator";

    public EmulatorServiceController(Context context)
    {
        if(context == null)
        {
            throw new IllegalArgumentException(
                "Context must not be null.");
        }

        applicationContext =
            context.getApplicationContext();
    }

    /**
     * Starts the foreground service before binding.
     * Call from a visible Activity lifecycle callback.
     */
    public void startAndBind(String title)
    {
        if(title != null && !title.trim().isEmpty())
        {
            gameTitle = title.trim();
        }

        Intent intent =
            new Intent(
                applicationContext,
                EmulatorService.class);

        intent.setAction(
            EmulatorService.ACTION_START);

        intent.putExtra(
            EmulatorService.EXTRA_GAME_TITLE,
            gameTitle);

        ComponentName startedComponent;

        try
        {
            if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            {
                startedComponent =
                    applicationContext.startForegroundService(intent);
            }
            else
            {
                startedComponent =
                    applicationContext.startService(intent);
            }
        }
        catch(RuntimeException error)
        {
            Log.e(
                TAG,
                "Unable to start EmulatorService.",
                error);

            return;
        }

        if(startedComponent == null)
        {
            Log.e(
                TAG,
                "Android did not return a started service component.");

            return;
        }

        if(!bound)
        {
            Intent bindIntent =
                new Intent(
                    applicationContext,
                    EmulatorService.class);

            try
            {
                bound = applicationContext.bindService(
                    bindIntent,
                    this,
                    Context.BIND_AUTO_CREATE);

                if(!bound)
                {
                    Log.e(
                        TAG,
                        "bindService returned false.");
                }
            }
            catch(RuntimeException error)
            {
                bound = false;

                Log.e(
                    TAG,
                    "Unable to bind EmulatorService.",
                    error);
            }
        }
    }

    public void setEmulatorState(boolean running)
    { 
        public Future<?> submitBackgroundTask(Runnable task)
        {
            EmulatorService current = service;

            if(current == null)
            {
                throw new IllegalStateException(
                    "EmulatorService is not connected.");
            }

            return current.submitBackgroundTask(task);
        } 

        public <T> Future<T> submitBackgroundTask(Callable<T> task)
        {  
            EmulatorService current = service;

            if(current == null)
            {
                throw new IllegalStateException(
                    "EmulatorService is not connected.");
            }

            return current.submitBackgroundTask(task);
        }
        
        pendingRunning = running;

        EmulatorService current = service;

        if(current != null)
        {
            current.setEmulatorState(
                pendingRunning,
                gameTitle);
        }
    }

    public boolean isBound()
    {
        return bound;
    }

    public boolean isConnected()
    {
        return service != null;
    }

    /**
     * Releases only the Activity's binding.
     * The started foreground service remains active.
     */
    public void unbind()
    {
        if(!bound)
        {
            service = null;
            return;
        }

        bound = false;
        service = null;

        try
        {
            applicationContext.unbindService(this);
        }
        catch(IllegalArgumentException error)
        {
            Log.w(
                TAG,
                "Service was already unbound.",
                error);
        }
    }

    /**
     * Stops the service session and releases the binding.
     */
    public void shutdown()
    {
        EmulatorService current = service;

        if(current != null)
        {
            current.stopSession();
        }

        unbind();

        Intent intent =
            new Intent(
                applicationContext,
                EmulatorService.class);

        applicationContext.stopService(intent);

        service = null;
    }

    @Override
    public void onServiceConnected(
        ComponentName name,
        IBinder binder)
    {
        if(!(binder instanceof EmulatorService.LocalBinder))
        {
            Log.e(
                TAG,
                "Unexpected EmulatorService binder.");
            return;
        }

        EmulatorService.LocalBinder localBinder =
            (EmulatorService.LocalBinder)binder;

        service = localBinder.getService();

        service.setEmulatorState(
            pendingRunning,
            gameTitle);

        Log.i(TAG, "EmulatorService connected.");
    }

    @Override
    public void onServiceDisconnected(ComponentName name)
    {
        service = null;

        Log.w(
            TAG,
            "EmulatorService disconnected unexpectedly.");

        if(pendingRunning)
        {
            startAndBind(gameTitle);
        }  
    }

    @Override
    public void onBindingDied(ComponentName name)
    {
        service = null;

        if(bound)
        {
            bound = false;

            try
            {
                applicationContext.unbindService(this);
            }
            catch(IllegalArgumentException ignored)
            {
            }
        }

        Log.e(
            TAG,
            "EmulatorService binding died. " +
            "A new Activity start is required to reconnect.");

        if(pendingRunning)
        {
            startAndBind(gameTitle);
        }  
    }

    @Override
    public void onNullBinding(ComponentName name)
    {
        service = null;

        if(bound)
        {
            bound = false;

            try
            {
                applicationContext.unbindService(this);
            }
            catch(IllegalArgumentException ignored)
            {
            }
        }

        Log.e(
            TAG,
            "EmulatorService returned a null binding.");
    }
}
