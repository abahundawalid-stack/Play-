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

        final Context _ctx;
        final String _bootablePath;
        final OnGameLaunchCompleteListener _completeListener;

        Exception _exception;

        public GameLauncher(
            Context ctx,
            String bootablePath,
            OnGameLaunchCompleteListener completeListener)
        {
            _ctx = ctx;
            _bootablePath = bootablePath;
            _completeListener = completeListener;
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
            try
            {
                Context appContext =
                    _ctx.getApplicationContext();

                // Initialize Android resources only when
                // a game is actually being launched.
                NativeInterop.setFilesDirPath(
                    appContext.getFilesDir().getAbsolutePath());

                NativeInterop.setCacheDirPath(
                    appContext.getCacheDir().getAbsolutePath());

                NativeInterop.setAssetManager(
                    appContext.getAssets());

                NativeInterop.setContentResolver(
                    appContext.getContentResolver());

                // Create the native PS2 virtual machine lazily.
                if(!NativeInterop.isVirtualMachineCreated())
                {
                    NativeInterop.createVirtualMachine();
                }

                DiagnosticsManager.beginSession(
                    _ctx,
                    _bootablePath);

                if(BootablesInterop.IsBootableExecutablePath(
                    _bootablePath))
                {
                    NativeInterop.loadElf(_bootablePath);
                }
                else
                {
                    NativeInterop.bootDiskImage(_bootablePath);
                }

                Intent intent = new Intent(
                    _ctx,
                    EmulatorActivity.class);

                intent.putExtra(
                    "diagnostic_game_path",
                    _bootablePath);

                intent.addFlags(
                    Intent.FLAG_ACTIVITY_NEW_TASK);

                _ctx.startActivity(intent);
            }
            catch(Exception e)
            {
                DiagnosticsManager.finishSession(_ctx);
                _exception = e;
            }

            return null;
        }

        @Override
        protected void onPostExecute(Void result)
        {
            if(progDialog != null && progDialog.isShowing())
            {
                try
                {
                    progDialog.dismiss();
                }
                catch(Exception e)
                {
                    // Ignore dialog dismissal errors.
                }
            }

            if(_exception != null)
            {
                displaySimpleMessage(
                    "Error",
                    _exception.getMessage() != null
                        ? _exception.getMessage()
                        : "Unable to initialize or launch the game.");
            }
            else
            {
                if(_completeListener != null)
                {
                    _completeListener.onComplete();
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
