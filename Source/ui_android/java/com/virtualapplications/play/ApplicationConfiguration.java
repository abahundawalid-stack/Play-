package com.virtualapplications.play;

import android.content.Context;
import android.content.SharedPreferences;
import androidx.preference.PreferenceManager;

/** Access to app-level preferences without owning emulator preferences. */
public final class ApplicationConfiguration
{
    private final SharedPreferences preferences;

    ApplicationConfiguration(Context context)
    {
        if(context == null)
            throw new IllegalArgumentException("Context must not be null.");

        preferences = PreferenceManager.getDefaultSharedPreferences(
            context.getApplicationContext());
    }

    public boolean getBoolean(String key, boolean defaultValue)
    {
        return preferences.getBoolean(key, defaultValue);
    }

    public int getInt(String key, int defaultValue)
    {
        return preferences.getInt(key, defaultValue);
    }

    public String getString(String key, String defaultValue)
    {
        return preferences.getString(key, defaultValue);
    }

    public void putBoolean(String key, boolean value)
    {
        requireKey(key);
        preferences.edit().putBoolean(key, value).apply();
    }

    public void putInt(String key, int value)
    {
        requireKey(key);
        preferences.edit().putInt(key, value).apply();
    }

    public void putString(String key, String value)
    {
        requireKey(key);
        if(value == null)
            preferences.edit().remove(key).apply();
        else
            preferences.edit().putString(key, value).apply();
    }

    private static void requireKey(String key)
    {
        if(key == null || key.trim().isEmpty())
            throw new IllegalArgumentException(
                "Preference key must not be empty.");
    }
}
