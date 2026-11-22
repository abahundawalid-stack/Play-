package com.virtualapplications.play;

import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Tracks process-level UI visibility; it does not own Activity instances. */
public final class ApplicationState
{
    public enum ProcessState
    {
        CREATED,
        FOREGROUND,
        BACKGROUND,
        SHUTTING_DOWN
    }

    private final AtomicReference<ProcessState> state =
        new AtomicReference<>(ProcessState.CREATED);

    private final AtomicInteger startedActivityCount =
        new AtomicInteger(0);

    public ProcessState getProcessState()
    {
        return state.get();
    }

    public int getStartedActivityCount()
    {
        return startedActivityCount.get();
    }

    void onActivityStarted()
    {
        int count = startedActivityCount.incrementAndGet();
        if(count > 0)
            state.set(ProcessState.FOREGROUND);
    }

    void onActivityStopped()
    {
        int count;
        do
        {
            int current = startedActivityCount.get();
            count = current > 0 ? current - 1 : 0;
            if(startedActivityCount.compareAndSet(current, count))
                break;
        }
        while(true);

        state.set(count > 0
            ? ProcessState.FOREGROUND
            : ProcessState.BACKGROUND);
    }

    void markShuttingDown()
    {
        state.set(ProcessState.SHUTTING_DOWN);
    }
}
