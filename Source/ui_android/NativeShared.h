#pragma once

#include <jni.h>
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
