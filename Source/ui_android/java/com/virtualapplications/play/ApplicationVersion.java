package com.virtualapplications.play;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Build;

/** Package metadata resolved from the installed application. */
public final class ApplicationVersion
{
    private final String versionName;
    private final long versionCode;
    private final String packageName;

    ApplicationVersion(Context context)
    {
        packageName = context.getPackageName();

        try
        {
            PackageInfo info = context.getPackageManager().getPackageInfo(
                packageName, 0);

            versionName = info.versionName == null
                ? "unknown" : info.versionName;

            if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.P)
                versionCode = info.getLongVersionCode();
            else
                versionCode = info.versionCode;
        }
        catch(PackageManager.NameNotFoundException error)
        {
            throw new IllegalStateException(
                "Unable to resolve application package metadata.", error);
        }
    }

    public String getPackageName()
    {
        return packageName;
    }

    public String getVersionName()
    {
        return versionName;
    }

    public long getVersionCode()
    {
        return versionCode;
    }
}
