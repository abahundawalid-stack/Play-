#include "NativeShared.h"

#include <android/log.h>
#include <mutex>
#include <cstdarg>
#include <cstdint>
#include <limits>
#include <vector>

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
    std::string result;

    if(!GetStringFromJstringUtf8(
        env,
        javaString,
        &result))
    {
        return {};
    }

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

namespace
{
    std::mutex g_virtualMachineMutex;

    constexpr uint32_t UNICODE_REPLACEMENT_CHARACTER = 0xFFFD;

    void AppendUtf8(
        std::string& output,
        uint32_t codePoint)
    {
        if(codePoint <= 0x7F)
        {
            output.push_back(static_cast<char>(codePoint));
        }
        else if(codePoint <= 0x7FF)
        {
            output.push_back(
                static_cast<char>(0xC0 | (codePoint >> 6)));

            output.push_back(
                static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else if(codePoint <= 0xFFFF)
        {
            output.push_back(
                static_cast<char>(0xE0 | (codePoint >> 12)));

            output.push_back(
                static_cast<char>(
                    0x80 | ((codePoint >> 6) & 0x3F)));

            output.push_back(
                static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else
        {
            output.push_back(
                static_cast<char>(0xF0 | (codePoint >> 18)));

            output.push_back(
                static_cast<char>(
                    0x80 | ((codePoint >> 12) & 0x3F)));

            output.push_back(
                static_cast<char>(
                    0x80 | ((codePoint >> 6) & 0x3F)));

            output.push_back(
                static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    bool Utf8ToUtf16(
        const std::string& input,
        std::vector<jchar>& output)
    {
        output.clear();

        const size_t maxLength =
            static_cast<size_t>(
                std::numeric_limits<jsize>::max());

        size_t index = 0;

        while(index < input.size())
        {
            const uint8_t first =
                static_cast<uint8_t>(input[index]);

            uint32_t codePoint =
                UNICODE_REPLACEMENT_CHARACTER;

            size_t sequenceLength = 1;
            bool valid = true;

            if(first <= 0x7F)
            {
                codePoint = first;
            }
            else if(first >= 0xC2 && first <= 0xDF)
            {
                sequenceLength = 2;
            }
            else if(first >= 0xE0 && first <= 0xEF)
            {
                sequenceLength = 3;
            }
            else if(first >= 0xF0 && first <= 0xF4)
            {
                sequenceLength = 4;
            }
            else
            {
                valid = false;
            }

            if(valid && sequenceLength > 1)
            {
                if(index + sequenceLength > input.size())
                {
                    valid = false;
                }
                else
                {
                    codePoint =
                        first &
                        ((1u << (7 - sequenceLength)) - 1u);

                    for(size_t i = 1; i < sequenceLength; i++)
                    {
                        const uint8_t next =
                            static_cast<uint8_t>(
                                input[index + i]);

                        if((next & 0xC0) != 0x80)
                        {
                            valid = false;
                            break;
                        }

                        codePoint =
                            (codePoint << 6) |
                            (next & 0x3F);
                    }

                    if(valid)
                    {
                        const uint32_t minimum =
                            sequenceLength == 2 ? 0x80 :
                            sequenceLength == 3 ? 0x800 :
                            0x10000;

                        if(codePoint < minimum ||
                           codePoint > 0x10FFFF ||
                           (codePoint >= 0xD800 &&
                            codePoint <= 0xDFFF))
                        {
                            valid = false;
                        }
                    }
                }
            }

            if(!valid)
            {
                codePoint = UNICODE_REPLACEMENT_CHARACTER;
                sequenceLength = 1;
            }

            if(codePoint <= 0xFFFF)
            {
                if(output.size() >= maxLength)
                {
                    return false;
                }

                output.push_back(
                    static_cast<jchar>(codePoint));
            }
            else
            {
                if(output.size() > maxLength - 2)
                {
                    return false;
                }

                codePoint -= 0x10000;

                output.push_back(
                    static_cast<jchar>(
                        0xD800 | (codePoint >> 10)));

                output.push_back(
                    static_cast<jchar>(
                        0xDC00 | (codePoint & 0x3FF)));
            }

            index += sequenceLength;
        }

        return true;
    }
}

std::mutex& GetVirtualMachineMutex()
{
    return g_virtualMachineMutex;
}

bool GetStringFromJstringUtf8(
    JNIEnv* env,
    jstring javaString,
    std::string* output)
{
    if(env == nullptr ||
       javaString == nullptr ||
       output == nullptr)
    {
        return false;
    }

    const jsize length =
        env->GetStringLength(javaString);

    if(env->ExceptionCheck())
    {
        return false;
    }

    const jchar* characters =
        env->GetStringChars(javaString, nullptr);

    if(characters == nullptr)
    {
        return false;
    }

    try
    {
        output->clear();

        for(jsize i = 0; i < length; i++)
        {
            uint32_t codePoint = characters[i];

            if(codePoint >= 0xD800 &&
               codePoint <= 0xDBFF)
            {
                if(i + 1 < length)
                {
                    const uint32_t low =
                        characters[i + 1];

                    if(low >= 0xDC00 &&
                       low <= 0xDFFF)
                    {
                        codePoint =
                            0x10000 +
                            ((codePoint - 0xD800) << 10) +
                            (low - 0xDC00);

                        i++;
                    }
                    else
                    {
                        codePoint =
                            UNICODE_REPLACEMENT_CHARACTER;
                    }
                }
                else
                {
                    codePoint =
                        UNICODE_REPLACEMENT_CHARACTER;
                }
            }
            else if(codePoint >= 0xDC00 &&
                    codePoint <= 0xDFFF)
            {
                codePoint =
                    UNICODE_REPLACEMENT_CHARACTER;
            }

            AppendUtf8(*output, codePoint);
        }
    }
    catch(...)
    {
        env->ReleaseStringChars(
            javaString,
            characters);

        throw;
    }

    env->ReleaseStringChars(
        javaString,
        characters);

    return !env->ExceptionCheck();
}

jstring NewJstringFromUtf8Shared(
    JNIEnv* env,
    const std::string& value)
{
    if(env == nullptr)
    {
        return nullptr;
    }

    std::vector<jchar> utf16;

    if(!Utf8ToUtf16(value, utf16))
    {
        return nullptr;
    }

    const jchar* data =
        utf16.empty() ? nullptr : utf16.data();

    return env->NewString(
        data,
        static_cast<jsize>(utf16.size()));
}

void ThrowJavaRuntimeException(
    JNIEnv* env,
    const char* message)
{
    if(env == nullptr ||
       env->ExceptionCheck())
    {
        return;
    }

    jclass exceptionClass =
        env->FindClass("java/lang/RuntimeException");

    if(exceptionClass == nullptr)
    {
        return;
    }

    env->ThrowNew(
        exceptionClass,
        message != nullptr
            ? message
            : "Unknown native error.");

    env->DeleteLocalRef(exceptionClass);
}
