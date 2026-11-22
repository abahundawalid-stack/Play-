package com.virtualapplications.play;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Toast;

public final class FileBrowserActivity extends Activity
{
    private static final int REQUEST_SELECT_FOLDER = 0xF11E;

    private static final String STATE_PICKER_STARTED =
            "play.filebrowser.picker_started";

    private boolean pickerStarted;

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        if(savedInstanceState != null)
        {
            pickerStarted = savedInstanceState.getBoolean(
                    STATE_PICKER_STARTED,
                    false);
        }

        if(!pickerStarted)
            launchFolderPicker();
    }

    private void launchFolderPicker()
    {
        pickerStarted = true;

        try
        {
            ActivityNavigator.openDocumentTreePicker(
                    this,
                    REQUEST_SELECT_FOLDER);
        }
        catch(Exception error)
        {
            Toast.makeText(
                    this,
                    "Tidak dapat membuka pemilih folder: " +
                            error.getMessage(),
                    Toast.LENGTH_LONG)
                    .show();

            setResult(RESULT_CANCELED);
            finish();
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle outState)
    {
        outState.putBoolean(
                STATE_PICKER_STARTED,
                pickerStarted);

        super.onSaveInstanceState(outState);
    }

    @Override
    protected void onActivityResult(
            int requestCode,
            int resultCode,
            Intent data)
    {
        super.onActivityResult(
                requestCode,
                resultCode,
                data);

        if(requestCode != REQUEST_SELECT_FOLDER)
            return;

        if(resultCode == RESULT_OK &&
           data != null &&
           data.getData() != null)
        {
            setResult(RESULT_OK, data);
        }
        else
        {
            setResult(RESULT_CANCELED);
        }

        finish();
    }
}
