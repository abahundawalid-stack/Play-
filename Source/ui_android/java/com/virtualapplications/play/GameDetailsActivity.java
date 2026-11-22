package com.virtualapplications.play;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

public final class GameDetailsActivity extends Activity
{
    private String gamePath;
    private String gameTitle;
    private String gameOverview;

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        gamePath = getIntent().getStringExtra(
                ActivityNavigator.EXTRA_GAME_PATH);

        gameTitle = getIntent().getStringExtra(
                ActivityNavigator.EXTRA_GAME_TITLE);

        gameOverview = getIntent().getStringExtra(
                ActivityNavigator.EXTRA_GAME_OVERVIEW);

        if(gamePath == null || gamePath.trim().isEmpty())
        {
            finish();
            return;
        }

        if(gameTitle == null || gameTitle.trim().isEmpty())
            gameTitle = getString(R.string.app_name);

        buildScreen();
    }

    private int dp(float value)
    {
        return (int)(value * getResources()
                .getDisplayMetrics().density + 0.5f);
    }

    private void buildScreen()
    {
        ScrollView scrollView = new ScrollView(this);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(
                dp(20), dp(20), dp(20), dp(20));

        TextView title = new TextView(this);
        title.setText(gameTitle);
        title.setTextSize(22);
        title.setTextIsSelectable(true);

        TextView path = new TextView(this);
        path.setText(gamePath);
        path.setTextSize(12);
        path.setTextIsSelectable(true);
        path.setPadding(0, dp(12), 0, dp(12));

        TextView overview = new TextView(this);
        overview.setText(
                gameOverview == null || gameOverview.trim().isEmpty()
                        ? "Tidak ada deskripsi game."
                        : gameOverview);
        overview.setTextSize(16);
        overview.setPadding(0, dp(16), 0, dp(24));

        Button launchButton = new Button(this);
        launchButton.setText("Jalankan Game");
        launchButton.setOnClickListener(
                this::launchSelectedGame);

        Button backButton = new Button(this);
        backButton.setText("Kembali");
        backButton.setOnClickListener(
                view -> finish());

        root.addView(title);
        root.addView(path);
        root.addView(overview);
        root.addView(
                launchButton,
                new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT));
        root.addView(
                backButton,
                new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT));

        scrollView.addView(root);
        setContentView(scrollView);
        setTitle(gameTitle);
    }

    private void launchSelectedGame(View view)
    {
        if(!BootablesInterop.DoesBootableExist(gamePath))
        {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.not_found)
                    .setMessage(R.string.game_unavailable)
                    .setPositiveButton(
                            android.R.string.ok,
                            null)
                    .show();

            return;
        }

        BootablesInterop.setLastBootedTime(
                gamePath,
                System.currentTimeMillis());

        Toast.makeText(
                this,
                "Menyiapkan game...",
                Toast.LENGTH_SHORT)
                .show();

        VirtualMachineManager.launchGame(
                this,
                gamePath,
                this::finish);
    }
}
