package com.virtualapplications.play;

import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.ConfigurationInfo;
import android.os.Build;

/** Reports platform capabilities; it does not select or create a renderer. */
public final class ApplicationCompatibility
{
    private final int sdkInt;
    private final String release;
    private final String supportedGlesVersion;

    ApplicationCompatibility(Context context)
    {
        sdkInt = Build.VERSION.SDK_INT;
        release = Build.VERSION.RELEASE == null
            ? "unknown" : Build.VERSION.RELEASE;
        supportedGlesVersion = queryGlesVersion(context);
    }

    public int getSdkInt()
    {
        return sdkInt;
    }

    public String getAndroidRelease()
    {
        return release;
    }

    public String getSupportedGlesVersion()
    {
        return supportedGlesVersion;
    }

    public boolean isAtLeastSdk(int requiredSdk)
    {
        return sdkInt >= requiredSdk;
    }

    private static String queryGlesVersion(Context context)
    {
        try
        {
            ActivityManager manager =
                (ActivityManager)context.getSystemService(
                    Context.ACTIVITY_SERVICE);

            if(manager == null)
                return "UNAVAILABLE";

            ConfigurationInfo info = manager.getDeviceConfigurationInfo();
            if(info == null || info.getGlEsVersion() == null)
                return "UNAVAILABLE";

            return info.getGlEsVersion();
        }
        catch(RuntimeException error)
        {
            ApplicationLogger.warning(
                Constants.TAG,
                "Unable to query device GLES capability: " + error);
            return "UNAVAILABLE";
        }
    }
}
