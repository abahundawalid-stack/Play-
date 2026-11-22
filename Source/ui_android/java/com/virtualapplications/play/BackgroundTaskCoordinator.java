package com.virtualapplications.play;

import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Callable;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Coordinates bounded background work owned by EmulatorService.
 *
 * This executor must never be used for VM, EGL, GS-thread, or Surface
 * operations. Those resources remain owned by their existing components.
 */
public final class BackgroundTaskCoordinator
{
    private static final int MAX_QUEUED_TASKS = 32;

    private final AtomicBoolean shutdown =
        new AtomicBoolean(false);

    private final ThreadPoolExecutor executor;

    public BackgroundTaskCoordinator()
    {
        ThreadFactory threadFactory = runnable ->
        {
            Thread thread = new Thread(
                runnable,
                "Play-EmulatorService-Background");

            thread.setDaemon(true);
            return thread;
        };

        executor = new ThreadPoolExecutor(
            1,
            1,
            0L,
            TimeUnit.MILLISECONDS,
            new ArrayBlockingQueue<Runnable>(
                MAX_QUEUED_TASKS),
            threadFactory,
            new ThreadPoolExecutor.AbortPolicy());
    }

    public Future<?> submit(Runnable task)
    {
        if(task == null)
        {
            throw new IllegalArgumentException(
                "Background task must not be null.");
        }

        ensureRunning();

        return executor.submit(task);
    }

    public <T> Future<T> submit(Callable<T> task)
    {
        if(task == null)
        {
            throw new IllegalArgumentException(
                "Background task must not be null.");
        }

        ensureRunning();

        return executor.submit(task);
    }

    public int getQueuedTaskCount()
    {
        return executor.getQueue().size();
    }

    public boolean isShutdown()
    {
        return shutdown.get();
    }

    private void ensureRunning()
    {
        if(shutdown.get())
        {
            throw new RejectedExecutionException(
                "Background task coordinator is shutting down.");
        }
    }

    /**
     * Cancels queued work and interrupts the worker when possible.
     * Tasks must cooperate with interruption.
     */
    public void shutdownNow()
    {
        if(!shutdown.compareAndSet(false, true))
        {
            return;
        }

        executor.shutdownNow();
    }
}
