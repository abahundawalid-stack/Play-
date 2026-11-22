package com.virtualapplications.play;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

/** Runs explicitly registered process-level cleanup actions once, in reverse order. */
public final class ApplicationShutdownCoordinator
{
    public interface ShutdownAction
    {
        void shutdown() throws Exception;
    }

    private final List<ShutdownAction> actions = new ArrayList<>();
    private final AtomicBoolean shutdown = new AtomicBoolean(false);

    public synchronized void register(ShutdownAction action)
    {
        if(action == null)
            throw new IllegalArgumentException(
                "Shutdown action must not be null.");

        if(shutdown.get())
            throw new IllegalStateException(
                "Shutdown has already started.");

        actions.add(action);
    }

    public void shutdown()
    {
        if(!shutdown.compareAndSet(false, true))
            return;

        List<ShutdownAction> pending;
        synchronized(this)
        {
            pending = new ArrayList<>(actions);
            actions.clear();
        }

        for(int index = pending.size() - 1; index >= 0; index--)
        {
            try
            {
                pending.get(index).shutdown();
            }
            catch(Exception error)
            {
                ApplicationLogger.error(
                    Constants.TAG,
                    "Application shutdown action failed.", error);
            }
        }
    }

    public boolean isShutdown()
    {
        return shutdown.get();
    }
}
