#include "NativeShared.h"

#include <android/log.h>
#include <mutex>
#include <cstdarg>

#define LOG_NAME "Play!"

namespace
{
    std::mutex g_graphicsDiagnosticsMutex;
    GraphicsDiagnostics g_graphicsDiagnostics;
}

void Log_Print(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, LOG_NAME, fmt, ap);
    va_end(ap);
}

void Log_Error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_ERROR, LOG_NAME, fmt, ap);
    va_end(ap);
}

std::string GetStringFromJstring(
    JNIEnv* env,
    jstring javaString)
{
    if(env == nullptr || javaString == nullptr)
    {
        return {};
    }

    const char* nativeString =
        env->GetStringUTFChars(javaString, nullptr);

    if(nativeString == nullptr)
    {
        return {};
    }

    std::string result(nativeString);

    env->ReleaseStringUTFChars(
        javaString,
        nativeString);

    return result;
}

void SetGraphicsDiagnostics(
    const std::string& vendor,
    const std::string& renderer,
    const std::string& version,
    const std::string& shadingLanguageVersion)
{
    std::lock_guard<std::mutex> lock(
        g_graphicsDiagnosticsMutex);

    g_graphicsDiagnostics.vendor = vendor;
    g_graphicsDiagnostics.renderer = renderer;
    g_graphicsDiagnostics.version = version;

    g_graphicsDiagnostics.shadingLanguageVersion =
        shadingLanguageVersion;

    g_graphicsDiagnostics.available =
        !vendor.empty() ||
        !renderer.empty() ||
        !version.empty();
}

void ClearGraphicsDiagnostics()
{
    std::lock_guard<std::mutex> lock(
        g_graphicsDiagnosticsMutex);

    g_graphicsDiagnostics = GraphicsDiagnostics{};
}

GraphicsDiagnostics GetGraphicsDiagnostics()
{
    std::lock_guard<std::mutex> lock(
        g_graphicsDiagnosticsMutex);

    return g_graphicsDiagnostics;
}
