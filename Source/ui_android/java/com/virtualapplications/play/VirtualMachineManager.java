package com.virtualapplications.play;

import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.Context;
import android.content.Intent;
import android.os.AsyncTask;

public class VirtualMachineManager
{
    public interface OnGameLaunchCompleteListener
    {
        void onComplete();
    }

    private static final class GameLauncher
        extends AsyncTask<Void, Void, Void>
    {
        private ProgressDialog progDialog;

        private final Context _ctx;
        private final String _bootablePath;
        private final OnGameLaunchCompleteListener _completeListener;

        private Exception _exception;
        private Intent _emulatorIntent;
        private boolean _bootCompleted = false;

        public GameLauncher(
            Context ctx,
            String bootablePath,
            OnGameLaunchCompleteListener completeListener)
        {
            _ctx = ctx;
            _bootablePath = bootablePath;
            _completeListener = completeListener;
        }

        private void setLaunchStage(String stage)
        {
            DiagnosticsManager.recordLaunchStage(
                _ctx,
                stage);
        }

        @Override
        protected void onPreExecute()
        {
            progDialog = ProgressDialog.show(
                _ctx,
                _ctx.getString(R.string.launch_game),
                _ctx.getString(R.string.launch_game_msg),
                true);
        }
      
        @Override
        protected Void doInBackground(Void... params)
        {
            EmulatorCoreInterface core = null;

            try
            {
                core = EmulatorCoreInterface.from(_ctx);

                core.beginSession(_bootablePath);

                setLaunchStage(
                    "INITIALIZING_ANDROID_RESOURCES");

                core.initializePlatformResources();

                setLaunchStage(
                    "CREATING_VIRTUAL_MACHINE");

                core.initialize();

                if(!core.isVirtualMachineCreated())
                {
                    throw new IllegalStateException(
                        "Virtual machine was not created.");
                }

                setLaunchStage(
                    "VIRTUAL_MACHINE_READY");

                setLaunchStage(
                    "CHECKING_BOOTABLE_TYPE");

                setLaunchStage(
                    "COORDINATING_GAME_BOOT");

                core.boot(_bootablePath);

                setLaunchStage(
                    "GAME_BOOT_RETURNED");

                _emulatorIntent = new Intent(
                    _ctx,
                    EmulatorActivity.class);

                _emulatorIntent.putExtra(
                    "diagnostic_game_path",
                    _bootablePath);

                _bootCompleted = true;
            }
            catch(Exception error)
            {
                _exception = error;

                if(core != null)
                {
                    core.recordFailure(
                        "LAUNCH_FAILED",
                        error);

                    core.finishSession();
                }
                else
                {
                    DiagnosticsManager.finishSession(_ctx);
                }

                setLaunchStage(
                    "LAUNCH_FAILED: " + error);
            }

            return null;
        }

        @Override
        protected void onPostExecute(Void result)
        {
            dismissProgressDialog();

            if(_exception != null)
            {
                displaySimpleMessage(
                    "Game Launch Failed",
                    _exception.getMessage() != null
                        ? _exception.getMessage()
                        : _exception.toString());

                return;
            }

            if(!_bootCompleted || _emulatorIntent == null)
            {
                setLaunchStage(
                    "LAUNCH_FAILED: Emulator intent was not prepared");

                EmulatorCoreInterface.from(_ctx).finishSession();

                displaySimpleMessage(
                    "Game Launch Failed",
                    "The emulator launch could not be completed.");

                return;
            }

            try
            {
                setLaunchStage(
                    "OPENING_EMULATOR_ACTIVITY");

                // Activity dibuka di UI thread setelah
                // dialog Loading Play Game ditutup.
                _ctx.startActivity(_emulatorIntent);

                setLaunchStage(
                    "EMULATOR_ACTIVITY_START_REQUESTED");

                if(_completeListener != null)
                {
                    _completeListener.onComplete();
                }
            }
            catch(Exception e)
            {
                setLaunchStage(
                    "EMULATOR_ACTIVITY_START_FAILED: " + e);

                EmulatorCoreInterface.from(_ctx).finishSession();

                displaySimpleMessage(
                    "Emulator Activity Error",
                    e.getMessage() != null
                        ? e.getMessage()
                        : e.toString());
            }
        }

        @Override
        protected void onCancelled()
        {
            dismissProgressDialog();

            setLaunchStage(
                "LAUNCH_CANCELLED");

            EmulatorCoreInterface.from(_ctx).finishSession();

            super.onCancelled();
        }

        private void dismissProgressDialog()
        {
            if(progDialog != null && progDialog.isShowing())
            {
                try
                {
                    progDialog.dismiss();
                }
                catch(Exception ignored)
                {
                    // Dialog may already have been dismissed.
                }
            }
        }

        private void displaySimpleMessage(
            String title,
            String message)
        {
            new AlertDialog.Builder(_ctx)
                .setTitle(title)
                .setMessage(message)
                .setPositiveButton(
                    android.R.string.ok,
                    null)
                .setOnDismissListener(dialog ->
                {
                    if(_completeListener != null)
                    {
                        _completeListener.onComplete();
                    }
                })
                .create()
                .show();
        }
    }

    public static void launchGame(
        Context ctx,
        String bootablePath,
        OnGameLaunchCompleteListener completeListener)
    {
        GameLauncher launcher = new GameLauncher(
            ctx,
            bootablePath,
            completeListener);

        launcher.execute();
    }
}
