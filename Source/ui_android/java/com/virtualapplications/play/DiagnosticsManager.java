package com.virtualapplications.play;

import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.ConfigurationInfo;
import android.net.Uri;
import android.os.Build;
import android.os.SystemClock;

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

    private static android.content.SharedPreferences prefs(Context context)
    {
        return context.getApplicationContext().getSharedPreferences(
            PREFS, Context.MODE_PRIVATE);
    }

    public static synchronized void beginSession(Context context, String gamePath)
    {
        long now = System.currentTimeMillis();

        prefs(context).edit()
            .putBoolean("running", true)
            .putString("game_path", gamePath == null ? "" : gamePath)
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
        Context context, int frames, int drawCalls)
    {
        android.content.SharedPreferences p = prefs(context);

        if(!p.getBoolean("running", false))
        {
            return;
        }

        int safeFrames = Math.max(0, frames);
        int safeDrawCalls = Math.max(0, drawCalls);
        int dcpf = safeFrames > 0 ? safeDrawCalls / safeFrames : 0;

        long samples = p.getLong("samples", 0) + 1;
        long totalFrames = p.getLong("total_frames", 0) + safeFrames;
        long totalDrawCalls =
            p.getLong("total_draw_calls", 0) + safeDrawCalls;

        int oldMin = p.getInt("fps_min", Integer.MAX_VALUE);
        int oldMax = p.getInt("fps_max", 0);

        int min = safeFrames > 0
            ? Math.min(oldMin, safeFrames) : oldMin;

        int max = Math.max(oldMax, safeFrames);

        p.edit()
            .putLong("samples", samples)
            .putLong("total_frames", totalFrames)
            .putLong("total_draw_calls", totalDrawCalls)
            .putLong("fps_sum",
                p.getLong("fps_sum", 0) + safeFrames)
            .putInt("fps_min", min)
            .putInt("fps_max", max)
            .putInt("last_fps", safeFrames)
            .putInt("last_draw_calls", safeDrawCalls)
            .putInt("last_dcpf", dcpf)
            .apply();
    }

    public static synchronized void finishSession(Context context)
    {
        android.content.SharedPreferences p = prefs(context);

        if(p.getBoolean("running", false))
        {
            p.edit()
                .putBoolean("running", false)
                .putLong("end_time", System.currentTimeMillis())
                .apply();
        }
    }

    public static String getGameName(String path)
    {
        if(path == null || path.trim().isEmpty())
        {
            return "Tidak diketahui";
        }

        try
        {
            Uri uri = Uri.parse(path);

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
            if(!name.isEmpty()) return name;
        }
        catch(Exception ignored)
        {
        }

        return path;
    }

    public static String buildReport(Context context)
    {
        android.content.SharedPreferences p = prefs(context);

        String gamePath = p.getString("game_path", "");
        String gameName = getGameName(gamePath);

        long start = p.getLong("start_time", 0);
        long end = p.getLong("end_time", 0);

        if(p.getBoolean("running", false))
        {
            end = System.currentTimeMillis();
        }

        long duration = start > 0 && end >= start
            ? end - start : 0;

        long samples = p.getLong("samples", 0);
        long totalFrames = p.getLong("total_frames", 0);
        long totalDrawCalls = p.getLong("total_draw_calls", 0);
        long fpsSum = p.getLong("fps_sum", 0);

        int averageFps = samples > 0
            ? (int)(fpsSum / samples) : 0;

        int minimumFps = p.getInt("fps_min", 0);
        if(minimumFps == Integer.MAX_VALUE) minimumFps = 0;

        int maximumFps = p.getInt("fps_max", 0);

        StringBuilder report = new StringBuilder();

        report.append("PLAY! ANDROID DIAGNOSTIC REPORT\n");
        report.append("====================================\n\n");

        report.append("SESSION\n");
        report.append("------------------------------------\n");
        report.append("Game file: ").append(gameName).append('\n');
        report.append("Game path: ").append(gamePath).append('\n');
        report.append("Status: ")
            .append(p.getBoolean("running", false)
                ? "RUNNING" : "FINISHED")
            .append('\n');

        report.append("Start: ")
            .append(formatDate(start)).append('\n');
        report.append("End: ")
            .append(formatDate(end)).append('\n');
        report.append("Duration: ")
            .append(duration / 1000).append(" seconds\n\n");

        report.append("GRAPHICS / PERFORMANCE\n");
        report.append("------------------------------------\n");
        report.append("FPS current: ")
            .append(p.getInt("last_fps", 0)).append('\n');
        report.append("FPS average: ").append(averageFps).append('\n');
        report.append("FPS minimum: ").append(minimumFps).append('\n');
        report.append("FPS maximum: ").append(maximumFps).append('\n');
        report.append("Total frames: ").append(totalFrames).append('\n');
        report.append("Draw calls last sample: ")
            .append(p.getInt("last_draw_calls", 0)).append('\n');
        report.append("Draw calls per frame last sample: ")
            .append(p.getInt("last_dcpf", 0)).append('\n');
        report.append("Total draw calls: ")
            .append(totalDrawCalls).append('\n');
        report.append("Sampling intervals: ").append(samples).append('\n');
        report.append("GPU renderer: NOT YET EXPOSED\n");
        report.append("OpenGL/Vulkan runtime details: NOT YET EXPOSED\n");
        report.append("GPU load / VRAM: UNAVAILABLE\n\n");

        report.append("AUDIO\n");
        report.append("------------------------------------\n");
        report.append("Backend: OpenSL ES\n");
        report.append("Output format: PCM 16-bit stereo\n");
        report.append("Configured sample rate: 44100 Hz\n");
        report.append("Queue buffers: 5\n");
        report.append("Underrun counter: NOT YET EXPOSED TO UI\n");
        report.append("Audio output enabled: ")
            .append(getAudioEnabled()).append("\n\n");

        report.append("DEVICE\n");
        report.append("------------------------------------\n");
        report.append("Manufacturer: ").append(Build.MANUFACTURER).append('\n');
        report.append("Model: ").append(Build.MODEL).append('\n');
        report.append("Device: ").append(Build.DEVICE).append('\n');
        report.append("Android: ").append(Build.VERSION.RELEASE).append('\n');
        report.append("SDK: ").append(Build.VERSION.SDK_INT).append('\n');
        report.append("Supported ABI: ");

        if(Build.SUPPORTED_ABIS != null)
        {
            for(int i = 0; i < Build.SUPPORTED_ABIS.length; i++)
            {
                if(i > 0) report.append(", ");
                report.append(Build.SUPPORTED_ABIS[i]);
            }
        }

        report.append('\n');
        report.append("CPU cores available: ")
            .append(Runtime.getRuntime().availableProcessors())
            .append('\n');
        report.append("Device GLES capability: ")
            .append(getGlesCapability(context)).append("\n\n");

        report.append("NOTES\n");
        report.append("------------------------------------\n");
        report.append("This report contains measured values only where available.\n");
        report.append("NOT YET EXPOSED means the native metric is not connected to the Android UI.\n");
        report.append("GPU load and VRAM are not inferred from FPS or draw calls.\n");

        return report.toString();
    }

    private static boolean getAudioEnabled()
    {
        try
        {
            return SettingsManager.getPreferenceBoolean("audio.enableoutput");
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

            ConfigurationInfo info =
                manager.getDeviceConfigurationInfo();

            return info.getGlEsVersion();
        }
        catch(Exception e)
        {
            return "UNAVAILABLE";
        }
    }

    private static String formatDate(long timestamp)
    {
        if(timestamp <= 0) return "Not recorded";

        return new SimpleDateFormat(
            "yyyy-MM-dd HH:mm:ss", Locale.US)
            .format(new Date(timestamp));
    }
}
