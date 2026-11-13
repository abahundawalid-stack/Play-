package com.virtualapplications.play;

import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.ConfigurationInfo;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.provider.OpenableColumns;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public final class DiagnosticsManager
{
    private static final String PREFS = "play_diagnostics";

    private DiagnosticsManager()
    {
    }

    private static android.content.SharedPreferences prefs(
        Context context)
    {
        return context.getApplicationContext()
            .getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    public static synchronized void beginSession(
        Context context,
        String gamePath)
    {
        long now = System.currentTimeMillis();

        prefs(context).edit()
            .putBoolean("running", true)
            .putString(
                "game_path",
                gamePath == null ? "" : gamePath)
            .putLong("start_time", now)
            .putLong("end_time", 0)
            .putLong("total_frames", 0)
            .putLong("total_draw_calls", 0)
            .putLong("samples", 0)
            .putLong("fps_sum", 0)
            .putInt("fps_min", Integer.MAX_VALUE)
            .putInt("fps_max", 0)
            .putInt("last_fps", 0)
            .putInt("last_draw_calls", 0)
            .putInt("last_dcpf", 0)
            .apply();
    }

    public static synchronized void recordSample(
        Context context,
        int frames,
        int drawCalls)
    {
        android.content.SharedPreferences p = prefs(context);

        if(!p.getBoolean("running", false))
        {
            return;
        }

        int safeFrames = Math.max(0, frames);
        int safeDrawCalls = Math.max(0, drawCalls);

        int dcpf = safeFrames > 0
            ? safeDrawCalls / safeFrames
            : 0;

        long samples = p.getLong("samples", 0) + 1;

        long totalFrames =
            p.getLong("total_frames", 0) + safeFrames;

        long totalDrawCalls =
            p.getLong("total_draw_calls", 0) + safeDrawCalls;

        int oldMin = p.getInt("fps_min", Integer.MAX_VALUE);
        int oldMax = p.getInt("fps_max", 0);

        int min = safeFrames > 0
            ? Math.min(oldMin, safeFrames)
            : oldMin;

        int max = Math.max(oldMax, safeFrames);

        p.edit()
            .putLong("samples", samples)
            .putLong("total_frames", totalFrames)
            .putLong("total_draw_calls", totalDrawCalls)
            .putLong(
                "fps_sum",
                p.getLong("fps_sum", 0) + safeFrames)
            .putInt("fps_min", min)
            .putInt("fps_max", max)
            .putInt("last_fps", safeFrames)
            .putInt("last_draw_calls", safeDrawCalls)
            .putInt("last_dcpf", dcpf)
            .apply();
    }

    public static synchronized void finishSession(
        Context context)
    {
        android.content.SharedPreferences p = prefs(context);

        if(p.getBoolean("running", false))
        {
            p.edit()
                .putBoolean("running", false)
                .putLong(
                    "end_time",
                    System.currentTimeMillis())
                .apply();
        }
    }

    public static String getGameName(
        Context context,
        String path)
    {
        if(path == null || path.trim().isEmpty())
        {
            return "Tidak diketahui";
        }

        try
        {
            Uri uri = Uri.parse(path);

            if("content".equalsIgnoreCase(uri.getScheme()))
            {
                Cursor cursor = context.getContentResolver().query(
                    uri,
                    new String[] {
                        OpenableColumns.DISPLAY_NAME
                    },
                    null,
                    null,
                    null);

                if(cursor != null)
                {
                    try
                    {
                        if(cursor.moveToFirst())
                        {
                            int index = cursor.getColumnIndex(
                                OpenableColumns.DISPLAY_NAME);

                            if(index >= 0)
                            {
                                String name = cursor.getString(index);

                                if(name != null && !name.isEmpty())
                                {
                                    return name;
                                }
                            }
                        }
                    }
                    finally
                    {
                        cursor.close();
                    }
                }
            }

            String name = uri.getLastPathSegment();

            if(name != null && !name.isEmpty())
            {
                return Uri.decode(name);
            }
        }
        catch(Exception ignored)
        {
        }

        try
        {
            String name = new File(path).getName();

            if(!name.isEmpty())
            {
                return name;
            }
        }
        catch(Exception ignored)
        {
        }

        return path;
    }

    private static String[] getGraphics()
    {
        try
        {
            String[] values =
                NativeInterop.getGraphicsDiagnostics();

            if(values != null && values.length >= 4)
            {
                return values;
            }
        }
        catch(Exception ignored)
        {
        }
        catch(LinkageError ignored)
        {
        }

        return new String[] {"", "", "", ""};
    }

    private static long[] getAudio()
    {
        try
        {
            long[] values =
                NativeInterop.getAudioDiagnostics();

            if(values != null && values.length >= 6)
            {
                return values;
            }
        }
        catch(Exception ignored)
        {
        }
        catch(LinkageError ignored)
        {
        }

        return new long[] {-1, -1, -1, -1, -1, -1};
    }

    public static String buildReport(Context context)
    {
        android.content.SharedPreferences p = prefs(context);

        String gamePath = p.getString("game_path", "");

        String gameName = getGameName(
            context,
            gamePath);

        boolean running = p.getBoolean("running", false);

        long start = p.getLong("start_time", 0);
        long end = p.getLong("end_time", 0);

        if(running)
        {
            end = System.currentTimeMillis();
        }

        long duration = start > 0 && end >= start
            ? end - start
            : 0;

        long samples = p.getLong("samples", 0);
        long totalFrames = p.getLong("total_frames", 0);
        long totalDrawCalls =
            p.getLong("total_draw_calls", 0);

        long fpsSum = p.getLong("fps_sum", 0);

        int averageFps = samples > 0
            ? (int)(fpsSum / samples)
            : 0;

        int minimumFps = p.getInt("fps_min", 0);

        if(minimumFps == Integer.MAX_VALUE)
        {
            minimumFps = 0;
        }

        int maximumFps = p.getInt("fps_max", 0);

        String[] graphics = getGraphics();
        long[] audio = getAudio();

        boolean glAvailable =
            !graphics[0].isEmpty() ||
            !graphics[1].isEmpty() ||
            !graphics[2].isEmpty();

        boolean openSlActive =
            audio[0] == 1;

        StringBuilder report = new StringBuilder();

        report.append("PLAY! ANDROID DIAGNOSTIC REPORT\n");
        report.append("====================================\n\n");

        report.append("SESSION\n");
        report.append("------------------------------------\n");

        report.append("Game file: ")
            .append(gameName).append('\n');

        report.append("Game path: ")
            .append(gamePath).append('\n');

        report.append("Status: ");

        if(start <= 0)
        {
            report.append("NOT STARTED\n");
        }
        else
        {
            report.append(running ? "RUNNING" : "FINISHED")
                .append('\n');
        }

        report.append("Start: ")
            .append(formatDate(start)).append('\n');

        report.append("End: ")
            .append(formatDate(end)).append('\n');

        report.append("Duration: ")
            .append(duration / 1000)
            .append(" seconds\n\n");

        report.append("GRAPHICS / PERFORMANCE\n");
        report.append("------------------------------------\n");

        report.append("FPS current: ")
            .append(p.getInt("last_fps", 0)).append('\n');

        report.append("FPS average: ")
            .append(averageFps).append('\n');

        report.append("FPS minimum: ")
            .append(minimumFps).append('\n');

        report.append("FPS maximum: ")
            .append(maximumFps).append('\n');

        report.append("Total frames: ")
            .append(totalFrames).append('\n');

        report.append("Draw calls last sample: ")
            .append(p.getInt("last_draw_calls", 0))
            .append('\n');

        report.append("Draw calls per frame last sample: ")
            .append(p.getInt("last_dcpf", 0))
            .append('\n');

        report.append("Total draw calls: ")
            .append(totalDrawCalls).append('\n');

        report.append("Sampling intervals: ")
            .append(samples).append('\n');

        report.append("GPU vendor: ")
            .append(glAvailable ? graphics[0] : "UNAVAILABLE")
            .append('\n');

        report.append("GPU renderer: ")
            .append(glAvailable ? graphics[1] : "UNAVAILABLE")
            .append('\n');

        report.append("OpenGL ES version: ")
            .append(glAvailable ? graphics[2] : "UNAVAILABLE")
            .append('\n');

        report.append("GLSL version: ")
            .append(glAvailable ? graphics[3] : "UNAVAILABLE")
            .append('\n');

        report.append("GPU load: UNAVAILABLE\n");
        report.append("VRAM usage: UNAVAILABLE\n\n");

        report.append("NATIVE CPU PROFILING\n");
        report.append("------------------------------------\n");

        try
        {
           if(StatsManager.isProfiling())
           {
               String profilingInfo = StatsManager.getProfilingInfo();

               report.append("Profiler status: ACTIVE\n");

               if(profilingInfo != null && !profilingInfo.trim().isEmpty())
               {
        report.append(profilingInfo);
               }
               else
               {
                   report.append("Profiler data: NOT AVAILABLE YET\n");
               }
           }
           else
           {
               report.append("Profiler status: DISABLED\n");
           }
        }
        catch(Exception e)
        {
            report.append("Profiler status: ERROR\n");
            report.append("Profiler data: UNAVAILABLE\n");
        }
        catch(LinkageError e)
        {
            report.append("Profiler status: NATIVE LINK ERROR\n");
            report.append("Profiler data: UNAVAILABLE\n");
        }

        report.append("\n");

        if(openSlActive)
        {
            report.append("Output format: PCM 16-bit stereo\n");
            report.append("Configured sample rate: 44100 Hz\n");

            report.append("Queued buffers: ")
                .append(audio[1]).append('\n');

            report.append("Buffer capacity: ")
                .append(audio[2]).append('\n');

            report.append("Queue-empty events: ")
                .append(audio[3]).append('\n');

            report.append("Backpressure wait count: ")
                .append(audio[4]).append('\n');

            report.append("Backpressure wait total: ")
                .append(String.format(
                    Locale.US,
                    "%.3f ms",
                    audio[5] / 1000000.0))
                .append('\n');
        }
        else
        {
            report.append("Output format: UNAVAILABLE\n");
            report.append("Configured sample rate: UNAVAILABLE\n");
            report.append("Queued buffers: UNAVAILABLE\n");
            report.append("Buffer capacity: UNAVAILABLE\n");
            report.append("Queue-empty events: UNAVAILABLE\n");
            report.append("Backpressure wait count: UNAVAILABLE\n");
            report.append("Backpressure wait total: UNAVAILABLE\n");
        }

        report.append("\n");

        report.append("DEVICE\n");
        report.append("------------------------------------\n");

        report.append("Manufacturer: ")
            .append(Build.MANUFACTURER).append('\n');

        report.append("Model: ")
            .append(Build.MODEL).append('\n');

        report.append("Device: ")
            .append(Build.DEVICE).append('\n');

        report.append("Android: ")
            .append(Build.VERSION.RELEASE).append('\n');

        report.append("SDK: ")
            .append(Build.VERSION.SDK_INT).append('\n');

        report.append("Supported ABI: ");

        if(Build.SUPPORTED_ABIS != null)
        {
            for(int i = 0; i < Build.SUPPORTED_ABIS.length; i++)
            {
                if(i > 0)
                {
                    report.append(", ");
                }

                report.append(Build.SUPPORTED_ABIS[i]);
            }
        }

        report.append('\n');

        report.append("CPU cores available: ")
            .append(Runtime.getRuntime().availableProcessors())
            .append('\n');

        report.append("Device GLES capability: ")
            .append(getGlesCapability(context))
            .append("\n\n");

        report.append("NOTES\n");
        report.append("------------------------------------\n");

        report.append(
            "GPU strings are queried from the active OpenGL context.\n");

        report.append(
            "OpenSL counters are read from the active native sound handler.\n");

        report.append(
            "Queue-empty events do not prove audible audio dropouts.\n");

        report.append(
            "Backpressure measures waits for an available audio buffer.\n");

        report.append(
            "GPU load and VRAM are not inferred from FPS or draw calls.\n");

        return report.toString();
    }

    public static String buildJsonReport(
        Context context,
        String reportText) throws JSONException
    {
        JSONObject json = new JSONObject();

        json.put("application", "Play! Android");
        json.put("report_format", 2);
        json.put(
            "generated_at",
            new SimpleDateFormat(
                "yyyy-MM-dd'T'HH:mm:ssZ",
                Locale.US).format(new Date()));

        json.put("report_text", reportText);

        return json.toString(2);
    }

    private static boolean getAudioEnabled()
    {
        try
        {
            return SettingsManager.getPreferenceBoolean(
                "audio.enableoutput");
        }
        catch(Exception e)
        {
            return false;
        }
    }

    private static String getGlesCapability(Context context)
    {
        try
        {
            ActivityManager manager =
                (ActivityManager)context.getSystemService(
                    Context.ACTIVITY_SERVICE);

            if(manager == null)
            {
                return "UNAVAILABLE";
            }

            ConfigurationInfo info =
                manager.getDeviceConfigurationInfo();

            if(info == null)
            {
                return "UNAVAILABLE";
            }

            return info.getGlEsVersion();
        }
        catch(Exception e)
        {
            return "UNAVAILABLE";
        }
    }

    private static String formatDate(long timestamp)
    {
        if(timestamp <= 0)
        {
            return "Not recorded";
        }

        return new SimpleDateFormat(
            "yyyy-MM-dd HH:mm:ss",
            Locale.US).format(new Date(timestamp));
    }
}
