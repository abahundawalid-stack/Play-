package com.virtualapplications.play;

import android.app.Activity;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public final class AboutActivity extends Activity
{
    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        buildScreen();
    }

    private int dp(float value)
    {
        return (int)(value * getResources()
                .getDisplayMetrics().density + 0.5f);
    }

    private void buildScreen()
    {
        Date buildDate = BuildConfig.buildTime;

        String formattedDate =
                new SimpleDateFormat(
                        "yyyy/MM/dd K:mm a",
                        Locale.getDefault())
                        .format(buildDate);

        String timestamp =
                formattedDate.substring(11).startsWith("0:")
                        ? formattedDate.replace("0:", "12:")
                        : formattedDate;

        String information = String.format(
                Locale.getDefault(),
                getString(R.string.about_play_emu) + "\n\n" +
                getString(R.string.about_play_version) + " %s\n" +
                getString(R.string.about_play_date) + " %s\n\n" +
                getString(R.string.about_play_founder) +
                "\nJean-Philip Desjardins",
                BuildConfig.VERSION_NAME,
                timestamp);

        ScrollView scrollView = new ScrollView(this);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(
                dp(20), dp(24), dp(20), dp(20));

        TextView title = new TextView(this);
        title.setText(getString(R.string.about_play));
        title.setTextSize(24);

        TextView body = new TextView(this);
        body.setText(information);
        body.setTextSize(16);
        body.setPadding(0, dp(20), 0, dp(24));
        body.setTextIsSelectable(true);

        Button closeButton = new Button(this);
        closeButton.setText("Tutup");
        closeButton.setOnClickListener(view -> finish());

        root.addView(title);
        root.addView(body);
        root.addView(closeButton);

        scrollView.addView(root);
        setContentView(scrollView);
        setTitle(R.string.about_play);
    }
}
