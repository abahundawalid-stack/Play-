package com.virtualapplications.play;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;

public final class ActivityNavigator
{
    public static final String EXTRA_GAME_PATH =
            "com.virtualapplications.play.extra.GAME_PATH";

    public static final String EXTRA_GAME_TITLE =
            "com.virtualapplications.play.extra.GAME_TITLE";

    public static final String EXTRA_GAME_OVERVIEW =
            "com.virtualapplications.play.extra.GAME_OVERVIEW";

    public static final String EXTRA_GAME_DISC_ID =
            "com.virtualapplications.play.extra.GAME_DISC_ID";

    private ActivityNavigator()
    {
    }

    private static void startActivity(
            Context context,
            Intent intent)
    {
        if(context == null || intent == null)
        {
            throw new IllegalArgumentException(
                    "Context and Intent must not be null.");
        }

        if(!(context instanceof Activity))
        {
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        }

        context.startActivity(intent);
    }

    public static void openSettings(
            Activity activity,
            int requestCode)
    {
        if(activity == null)
            throw new IllegalArgumentException(
                    "Activity must not be null.");

        Intent intent = new Intent(
                activity,
                SettingsActivity.class);

        activity.startActivityForResult(
                intent,
                requestCode);
    }

    public static void openFileBrowser(
            Activity activity,
            int requestCode)
    {
        if(activity == null)
            throw new IllegalArgumentException(
                    "Activity must not be null.");

        Intent intent = new Intent(
                activity,
                FileBrowserActivity.class);

        activity.startActivityForResult(
                intent,
                requestCode);
    }

    public static void openDocumentTreePicker(
            Activity activity,
            int requestCode)
    {
        if(activity == null)
            throw new IllegalArgumentException(
                    "Activity must not be null.");

        Intent intent =
                new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);

        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        intent.putExtra(Intent.EXTRA_LOCAL_ONLY, true);

        activity.startActivityForResult(
                intent,
                requestCode);
    }

    public static void openGameLibrary(Context context)
    {
        startActivity(
                context,
                new Intent(context, GameLibraryActivity.class));
    }

    public static void openGameDetails(
            Context context,
            Bootable game)
    {
        if(context == null || game == null ||
           game.path == null || game.path.trim().isEmpty())
        {
            throw new IllegalArgumentException(
                    "A valid game and context are required.");
        }

        Intent intent = new Intent(
                context,
                GameDetailsActivity.class);

        intent.putExtra(EXTRA_GAME_PATH, game.path);
        intent.putExtra(EXTRA_GAME_TITLE, game.title);
        intent.putExtra(EXTRA_GAME_OVERVIEW, game.overview);
        intent.putExtra(EXTRA_GAME_DISC_ID, game.discId);

        startActivity(context, intent);
    }

    public static void openAbout(Context context)
    {
        startActivity(
                context,
                new Intent(context, AboutActivity.class));
    }

    public static void openDiagnostics(Activity activity)
    {
        startActivity(
                activity,
                new Intent(activity, DiagnosticsActivity.class));
    }

    public static void openForensicAnalyzer(Activity activity)
    {
        startActivity(
                activity,
                new Intent(activity, ForensicAnalyzerActivity.class));
    }
}
