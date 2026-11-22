package com.virtualapplications.play;

import android.content.Context;
import android.view.Surface;

/**
 * Central Android-facing interface for the native emulator core.
 *
 * This class coordinates lifecycle and access to existing components.
 * It does not own CPS2VM, GS handlers, EGL objects, or native windows.
 *
 * Native ownership remains in the existing C++ implementation.
 */
public final class EmulatorCoreInterface
{
    public enum State
    {
        UNINITIALIZED,
        INITIALIZING,
        READY,
        BOOTING,
        RUNNING,
        PAUSED,
        SHUTTING_DOWN,
        SHUTDOWN,
        ERROR
    }

    private final Context applicationContext;

    private final ApplicationConfiguration applicationConfiguration;

    private volatile State state = State.UNINITIALIZED;

    private volatile String currentBootablePath;

    private volatile String lastError;

    private boolean platformResourcesInitialized;

    private boolean sessionActive;

    EmulatorCoreInterface(
        Context context,
        ApplicationConfiguration configuration)
    {
        if(context == null || configuration == null)
        {
            throw new IllegalArgumentException(
                "Context and configuration must not be null.");
        }

        applicationContext = context.getApplicationContext();

        applicationConfiguration = configuration;
    }

    /**
     * Retrieves the process-wide core interface.
     */
    public static EmulatorCoreInterface from(Context context)
    {
        if(context == null)
        {
            throw new IllegalArgumentException(
                "Context must not be null.");
        }

        Context appContext = context.getApplicationContext();

        if(!(appContext instanceof PlayApplication))
        {
            throw new IllegalStateException(
                "PlayApplication is not the active application.");
        }

        PlayApplication application =
            (PlayApplication)appContext;

        ApplicationServiceRegistry services =
            application.getServices();

        if(services == null)
        {
            throw new IllegalStateException(
                "Application services are not initialized.");
        }

        return services.get(EmulatorCoreInterface.class);
    }

    public synchronized State getState()
    {
        return state;
    }

    public String getLastError()
    {
        return lastError;
    }

    public String getCurrentBootablePath()
    {
        return currentBootablePath;
    }

    public ApplicationConfiguration getApplicationConfiguration()
    {
        return applicationConfiguration;
    }

    /**
     * Initializes Android resources required by the native bridge.
     *
     * This operation is idempotent for the lifetime of this process.
     */
    public synchronized void initializePlatformResources()
    {
        if(platformResourcesInitialized)
        {
            return;
        }

        try
        {
            NativeInterop.setFilesDirPath(
                applicationContext.getFilesDir().getAbsolutePath());

            NativeInterop.setCacheDirPath(
                applicationContext.getCacheDir().getAbsolutePath());

            NativeInterop.setAssetManager(
                applicationContext.getAssets());

            NativeInterop.setContentResolver(
                applicationContext.getContentResolver());

            platformResourcesInitialized = true;
        }
        catch(RuntimeException error)
        {
            recordFailure(
                "PLATFORM_RESOURCE_INITIALIZATION_FAILED",
                error);

            throw error;
        }
        catch(LinkageError error)
        {
            recordFailure(
                "NATIVE_BRIDGE_INITIALIZATION_FAILED",
                error);

            throw new IllegalStateException(
                "Native emulator library is unavailable.",
                error);
        }
    }

    /**
     * Initializes the native virtual machine.
     */
    public synchronized void initialize()
    {
        state = State.INITIALIZING;

        try
        {
            initializePlatformResources();

            if(!NativeInterop.isVirtualMachineCreated())
            {
                NativeInterop.createVirtualMachine();
            }

            if(!NativeInterop.isVirtualMachineCreated())
            {
                throw new IllegalStateException(
                    "Native virtual machine was not created.");
            }

            lastError = null;

            state = State.READY;

            DiagnosticsManager.recordLaunchStage(
                applicationContext,
                "EMULATOR_CORE_INITIALIZED");
        }
        catch(RuntimeException error)
        {
            recordFailure(
                "EMULATOR_CORE_INITIALIZATION_FAILED",
                error);

            throw error;
        }
        catch(LinkageError error)
        {
            recordFailure(
                "EMULATOR_CORE_NATIVE_LINK_FAILED",
                error);

            throw new IllegalStateException(
                "Unable to initialize native emulator core.",
                error);
        }
    }

    /**
     * Coordinates ELF or disc-image boot.
     *
     * Bootable detection remains owned by BootablesInterop.
     */
    public synchronized void boot(String bootablePath)
    {
        if(bootablePath == null ||
           bootablePath.trim().isEmpty())
        {
            throw new IllegalArgumentException(
                "Bootable path must not be empty.");
        }

        initialize();

        state = State.BOOTING;

        try
        {
            boolean executable =
                BootablesInterop.IsBootableExecutablePath(
                    bootablePath);

            if(executable)
            {
                DiagnosticsManager.recordLaunchStage(
                    applicationContext,
                    "CORE_LOADING_ELF");

                NativeInterop.loadElf(bootablePath);
            }
            else
            {
                DiagnosticsManager.recordLaunchStage(
                    applicationContext,
                    "CORE_BOOTING_DISC_IMAGE");

                NativeInterop.bootDiskImage(bootablePath);
            }

            currentBootablePath = bootablePath;

            lastError = null;

            state = State.READY;

            DiagnosticsManager.recordLaunchStage(
                applicationContext,
                "CORE_BOOT_COMPLETED");
        }
        catch(RuntimeException error)
        {
            recordFailure(
                "CORE_BOOT_FAILED",
                error);

            throw error;
        }
        catch(LinkageError error)
        {
            recordFailure(
                "CORE_BOOT_NATIVE_LINK_FAILED",
                error);

            throw new IllegalStateException(
                "Native boot operation failed.",
                error);
        }
    }

    public synchronized boolean isVirtualMachineCreated()
    {
        return NativeInterop.isVirtualMachineCreated();
    }

    /**
     * Starts or resumes native VM execution.
     */
    public synchronized void resume()
    {
        ensureVirtualMachine();

        try
        {
            NativeInterop.resumeVirtualMachine();

            state = State.RUNNING;

            DiagnosticsManager.recordLaunchStage(
                applicationContext,
                "CORE_RESUME_REQUESTED");
        }
        catch(RuntimeException error)
        {
            recordFailure("CORE_RESUME_FAILED", error);
            throw error;
        }
    }

    /**
     * Pauses native VM execution.
     */
    public synchronized void pause()
    {
        if(!NativeInterop.isVirtualMachineCreated())
        {
            return;
        }

        try
        {
            NativeInterop.pauseVirtualMachine();

            state = State.PAUSED;

            DiagnosticsManager.recordLaunchStage(
                applicationContext,
                "CORE_PAUSE_REQUESTED");
        }
        catch(RuntimeException error)
        {
            recordFailure("CORE_PAUSE_FAILED", error);
            throw error;
        }
    }

    /**
     * Resets the existing native VM without destroying its ownership.
     */
    public synchronized void reset()
    {
        ensureVirtualMachine();

        try
        {
            NativeInterop.resetVirtualMachine();

            state = State.PAUSED;

            DiagnosticsManager.recordLaunchStage(
                applicationContext,
                "CORE_RESET_COMPLETED");
        }
        catch(RuntimeException error)
        {
            recordFailure("CORE_RESET_FAILED", error);
            throw error;
        }
    }

    /**
     * Passes an Android Surface to the existing native GS handler.
     *
     * The native renderer remains responsible for ANativeWindow ownership.
     */
    public synchronized void attachSurface(
        Surface surface,
        int width,
        int height,
        float refreshRate)
    {
        if(!NativeInterop.isVirtualMachineCreated())
        {
            return;
        }

        NativeInterop.setupGsHandler(
            surface,
            width,
            height,
            refreshRate);
    }

    /**
     * Detaches the native rendering surface.
     */
    public synchronized void detachSurface()
    {
        if(!NativeInterop.isVirtualMachineCreated())
        {
            return;
        }

        NativeInterop.setupGsHandler(
            null,
            0,
            0,
            0.0f);
    }

    public synchronized boolean hasState(int slot)
    {
        if(!NativeInterop.isVirtualMachineCreated())
        {
            return false;
        }

        return NativeInterop.hasState(slot);
    }

    public synchronized void saveState(int slot)
    {
        ensureVirtualMachine();

        NativeInterop.saveState(slot);
    }

    public synchronized void loadState(int slot)
    {
        ensureVirtualMachine();

        NativeInterop.loadState(slot);
    }

    /**
     * Recovery leaves the VM paused.
     * The caller decides when it is safe to resume execution.
     */
    public synchronized void recoverFromState(int slot)
    {
        ensureVirtualMachine();

        try
        {
            pause();

            if(!hasState(slot))
            {
                throw new IllegalStateException(
                    "Requested recovery state does not exist.");
            }

            loadState(slot);

            state = State.PAUSED;

            DiagnosticsManager.recordLaunchStage(
                applicationContext,
                "CORE_STATE_RECOVERY_COMPLETED");
        }
        catch(RuntimeException error)
        {
            recordFailure(
                "CORE_STATE_RECOVERY_FAILED",
                error);

            throw error;
        }
    }

    /**
     * Native reset, Surface detach, and VM destruction are performed
     * in their required order.
     */
    public synchronized void shutdown()
    {
        if(!NativeInterop.isVirtualMachineCreated())
        {
            state = State.SHUTDOWN;
            currentBootablePath = null;
            return;
        }

        state = State.SHUTTING_DOWN;

        Throwable failure = null;

        try
        {
            NativeInterop.pauseVirtualMachine();
        }
        catch(RuntimeException | LinkageError error)
        {
            failure = error;
        }

        try
        {
            NativeInterop.setupGsHandler(
                null,
                0,
                0,
                0.0f);
        }
        catch(RuntimeException | LinkageError error)
        {
            if(failure == null)
            {
                failure = error;
            }
        }

        try
        {
            NativeInterop.destroyVirtualMachine();
        }
        catch(RuntimeException | LinkageError error)
        {
            if(failure == null)
            {
                failure = error;
            }
        }

        if(failure != null)
        {
            recordFailure(
                "CORE_SHUTDOWN_FAILED",
                failure);

            rethrow(failure);
            return;
        }

        currentBootablePath = null;

        lastError = null;

        state = State.SHUTDOWN;

        DiagnosticsManager.recordLaunchStage(
            applicationContext,
            "CORE_SHUTDOWN_COMPLETED");
    }

    /**
     * Session management delegates persistence and reporting
     * to DiagnosticsManager.
     */
    public synchronized void beginSession(String gamePath)
    {
        DiagnosticsManager.beginSession(
            applicationContext,
            gamePath);

        sessionActive = true;
    }

    public synchronized void finishSession()
    {
        if(!sessionActive)
        {
            return;
        }

        DiagnosticsManager.finishSession(
            applicationContext);

        sessionActive = false;
    }

    public void recordLaunchStage(String stage)
    {
        DiagnosticsManager.recordLaunchStage(
            applicationContext,
            stage);
    }

    public synchronized void recordFailure(
        String stage,
        Throwable error)
    {
        String message = error == null
            ? "Unknown emulator core failure."
            : error.getClass().getSimpleName() +
              ": " + String.valueOf(error.getMessage());

        lastError = message;

        state = State.ERROR;

        try
        {
            DiagnosticsManager.recordLaunchStage(
                applicationContext,
                stage + ": " + message);
        }
        catch(Throwable ignored)
        {
            // Diagnostic failure must not hide the original error.
        }
    }

    /**
     * Native emulator configuration remains owned by SettingsManager.
     */
    public boolean getPreferenceBoolean(String key)
    {
        return SettingsManager.getPreferenceBoolean(key);
    }

    public void setPreferenceBoolean(
        String key,
        boolean value)
    {
        SettingsManager.setPreferenceBoolean(key, value);
    }

    public int getPreferenceInteger(String key)
    {
        return SettingsManager.getPreferenceInteger(key);
    }

    public void setPreferenceInteger(
        String key,
        int value)
    {
        SettingsManager.setPreferenceInteger(key, value);
    }

    public void saveConfiguration()
    {
        SettingsManager.save();
    }

    /**
     * Performance monitoring delegates to StatsManager.
     */
    public int getFrames()
    {
        return StatsManager.getFrames();
    }

    public int getDrawCalls()
    {
        return StatsManager.getDrawCalls();
    }

    public boolean isProfiling()
    {
        return StatsManager.isProfiling();
    }

    public String getProfilingInfo()
    {
        return StatsManager.getProfilingInfo();
    }

    public String getGsPipelineDiagnostics()
    {
        return StatsManager.getGsPipelineDiagnostics();
    }

    public String getRuntimeExecutionDiagnostics()
    {
        return StatsManager.getRuntimeExecutionDiagnostics();
    }

    public void clearPerformanceStats()
    {
        StatsManager.clearStats();
    }

    /**
     * Native diagnostic bridge.
     */
    public String[] getGraphicsDiagnostics()
    {
        return NativeInterop.getGraphicsDiagnostics();
    }

    public long[] getAudioDiagnostics()
    {
        return NativeInterop.getAudioDiagnostics();
    }

    public synchronized void notifyPreferencesChanged()
    {
        if(NativeInterop.isVirtualMachineCreated())
        {
            NativeInterop.notifyPreferencesChanged();
        }
    }

    private void ensureVirtualMachine()
    {
        if(!NativeInterop.isVirtualMachineCreated())
        {
            throw new IllegalStateException(
                "Native virtual machine is not initialized.");
        }
    }

    private static void rethrow(Throwable error)
    {
        if(error instanceof Error)
        {
            throw (Error)error;
        }

        if(error instanceof RuntimeException)
        {
            throw (RuntimeException)error;
        }

        throw new IllegalStateException(error);
    }
}
