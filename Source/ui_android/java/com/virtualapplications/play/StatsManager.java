package com.virtualapplications.play;

public class StatsManager
{
	static
	{
		System.loadLibrary("Play");
	}

	public static native int getFrames();

	public static native int getDrawCalls();

	public static native void clearStats();

	public static native boolean isProfiling();

	public static native String getProfilingInfo();

	public static native String getGsPipelineDiagnostics();

	public static native String getRuntimeExecutionDiagnostics();
}
