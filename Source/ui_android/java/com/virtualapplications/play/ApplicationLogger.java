package com.virtualapplications.play;

import android.util.Log;

/** Process-wide Android logging facade. */
public final class ApplicationLogger
{
    private static final String TAG = "PlayApplication";

    private ApplicationLogger() {}

    public static void debug(String tag, String message)
    {
        Log.d(safeTag(tag), safeMessage(message));
    }

    public static void info(String tag, String message)
    {
        Log.i(safeTag(tag), safeMessage(message));
    }

    public static void warning(String tag, String message)
    {
        Log.w(safeTag(tag), safeMessage(message));
    }

    public static void error(String tag, String message, Throwable error)
    {
        if(error == null)
            Log.e(safeTag(tag), safeMessage(message));
        else
            Log.e(safeTag(tag), safeMessage(message), error);
    }

    private static String safeTag(String tag)
    {
        return tag == null || tag.trim().isEmpty() ? TAG : tag;
    }

    private static String safeMessage(String message)
    {
        return message == null ? "" : message;
    }
}
