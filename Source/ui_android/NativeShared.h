#pragma once

#include <jni.h>
#include <mutex>
#include <string>

struct GraphicsDiagnostics
{
    bool available = false;
    std::string vendor;
    std::string renderer;
    std::string version;
    std::string shadingLanguageVersion;
};

void Log_Print(const char* fmt, ...);
void Log_Error(const char* fmt, ...);

std::string GetStringFromJstring(JNIEnv*, jstring);

void SetGraphicsDiagnostics(
    const std::string& vendor,
    const std::string& renderer,
    const std::string& version,
    const std::string& shadingLanguageVersion);

void ClearGraphicsDiagnostics();

GraphicsDiagnostics GetGraphicsDiagnostics();

// Serializes access to the process-wide native VM pointer.
// All JNI entry points that access g_virtualMachine must use this mutex.
std::mutex& GetVirtualMachineMutex();

// Converts Java UTF-16 strings into standard UTF-8.
// Returns false when conversion fails or JNI reports an exception.
bool GetStringFromJstringUtf8(
    JNIEnv* env,
    jstring javaString,
    std::string* output);

// Converts standard UTF-8 into a Java UTF-16 string.
jstring NewJstringFromUtf8(
    JNIEnv* env,
    const std::string& value);

// Raises java.lang.RuntimeException unless an exception is
// already pending on the current JNI thread.
void ThrowJavaRuntimeException(
    JNIEnv* env,
    const char* message);
