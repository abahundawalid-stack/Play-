package com.virtualapplications.play;

import android.os.Bundle;

/**
 * Stores only restorable UI state.
 *
 * Native VM, EGL, Surface and renderer resources must never be
 * restored from this Bundle.
 */
public final class ActivityStateController
{
    private static final String KEY_MAIN_SORT_METHOD =
            "play.activity.main.sort_method";

    private static final String KEY_EMULATOR_RESUME_PROMPT =
            "play.activity.emulator.resume_prompt_shown";

    private ActivityStateController()
    {
    }

    public static void saveMainActivityState(
            Bundle outState,
            int sortMethod)
    {
        if(outState == null)
            return;

        outState.putInt(
                KEY_MAIN_SORT_METHOD,
                sortMethod);
    }

    public static int restoreMainSortMethod(
            Bundle savedInstanceState,
            int defaultValue)
    {
        if(savedInstanceState == null)
            return defaultValue;

        return savedInstanceState.getInt(
                KEY_MAIN_SORT_METHOD,
                defaultValue);
    }

    public static void saveEmulatorActivityState(
            Bundle outState,
            boolean resumePromptShown)
    {
        if(outState == null)
            return;

        outState.putBoolean(
                KEY_EMULATOR_RESUME_PROMPT,
                resumePromptShown);
    }

    public static boolean restoreEmulatorResumePromptShown(
            Bundle savedInstanceState,
            boolean defaultValue)
    {
        if(savedInstanceState == null)
            return defaultValue;

        return savedInstanceState.getBoolean(
                KEY_EMULATOR_RESUME_PROMPT,
                defaultValue);
    }
}
