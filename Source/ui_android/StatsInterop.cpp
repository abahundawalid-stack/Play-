#include <jni.h>
#include "ui_shared/StatsManager.h"

extern "C" JNIEXPORT jint JNICALL Java_com_virtualapplications_play_StatsManager_getFrames(JNIEnv* env, jclass clazz)
{
	return CStatsManager::GetInstance().GetFrames();
}

extern "C" JNIEXPORT jint JNICALL Java_com_virtualapplications_play_StatsManager_getDrawCalls(JNIEnv* env, jclass clazz)
{
	return CStatsManager::GetInstance().GetDrawCalls();
}

extern "C" JNIEXPORT void JNICALL Java_com_virtualapplications_play_StatsManager_clearStats(JNIEnv* env, jclass clazz)
{
	CStatsManager::GetInstance().ClearStats();
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_virtualapplications_play_StatsManager_isProfiling(JNIEnv* env, jclass clazz)
{
#ifdef PROFILE
	return JNI_TRUE;
#else
	return JNI_FALSE;
#endif
}

extern "C" JNIEXPORT jstring JNICALL Java_com_virtualapplications_play_StatsManager_getProfilingInfo(JNIEnv* env, jclass clazz)
{
	std::string info;
#ifdef PROFILE
	info = CStatsManager::GetInstance().GetProfilingInfo();
#endif
	jstring result = env->NewStringUTF(info.c_str());
	return result;
}
