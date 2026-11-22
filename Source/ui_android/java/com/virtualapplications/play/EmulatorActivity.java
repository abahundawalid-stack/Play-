package com.virtualapplications.play;

import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.TimeUnit;
import android.app.*;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.*;

import androidx.drawerlayout.widget.DrawerLayout;

import android.util.*;
import android.view.*;
import android.widget.*;
import android.widget.Toast;

import static com.virtualapplications.play.Constants.PREF_EMU_GENERAL_SHOWFPS;
import static com.virtualapplications.play.Constants.PREF_EMU_GENERAL_SHOWVIRTUALPAD;

public class EmulatorActivity extends Activity
{
    private static final int AUTOSAVE_SLOT = 99;
    private static final long AUTOSAVE_INTERVAL_MS = 60000;

    private final ExecutorService _autosaveExecutor =
    Executors.newSingleThreadExecutor();

    private final AtomicBoolean _autosavePending =
    new AtomicBoolean(false);

    private final Runnable _autosaveRunnable = new Runnable()
    {
        @Override
        public void run()
        {
            if(!_activityRunning || !_surfaceCreated)
            {
                return;
            }

            if(NativeInterop.isVirtualMachineCreated())
            {
                requestAutosave();
            }

            // Tetap jadwalkan ulang selama Activity aktif.
            _statsTimerHandler.postDelayed(
            this, AUTOSAVE_INTERVAL_MS);
        }
    };

    private void promptAutosaveRecovery()
    {
        if(_resumePromptShown)
        {
            return;
        }

        _resumePromptShown = true;

        if(!NativeInterop.hasState(AUTOSAVE_SLOT))
        {
            return;
        }

        new AlertDialog.Builder(this)
            .setTitle("Autosave ditemukan")
            .setMessage(
                "Ditemukan penyimpanan otomatis untuk game ini. " +
                "Apakah kamu ingin melanjutkan permainan terakhir?")
            .setPositiveButton("Continue", (dialog, which) ->
            {
    NativeInterop.pauseVirtualMachine();

                new Thread(() ->
                {
                    try
                    {
    NativeInterop.loadState(AUTOSAVE_SLOT);

                        runOnUiThread(() ->
                        {
                            if(!_activityRunning ||
                                isFinishing())
                            {
                                return;
                            }

    NativeInterop.resumeVirtualMachine();

    Toast.makeText(
                                this,
                                "Autosave berhasil dimuat.",
    Toast.LENGTH_SHORT
                            ).show();
                        });
                    }
                    catch(Exception e)
                    {
                        Log.e(
    Constants.TAG,
                            "Autosave recovery failed",
                            e
                        );

                        runOnUiThread(() ->
                        {
                          if(!_activityRunning ||
                             !_surfaceCreated ||
                             isFinishing() ||
                             isDestroyed())
                          {
                              return;
                          }

    NativeInterop.resumeVirtualMachine();

        new AlertDialog.Builder(this)
            .setTitle("Gagal memuat autosave")
            .setMessage(
                  "Penyimpanan otomatis tidak dapat dimuat. " +
                  "Permainan akan dilanjutkan dari awal.")
            .setPositiveButton("OK", null)
            .show();
    });
                    }
                }).start();
            })
            .setNegativeButton("Restart", null)
            .setCancelable(false)
            .show();
    }
    
    private void requestAutosave()
    {
        if(!NativeInterop.isVirtualMachineCreated())
        {
            return;
        }

        if(!_autosavePending.compareAndSet(false, true))
        {
            return;
        }

        try
        {
           _autosaveExecutor.execute(() ->
           {
               try
               {
    NativeInterop.saveState(AUTOSAVE_SLOT);
                  Log.i(Constants.TAG, "Autosave completed.");
               }
               catch(Exception e)
               {
                  Log.e(Constants.TAG, "Autosave failed", e);
               }
               finally
               {
    _autosavePending.set(false);
               }
           });
        }
        catch(Exception e)
        {
            _autosavePending.set(false);
            Log.e(Constants.TAG, "Autosave queue failed", e);
        }
    }
    
    private static final int _settingsIntentRequestCode = 0xBEEF;

    private final Handler _statsTimerHandler = new Handler();
    private Runnable _statsTimerRunnable;

    private TextView _fpsTextView;
    private TextView _profileTextView;

    private boolean _surfaceCreated = false;
    private boolean _activityRunning = false;
    private boolean _resumePromptShown = false;
    private boolean _inMultiWindowMode = false;

    private EmulatorServiceController _serviceController;

    private String _serviceGameTitle = "Play! Emulator";

    protected EmulatorDrawerFragment _drawerFragment;

    public static void RegisterPreferences()
    {
        SettingsManager.registerPreferenceBoolean(
            PREF_EMU_GENERAL_SHOWFPS, false);

        SettingsManager.registerPreferenceBoolean(
            PREF_EMU_GENERAL_SHOWVIRTUALPAD, true);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        _serviceController =
        new EmulatorServiceController(this);

        String serviceGamePath =
        getIntent().getStringExtra("diagnostic_game_path");

        if(serviceGamePath != null &&
           !serviceGamePath.trim().isEmpty())
        {
            _serviceGameTitle =
                DiagnosticsManager.getGameName(
                    this,
                    serviceGamePath);
        }

        _resumePromptShown =
        ActivityStateController.restoreEmulatorResumePromptShown(
                savedInstanceState,
                false);

        if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.N)
        {
            _inMultiWindowMode = isInMultiWindowMode();
        }

        DiagnosticsManager.recordLaunchStage(
            this,
            "EMULATOR_ACTIVITY_CREATED");

        ThemeManager.applyTheme(this, null);
        setContentView(R.layout.emulator);
        setupWindow();

        _drawerFragment = (EmulatorDrawerFragment)
            getFragmentManager().findFragmentById(
                R.id.emulator_drawer);

        _drawerFragment.setEventListener(
            new EmulatorDrawerFragment.EventListener()
            {
                @Override
                public void onExitSelected()
                {
                    finish();
                }

                @Override
                public void onSaveStateSelected()
                {
                    int messageStringId =
                        R.string.emulator_save_state_ok;

                    try
                    {
                        NativeInterop.saveState(0);
                    }
                    catch(Exception e)
                    {
                        messageStringId =
                            R.string.emulator_save_state_fail;
                    }

                    Toast.makeText(
                        getApplicationContext(),
                        getString(messageStringId),
                        Toast.LENGTH_SHORT
                    ).show();

                    _drawerFragment.closeDrawer();
                }

                @Override
                public void onLoadStateSelected()
                {
                    int messageStringId =
                        R.string.emulator_load_state_ok;

                    try
                    {
                        NativeInterop.loadState(0);
                    }
                    catch(Exception e)
                    {
                        messageStringId =
                            R.string.emulator_load_state_fail;
                    }

                    Toast.makeText(
                        getApplicationContext(),
                        getString(messageStringId),
                        Toast.LENGTH_SHORT
                    ).show();

                    _drawerFragment.closeDrawer();
                }

                @Override
                public void onSettingsSelected()
                {
                    ActivityNavigator.openSettings(
                            EmulatorActivity.this,
                            _settingsIntentRequestCode);
                }
            }
        );

        View fragmentView =
            findViewById(R.id.emulator_drawer);

        DrawerLayout drawerLayout =
            findViewById(R.id.emulator_drawer_layout);

        _drawerFragment.setUp(fragmentView, drawerLayout);

        _fpsTextView = findViewById(R.id.emulator_fps);
        _profileTextView = findViewById(R.id.emulator_profile);

        _statsTimerRunnable = new Runnable()
        {
            @Override
            public void run()
            {
                if(!_activityRunning)
                {
                    return;
                }

                int frames = StatsManager.getFrames();
                int drawCalls = StatsManager.getDrawCalls();

                int dcpf = frames > 0
                    ? drawCalls / frames
                    : 0;

                String profilingInfo = "";

                if(StatsManager.isProfiling())
                {
                    profilingInfo = StatsManager.getProfilingInfo();

                    _profileTextView.setText(profilingInfo);
                }

                DiagnosticsManager.recordSample(
                    EmulatorActivity.this,
                    frames,
                    drawCalls,
                    profilingInfo
                );

                _fpsTextView.setText(
                    String.format(
                        Locale.US,
                        "%d f/s, %d dc/f",
                        frames,
                        dcpf
                    )
                );

                StatsManager.clearStats();             

                if(_activityRunning)
                {
                    _statsTimerHandler.postDelayed(
                        this, 1000);
                }
            }
        };
    }

    @Override
    protected void onPostCreate(Bundle savedInstanceState)
    {
        super.onPostCreate(savedInstanceState);

        SurfaceView renderView =
            findViewById(R.id.emulator_view);

        SurfaceHolder holder = renderView.getHolder();
        holder.addCallback(new SurfaceCallback());

        updateOnScreenWidgets();
    }

    @Override
    public void onAttachedToWindow()
    {
        super.onAttachedToWindow();

        if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.P)
        {
            WindowInsets insets =
                getWindow().getDecorView().getRootWindowInsets();

            if(insets != null)
            {
                DisplayCutout displayCutout =
                    insets.getDisplayCutout();

                if(displayCutout != null)
                {
                    _fpsTextView.setX(
                        displayCutout.getSafeInsetLeft());
                }
            }
        }
    }

    @Override
    protected void onActivityResult(
        int requestCode,
        int resultCode,
        Intent data)
    {
        if(requestCode == _settingsIntentRequestCode)
        {
            updateOnScreenWidgets();
            NativeInterop.notifyPreferencesChanged();
        }

        super.onActivityResult(
            requestCode, resultCode, data);
    }

    @Override
    protected void onSaveInstanceState(Bundle outState)
    {
        ActivityStateController.saveEmulatorActivityState(
                outState,
                _resumePromptShown);

        super.onSaveInstanceState(outState);
    }
    
    @Override
    public void onWindowFocusChanged(boolean hasFocus)
    {
        super.onWindowFocusChanged(hasFocus);

        if(hasFocus)
        {
            setupWindow();
        }
    }

    @Override
    public void onPause()
    {
        _activityRunning = false;

        stopStatsTimer();
        StatsManager.clearStats();

        updateVirtualMachineState();

        if(_serviceController != null)
        {
            _serviceController.setEmulatorState(false);
        }

        // Simpan setelah VM dijeda agar kondisi terakhir ikut tersimpan.
        requestAutosave();

        _statsTimerHandler.removeCallbacks(_autosaveRunnable);
        
        super.onPause();
    }

    @Override
    protected void onStart()
    {
        super.onStart();

        if(_serviceController != null &&
           NativeInterop.isVirtualMachineCreated())
        {
            _serviceController.startAndBind(
                _serviceGameTitle);
        }
    }

    @Override
    protected void onStop()
    {
        if(_serviceController != null)
        {
            _serviceController.setEmulatorState(false);
            _serviceController.unbind();
        }

        super.onStop();
    }
    
    @Override
    public void onResume()
    {
        super.onResume();

        DiagnosticsManager.recordLaunchStage(
            this,
            "EMULATOR_ACTIVITY_RESUMED");

        _activityRunning = true;

        updateVirtualMachineState();

        if(_serviceController != null)
        {
            _serviceController.setEmulatorState(true);
        }

        startStatsTimer();
        _statsTimerHandler.removeCallbacks(_autosaveRunnable);
        _statsTimerHandler.postDelayed(
        _autosaveRunnable, AUTOSAVE_INTERVAL_MS);
    }

    @Override
    protected void onDestroy()
    {
        stopStatsTimer();

        _statsTimerHandler.removeCallbacks(_autosaveRunnable);

        _activityRunning = false;

        requestAutosave();

        _autosaveExecutor.shutdown();

        if(_serviceController != null)
        {
            _serviceController.shutdown();
            _serviceController = null;
        }

        if(isFinishing() && !isChangingConfigurations())
        {
            boolean autosaveCompleted = false;

            try
            {
                autosaveCompleted =
                    _autosaveExecutor.awaitTermination(
                        15,
                        TimeUnit.SECONDS);
            }
            catch(InterruptedException error)
            {
                Thread.currentThread().interrupt();

                Log.w(
                    Constants.TAG,
                    "Interrupted while waiting for autosave shutdown.",
                    error);
            }

            if(autosaveCompleted)
            {
                try
                {
                    if(NativeInterop.isVirtualMachineCreated())
                    {
                        NativeInterop.pauseVirtualMachine();

                        NativeInterop.setupGsHandler(
                            null,
                            0,
                            0,
                            0.0f);

                        NativeInterop.destroyVirtualMachine();
                    }
                }
                catch(Exception error)
                {
                    Log.e(
                        Constants.TAG,
                        "Native VM shutdown failed.",
                        error);
                }
            }
            else
            {
                _autosaveExecutor.shutdownNow();

                Log.e(
                    Constants.TAG,
                    "Autosave did not finish. Native VM is retained " +
                    "to avoid destroying resources during an active save.");
            }
        }

        DiagnosticsManager.finishSession(this);

        super.onDestroy();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event)
    {
        
        if(_drawerFragment.isDrawerOpened())
        {
            return super.dispatchKeyEvent(event);
        }

        int action = event.getAction();

        if(action == KeyEvent.ACTION_DOWN ||
           action == KeyEvent.ACTION_UP)
        {
            boolean pressed =
                action == KeyEvent.ACTION_DOWN;

            switch(event.getKeyCode())
            {
            case KeyEvent.KEYCODE_DPAD_UP:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_UP,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_DPAD_DOWN:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_DOWN,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_DPAD_LEFT:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_LEFT,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_DPAD_RIGHT:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_RIGHT,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_BUTTON_A:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_CROSS,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_BUTTON_B:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_CIRCLE,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_BUTTON_X:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_SQUARE,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_BUTTON_Y:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_TRIANGLE,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_BUTTON_L1:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_L1,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_BUTTON_L2:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_L2,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_BUTTON_R1:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_R1,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_BUTTON_R2:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_R2,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_BUTTON_START:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_START,
                    pressed);
                return true;

            case KeyEvent.KEYCODE_BUTTON_SELECT:
                InputManager.setButtonState(
                    InputManagerConstants.BUTTON_SELECT,
                    pressed);
                return true;
            }
        }

        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event)
    {
        int action = event.getActionMasked();

        if(action == MotionEvent.ACTION_MOVE)
        {
            float lx = event.getAxisValue(MotionEvent.AXIS_X);
            float ly = event.getAxisValue(MotionEvent.AXIS_Y);
            float rx = event.getAxisValue(MotionEvent.AXIS_Z);
            float ry = event.getAxisValue(MotionEvent.AXIS_RZ);

            InputManager.setAxisState(
                InputManagerConstants.ANALOG_LEFT_X, lx);

            InputManager.setAxisState(
                InputManagerConstants.ANALOG_LEFT_Y, ly);

            InputManager.setAxisState(
                InputManagerConstants.ANALOG_RIGHT_X, rx);

            InputManager.setAxisState(
                InputManagerConstants.ANALOG_RIGHT_Y, ry);
        }

        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    public void onBackPressed()
    {
        if(_drawerFragment.isDrawerOpened())
        {
            _drawerFragment.closeDrawer();
        }
        else
        {
            _drawerFragment.openDrawer();
        }
    }

    private void startStatsTimer()
    {
        stopStatsTimer();

        if(_activityRunning)
        {
            _statsTimerHandler.postDelayed(
                _statsTimerRunnable, 1000);
        }
    }

    private void stopStatsTimer()
    {
        _statsTimerHandler.removeCallbacks(
            _statsTimerRunnable);
    }

    private void setupWindow()
    {
        getWindow().addFlags(
            WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        if(_inMultiWindowMode)
        {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_VISIBLE);
        }
        else
        {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }
    
    private void updateVirtualMachineState()
    {
        if(!_surfaceCreated)
        {
            Log.w(Constants.TAG,
                "[ORBIT2SX_BOOT] VM state update skipped: Surface not ready.");

            DiagnosticsManager.recordLaunchStage(
                this,
                "VM_STATE_SKIPPED_NO_SURFACE");

            return;
        } 

        Log.i(Constants.TAG,
            "[ORBIT2SX_BOOT] updateVirtualMachineState activityRunning=" +
            _activityRunning + " surfaceCreated=" + _surfaceCreated);

        if(_activityRunning)
        {
            NativeInterop.resumeVirtualMachine();

            DiagnosticsManager.recordLaunchStage(
                this,
                "VM_RESUME_REQUESTED");
        }
        else
        {
            NativeInterop.pauseVirtualMachine();

            DiagnosticsManager.recordLaunchStage(
                this,
                "VM_PAUSE_REQUESTED");
        }
    }

    private void updateOnScreenWidgets()
    {
        View virtualPadView =
            findViewById(R.id.emulator_virtualpad);

        virtualPadView.setVisibility(
            SettingsManager.getPreferenceBoolean(
                PREF_EMU_GENERAL_SHOWVIRTUALPAD)
                ? View.VISIBLE
                : View.GONE
        );

        boolean fpsVisible =
            SettingsManager.getPreferenceBoolean(
                PREF_EMU_GENERAL_SHOWFPS) ||
            StatsManager.isProfiling();

        _fpsTextView.setVisibility(
            fpsVisible ? View.VISIBLE : View.GONE);

        _profileTextView.setVisibility(
            fpsVisible ? View.VISIBLE : View.GONE);

        if(_activityRunning)
        {
            startStatsTimer();
        }
        else
        {
            stopStatsTimer();
        }
    }

    private class SurfaceCallback
        implements SurfaceHolder.Callback
    {
        @Override
        public void surfaceChanged(
            SurfaceHolder holder,
            int format,
            int width,
            int height)
        {
            Surface surface = holder.getSurface();

            boolean validSurface =
                surface != null && surface.isValid();

            Log.i(Constants.TAG,
                "[ORBIT2SX_BOOT] surfaceChanged width=" + width +
                " height=" + height +
                " valid=" + validSurface);

            DiagnosticsManager.recordLaunchStage(
                EmulatorActivity.this,
                validSurface
                    ? "ANDROID_SURFACE_VALID"
                    : "ANDROID_SURFACE_INVALID");

            float refreshRate = getWindowManager()
                .getDefaultDisplay()
                .getRefreshRate();

            NativeInterop.setupGsHandler(
                surface,
                width,
                height,
                refreshRate);

            _surfaceCreated = validSurface;

            DiagnosticsManager.recordLaunchStage(
                EmulatorActivity.this,
                validSurface
                    ? "GS_SURFACE_SETUP_REQUESTED"
                    : "GS_SURFACE_SETUP_REJECTED");

            updateVirtualMachineState();

            promptAutosaveRecovery();
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder)
        {
            Surface surface = holder.getSurface();

            boolean validSurface =
                surface != null && surface.isValid();

            Log.i(Constants.TAG,
                "[ORBIT2SX_BOOT] surfaceCreated valid=" + validSurface);

            DiagnosticsManager.recordLaunchStage(
                EmulatorActivity.this,
                validSurface
                    ? "ANDROID_SURFACE_CREATED_VALID"
                    : "ANDROID_SURFACE_CREATED_INVALID");
        }

        @Override
        public void onConfigurationChanged(Configuration newConfig)
        {
            super.onConfigurationChanged(newConfig);

            ActivityLifecycleController.dispatchConfigurationChanged(
                    this,
                    newConfig);
  
            setupWindow();
            updateOnScreenWidgets();
        }

        @Override
        public void onMultiWindowModeChanged
                boolean isInMultiWindowMode,
                Configuration newConfig)
        {
            super.onMultiWindowModeChanged(
                    isInMultiWindowMode,
                    newConfig);

            _inMultiWindowMode = isInMultiWindowMode;

            ActivityLifecycleController.dispatchMultiWindowModeChanged(
                    this,
                    isInMultiWindowMode);

            ActivityLifecycleController.dispatchConfigurationChanged(
                    this,
                    newConfig);

            setupWindow();
            updateOnScreenWidgets();
        }
        
        @Override
        public void surfaceDestroyed(SurfaceHolder holder)
        {
            Log.w(Constants.TAG,
                "[ORBIT2SX_BOOT] surfaceDestroyed; pausing VM.");

            DiagnosticsManager.recordLaunchStage(
                EmulatorActivity.this,
                "ANDROID_SURFACE_DESTROYED");

            if(NativeInterop.isVirtualMachineCreated())
            {
                // 1. Hentikan eksekusi VM terlebih dahulu.
                NativeInterop.pauseVirtualMachine();

                NativeInterop.setupGsHandler(
                    null,
                    0,
                    0,
                    0.0f);
            }

            // 3. Tandai Surface sudah tidak tersedia.
            _surfaceCreated = false;

            stopStatsTimer();
        }

    }
}
