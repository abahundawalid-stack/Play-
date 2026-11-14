package com.virtualapplications.play;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;
import android.util.Log;

import java.io.InputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public final class AppExitDiagnostics
{
    private static final String TAG = "PlayExitDiagnostics";
    private static final String PREFS = "play_exit_diagnostics";
    private static final String KEY_REPORT = "last_report";
    private static final String KEY_TIMESTAMP = "last_timestamp";

    private AppExitDiagnostics()
    {
    }

    public static void capturePreviousExit(Context context)
    {
        if(Build.VERSION.SDK_INT < Build.VERSION_CODES.R)
        {
            return;
        }

        try
        {
            Context app = context.getApplicationContext();

            ActivityManager manager =
                (ActivityManager)app.getSystemService(
                    Context.ACTIVITY_SERVICE);

            if(manager == null)
            {
                return;
            }

            List<ApplicationExitInfo> exits =
                manager.getHistoricalProcessExitReasons(
                    app.getPackageName(),
                    0,
                    5);

            if(exits == null || exits.isEmpty())
            {
                return;
            }

            SharedPreferences prefs =
                app.getSharedPreferences(
                    PREFS,
                    Context.MODE_PRIVATE);

            long previousTimestamp =
                prefs.getLong(KEY_TIMESTAMP, 0);

            long newestTimestamp = 0;

            for(ApplicationExitInfo info : exits)
            {
                if(info != null)
                {
                    newestTimestamp = Math.max(
                        newestTimestamp,
                        info.getTimestamp());
                }
            }

            if(newestTimestamp <= previousTimestamp)
            {
                return;
            }

            StringBuilder report = new StringBuilder();

            report.append("PLAY! PROCESS EXIT DIAGNOSTICS\n");
            report.append("==============================\n\n");

            report.append("Package: ")
                .append(app.getPackageName())
                .append('\n');

            report.append("Android SDK: ")
                .append(Build.VERSION.SDK_INT)
                .append("\n\n");

            for(ApplicationExitInfo info : exits)
            {
                if(info == null)
                {
                    continue;
                }

                report.append("--------------------------------\n");

                report.append("Process: ")
                    .append(info.getProcessName())
                    .append('\n');

                report.append("PID: ")
                    .append(info.getPid())
                    .append('\n');

                report.append("Reason: ")
                    .append(reasonName(info.getReason()))
                    .append(" (")
                    .append(info.getReason())
                    .append(")\n");

                report.append("Status / signal: ")
                    .append(info.getStatus())
                    .append('\n');

                report.append("Description: ")
                    .append(info.getDescription())
                    .append('\n');

                report.append("Timestamp: ")
                    .append(formatDate(info.getTimestamp()))
                    .append('\n');

                report.append("PSS: ")
                    .append(info.getPss())
                    .append(" KB\n");

                report.append("RSS: ")
                    .append(info.getRss())
                    .append(" KB\n");

                boolean traceAvailable = false;

                try
                {
                    InputStream trace =
                        info.getTraceInputStream();

                    if(trace != null)
                    {
                        traceAvailable = true;
                        trace.close();
                    }
                }
                catch(Exception ignored)
                {
                }

                report.append("System trace available: ")
                    .append(traceAvailable)
                    .append('\n');
            }

            String output = report.toString();

            prefs.edit()
                .putString(KEY_REPORT, output)
                .putLong(KEY_TIMESTAMP, newestTimestamp)
                .apply();

            Log.e(TAG, output);
        }
        catch(Throwable error)
        {
            Log.e(
                TAG,
                "Unable to collect previous process exit information",
                error);
        }
    }

    public static String getLastReport(Context context)
    {
        SharedPreferences prefs =
            context.getApplicationContext()
                .getSharedPreferences(
                    PREFS,
                    Context.MODE_PRIVATE);

        return prefs.getString(
            KEY_REPORT,
            "Belum ada catatan proses aplikasi sebelumnya.");
    }

    private static String reasonName(int reason)
    {
        switch(reason)
        {
        case ApplicationExitInfo.REASON_UNKNOWN:
            return "UNKNOWN";

        case ApplicationExitInfo.REASON_EXIT_SELF:
            return "EXIT_SELF";

        case ApplicationExitInfo.REASON_SIGNALED:
            return "SIGNALED";

        case ApplicationExitInfo.REASON_LOW_MEMORY:
            return "LOW_MEMORY";

        case ApplicationExitInfo.REASON_CRASH:
            return "JAVA_CRASH";

        case ApplicationExitInfo.REASON_CRASH_NATIVE:
            return "NATIVE_CRASH";

        case ApplicationExitInfo.REASON_ANR:
            return "ANR";

        case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE:
            return "INITIALIZATION_FAILURE";

        case ApplicationExitInfo.REASON_PERMISSION_CHANGE:
            return "PERMISSION_CHANGE";

        case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE:
            return "EXCESSIVE_RESOURCE_USAGE";

        case ApplicationExitInfo.REASON_USER_REQUESTED:
            return "USER_REQUESTED";

        case ApplicationExitInfo.REASON_USER_STOPPED:
            return "USER_STOPPED";

        case ApplicationExitInfo.REASON_DEPENDENCY_DIED:
            return "DEPENDENCY_DIED";

        case ApplicationExitInfo.REASON_OTHER:
            return "OTHER";

        default:
            return "UNRECOGNIZED";
        }
    }

    private static String formatDate(long timestamp)
    {
        return new SimpleDateFormat(
            "yyyy-MM-dd HH:mm:ss",
            Locale.US).format(new Date(timestamp));
    }
}
