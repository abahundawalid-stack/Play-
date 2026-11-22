#include <jni.h>

#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <vector>

#include "../ui_shared/BootablesProcesses.h"
#include "../ui_shared/BootablesDbClient.h"
#include "ui_shared/BootableUtils.h"
#include "com_virtualapplications_play_Bootable.h"
#include "NativeShared.h"

namespace
{
	constexpr uint32_t UNICODE_REPLACEMENT_CHARACTER = 0xFFFD;

	void AppendUtf8(std::string& output, uint32_t codePoint)
	{
		if(codePoint <= 0x7F)
		{
			output.push_back(static_cast<char>(codePoint));
		}
		else if(codePoint <= 0x7FF)
		{
			output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
			output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
		else if(codePoint <= 0xFFFF)
		{
			output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
			output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
			output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
		else
		{
			output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
			output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
			output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
			output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
	}

	// Convert Java UTF-16 into standard UTF-8.
	// This is intentionally separate from GetStringFromJstring(),
	// which uses JNI Modified UTF-8.
	bool JavaStringToUtf8(
	    JNIEnv* env,
	    jstring javaString,
	    std::string* output)
	{
		if(env == nullptr || javaString == nullptr || output == nullptr)
		{
			return false;
		}

		const jsize length = env->GetStringLength(javaString);

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

				if(codePoint >= 0xD800 && codePoint <= 0xDBFF)
				{
					if((i + 1) < length)
					{
						const uint32_t low = characters[i + 1];

						if(low >= 0xDC00 && low <= 0xDFFF)
						{
							codePoint =
							    0x10000 +
							    ((codePoint - 0xD800) << 10) +
							    (low - 0xDC00);

							i++;
						}
						else
						{
							codePoint = UNICODE_REPLACEMENT_CHARACTER;
						}
					}
					else
					{
						codePoint = UNICODE_REPLACEMENT_CHARACTER;
					}
				}
				else if(codePoint >= 0xDC00 && codePoint <= 0xDFFF)
				{
					codePoint = UNICODE_REPLACEMENT_CHARACTER;
				}

				AppendUtf8(*output, codePoint);
			}
		}
		catch(...)
		{
			env->ReleaseStringChars(javaString, characters);
			throw;
		}

		env->ReleaseStringChars(javaString, characters);

		return !env->ExceptionCheck();
	}

	// Convert standard UTF-8 into UTF-16 for JNI NewString().
	// Invalid UTF-8 sequences are replaced with U+FFFD.
	bool Utf8ToUtf16(
	    const std::string& input,
	    std::vector<jchar>& output)
	{
		output.clear();

		const size_t maxLength =
		    static_cast<size_t>(std::numeric_limits<jsize>::max());

		size_t i = 0;

		while(i < input.size())
		{
			const auto first =
			    static_cast<uint8_t>(input[i]);

			uint32_t codePoint = UNICODE_REPLACEMENT_CHARACTER;
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
				if(i + sequenceLength > input.size())
				{
					valid = false;
				}
				else
				{
					codePoint = first &
					    ((1u << (7 - sequenceLength)) - 1u);

					for(size_t j = 1; j < sequenceLength; j++)
					{
						const auto next =
						    static_cast<uint8_t>(input[i + j]);

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
						   (codePoint >= 0xD800 && codePoint <= 0xDFFF))
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

				output.push_back(static_cast<jchar>(codePoint));
			}
			else
			{
				if(output.size() > maxLength - 2)
				{
					return false;
				}

				codePoint -= 0x10000;

				output.push_back(
				    static_cast<jchar>(0xD800 | (codePoint >> 10)));

				output.push_back(
				    static_cast<jchar>(0xDC00 | (codePoint & 0x3FF)));
			}

			i += sequenceLength;
		}

		return true;
	}

	jstring NewJstringFromUtf8(
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
			Log_Error("BootablesInterop: UTF-8 string exceeds JNI limits.\r\n");
			return nullptr;
		}

		const jchar* data =
		    utf16.empty() ? nullptr : utf16.data();

		return env->NewString(
		    data,
		    static_cast<jsize>(utf16.size()));
	}

	bool GetBootablePath(
	    JNIEnv* env,
	    jstring javaPath,
	    fs::path* output)
	{
		if(output == nullptr)
		{
			return false;
		}

		std::string path;

		if(!JavaStringToUtf8(env, javaPath, &path))
		{
			return false;
		}

		// POSIX paths cannot contain embedded NUL bytes.
		if(path.empty() ||
		   path.find('\0') != std::string::npos)
		{
			return false;
		}

		*output = fs::path(path);
		return true;
	}

	bool IsBootableClassInfoValid(
	    const com::virtualapplications::play::Bootable_ClassInfo& info)
	{
		return info.clazz != nullptr &&
		       info.init != nullptr &&
		       info.path != nullptr &&
		       info.discId != nullptr &&
		       info.title != nullptr &&
		       info.coverUrl != nullptr &&
		       info.overview != nullptr;
	           info.lastBootedTime != nullptr &&
               info.bootableType != nullptr &&
               info.favorite != nullptr &&
               info.compatibility != nullptr;
	}

	void LogJniFailure(const char* operation)
	{
		Log_Error(
		    "BootablesInterop: JNI operation failed: %s\r\n",
		    operation);
	}
}

static void fullScan(
    JNIEnv* env,
    jobjectArray rootDirectories)
{
	if(env == nullptr || rootDirectories == nullptr)
	{
		Log_Error("BootablesInterop: fullScan received null arguments.\r\n");
		return;
	}

	const jsize rootDirectoryCount =
	    env->GetArrayLength(rootDirectories);

	if(env->ExceptionCheck())
	{
		LogJniFailure("GetArrayLength");
		return;
	}

	for(jsize i = 0; i < rootDirectoryCount; i++)
	{
		jstring rootDirectoryString =
		    static_cast<jstring>(
		        env->GetObjectArrayElement(rootDirectories, i));

		if(env->ExceptionCheck())
		{
			LogJniFailure("GetObjectArrayElement");
			return;
		}

		if(rootDirectoryString == nullptr)
		{
			continue;
		}

		std::string rootDirectory;

		const bool converted =
		    JavaStringToUtf8(
		        env,
		        rootDirectoryString,
		        &rootDirectory);

		env->DeleteLocalRef(rootDirectoryString);

		if(env->ExceptionCheck())
		{
			LogJniFailure("Release root directory string");
			return;
		}

		if(!converted ||
		   rootDirectory.empty() ||
		   rootDirectory.find('\0') != std::string::npos)
		{
			continue;
		}

		ScanBootables(fs::path(rootDirectory), true);
	}
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_BootablesInterop_scanBootables(
    JNIEnv* env,
    jclass clazz,
    jobjectArray rootDirectories)
{
	try
	{
		auto activeDirectories =
		    GetActiveBootableDirectories();

		if(activeDirectories.empty())
		{
			fullScan(env, rootDirectories);
		}
		else
		{
			for(const auto& activeDirectory : activeDirectories)
			{
				ScanBootables(activeDirectory, false);
			}
		}

		if(env != nullptr && !env->ExceptionCheck())
		{
			FetchGameTitles();
		}
	}
	catch(const std::exception& exception)
	{
		Log_Error(
		    "BootablesInterop::scanBootables: %s\r\n",
		    exception.what());
	}
	catch(...)
	{
		Log_Error(
		    "BootablesInterop::scanBootables: unknown native exception.\r\n");
	}
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_BootablesInterop_fullScanBootables(
    JNIEnv* env,
    jclass clazz,
    jobjectArray rootDirectories)
{
	try
	{
		fullScan(env, rootDirectories);

		if(env != nullptr && !env->ExceptionCheck())
		{
			FetchGameTitles();
		}
	}
	catch(const std::exception& exception)
	{
		Log_Error(
		    "BootablesInterop::fullScanBootables: %s\r\n",
		    exception.what());
	}
	catch(...)
	{
		Log_Error(
		    "BootablesInterop::fullScanBootables: unknown native exception.\r\n");
	}
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_virtualapplications_play_BootablesInterop_getBootables(
    JNIEnv* env,
    jclass clazz,
    jint sortedMethod)
{
	if(env == nullptr)
	{
		return nullptr;
	}

	try
	{
		auto bootables =
		    BootablesDb::CClient::GetInstance().GetBootables(sortedMethod);

		const auto& bootableClassInfo =
		    com::virtualapplications::play::Bootable_ClassInfo::GetInstance();

		if(!IsBootableClassInfoValid(bootableClassInfo))
		{
			Log_Error(
			    "BootablesInterop: Bootable JNI class information is incomplete.\r\n");
			return nullptr;
		}

		if(bootables.size() >
		   static_cast<size_t>(std::numeric_limits<jsize>::max()))
		{
			Log_Error(
			    "BootablesInterop: bootable count exceeds JNI array limits.\r\n");
			return nullptr;
		}

		const jsize bootableCount =
		    static_cast<jsize>(bootables.size());

		jobjectArray bootablesJ =
		    env->NewObjectArray(
		        bootableCount,
		        bootableClassInfo.clazz,
		        nullptr);

		if(bootablesJ == nullptr || env->ExceptionCheck())
		{
			return nullptr;
		}

		for(jsize i = 0; i < bootableCount; i++)
		{
			if(env->PushLocalFrame(16) < 0)
			{
				return nullptr;
			}

			const auto& bootable =
			    bootables[static_cast<size_t>(i)];

			jobject bootableJ =
			    env->NewObject(
			        bootableClassInfo.clazz,
			        bootableClassInfo.init);

			if(bootableJ == nullptr || env->ExceptionCheck())
			{
				env->PopLocalFrame(nullptr);
				return nullptr;
			}

			jstring pathString =
			    NewJstringFromUtf8(
			        env,
			        bootable.path.string());

			jstring discIdString =
			    NewJstringFromUtf8(
			        env,
			        bootable.discId);

			jstring titleString =
			    NewJstringFromUtf8(
			        env,
			        bootable.title);

			jstring coverUrlString =
			    NewJstringFromUtf8(
			        env,
			        bootable.coverUrl);

			jstring overviewString =
			    NewJstringFromUtf8(
			        env,
			        bootable.overview);

			if(pathString == nullptr ||
			   discIdString == nullptr ||
			   titleString == nullptr ||
			   coverUrlString == nullptr ||
			   overviewString == nullptr ||
			   env->ExceptionCheck())
			{
				env->PopLocalFrame(nullptr);
				return nullptr;
			}

			env->SetObjectField(
			    bootableJ,
			    bootableClassInfo.path,
			    pathString);

			env->SetObjectField(
			    bootableJ,
			    bootableClassInfo.discId,
			    discIdString);

			env->SetObjectField(
			    bootableJ,
			    bootableClassInfo.title,
			    titleString);

			env->SetObjectField(
			    bootableJ,
			    bootableClassInfo.coverUrl,
			    coverUrlString);

			env->SetObjectField(
			    bootableJ,
			    bootableClassInfo.overview,
			    overviewString);

			env->SetLongField(
                bootableJ,
                bootableClassInfo.lastBootedTime,
                static_cast<jlong>(bootable.lastBootedTime));

            env->SetIntField(
                bootableJ,
                bootableClassInfo.bootableType,
                static_cast<jint>(bootable.bootableType));

            env->SetBooleanField(
                bootableJ,
                bootableClassInfo.favorite,
                bootable.favorite);

std::string compatibilityText;

for(const auto& state : bootable.states)
{
    if(!compatibilityText.empty())
    {
        compatibilityText += "\n";
    }

    compatibilityText += state.name;
}

jstring compatibilityString =
    NewJstringFromUtf8(env, compatibilityText);

if(compatibilityString == nullptr ||
   env->ExceptionCheck())
{
    env->PopLocalFrame(nullptr);
    return nullptr;
}

env->SetObjectField(
    bootableJ,
    bootableClassInfo.compatibility,
    compatibilityString);

			if(env->ExceptionCheck())
			{
				env->PopLocalFrame(nullptr);
				return nullptr;
			}

			env->SetObjectArrayElement(
			    bootablesJ,
			    i,
			    bootableJ);

			if(env->ExceptionCheck())
			{
				env->PopLocalFrame(nullptr);
				return nullptr;
			}

			env->PopLocalFrame(nullptr);
		}

		return bootablesJ;
	}
	catch(const std::exception& exception)
	{
		Log_Error(
		    "BootablesInterop::getBootables: %s\r\n",
		    exception.what());
		return nullptr;
	}
	catch(...)
	{
		Log_Error(
		    "BootablesInterop::getBootables: unknown native exception.\r\n");
		return nullptr;
	}
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_virtualapplications_play_BootablesInterop_tryRegisterBootable(
    JNIEnv* env,
    jclass clazz,
    jstring bootablePathString)
{
	try
	{
		fs::path bootablePath;

		if(!GetBootablePath(env, bootablePathString, &bootablePath))
		{
			return JNI_FALSE;
		}

		return TryRegisterBootable(bootablePath)
		           ? JNI_TRUE
		           : JNI_FALSE;
	}
	catch(const std::exception& exception)
	{
		Log_Error(
		    "BootablesInterop::tryRegisterBootable: %s\r\n",
		    exception.what());
		return JNI_FALSE;
	}
	catch(...)
	{
		Log_Error(
		    "BootablesInterop::tryRegisterBootable: unknown native exception.\r\n");
		return JNI_FALSE;
	}
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_BootablesInterop_setLastBootedTime(
    JNIEnv* env,
    jclass clazz,
    jstring bootablePathString,
    jlong lastBootedTime)
{
	try
	{
		fs::path bootablePath;

		if(!GetBootablePath(env, bootablePathString, &bootablePath))
		{
			return;
		}

		BootablesDb::CClient::GetInstance().SetLastBootedTime(
		    bootablePath,
		    static_cast<time_t>(lastBootedTime));
	}
	catch(const std::exception& exception)
	{
		Log_Error(
		    "BootablesInterop::setLastBootedTime: %s\r\n",
		    exception.what());
	}
	catch(...)
	{
		Log_Error(
		    "BootablesInterop::setLastBootedTime: unknown native exception.\r\n");
	}
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_virtualapplications_play_BootablesInterop_IsBootableExecutablePath(
    JNIEnv* env,
    jclass clazz,
    jstring bootablePathString)
{
	try
	{
		fs::path bootablePath;

		if(!GetBootablePath(env, bootablePathString, &bootablePath))
		{
			return JNI_FALSE;
		}

		return BootableUtils::IsBootableExecutablePath(bootablePath)
		           ? JNI_TRUE
		           : JNI_FALSE;
	}
	catch(const std::exception& exception)
	{
		Log_Error(
		    "BootablesInterop::IsBootableExecutablePath: %s\r\n",
		    exception.what());
		return JNI_FALSE;
	}
	catch(...)
	{
		Log_Error(
		    "BootablesInterop::IsBootableExecutablePath: unknown native exception.\r\n");
		return JNI_FALSE;
	}
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_virtualapplications_play_BootablesInterop_DoesBootableExist(
    JNIEnv* env,
    jclass clazz,
    jstring bootablePathString)
{
	try
	{
		fs::path bootablePath;

		if(!GetBootablePath(env, bootablePathString, &bootablePath))
		{
			return JNI_FALSE;
		}

		return DoesBootableExist(bootablePath)
		           ? JNI_TRUE
		           : JNI_FALSE;
	}
	catch(const std::exception& exception)
	{
		Log_Error(
		    "BootablesInterop::DoesBootableExist: %s\r\n",
		    exception.what());
		return JNI_FALSE;
	}
	catch(...)
	{
		Log_Error(
		    "BootablesInterop::DoesBootableExist: unknown native exception.\r\n");
		return JNI_FALSE;
	}
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_BootablesInterop_UnregisterBootable(
    JNIEnv* env,
    jclass clazz,
    jstring bootablePathString)
{
	try
	{
		fs::path bootablePath;

		if(!GetBootablePath(env, bootablePathString, &bootablePath))
		{
			return;
		}

		BootablesDb::CClient::GetInstance().UnregisterBootable(
		    bootablePath);
	}
	catch(const std::exception& exception)
	{
		Log_Error(
		    "BootablesInterop::UnregisterBootable: %s\r\n",
		    exception.what());
	}
	catch(...)
	{
		Log_Error(
		    "BootablesInterop::UnregisterBootable: unknown native exception.\r\n");
	}
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_BootablesInterop_fetchGameTitles(
    JNIEnv* env,
    jclass clazz)
{
	try
	{
		FetchGameTitles();
	}
	catch(const std::exception& exception)
	{
		Log_Error(
		    "BootablesInterop::fetchGameTitles: %s\r\n",
		    exception.what());
	}
	catch(...)
	{
		Log_Error(
		    "BootablesInterop::fetchGameTitles: unknown native exception.\r\n");
	}
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_BootablesInterop_PurgeInexistingFiles(
    JNIEnv* env,
    jclass clazz)
{
	try
	{
		PurgeInexistingFiles();
	}
	catch(const std::exception& exception)
	{
		Log_Error(
		    "BootablesInterop::PurgeInexistingFiles: %s\r\n",
		    exception.what());
	}
	catch(...)
	{
		Log_Error(
		    "BootablesInterop::PurgeInexistingFiles: unknown native exception.\r\n");
	}
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_BootablesInterop_setTitle(
    JNIEnv* env,
    jclass clazz,
    jstring pathString,
    jstring titleString)
{
    try
    {
        fs::path path;
        std::string title;

        if(!GetBootablePath(env, pathString, &path))
            return;

        if(!JavaStringToUtf8(env, titleString, &title))
            return;

        BootablesDb::CClient::GetInstance().SetTitle(
            path,
            title.c_str());
    }
    catch(const std::exception& exception)
    {
        Log_Error(
            "BootablesInterop::setTitle: %s\r\n",
            exception.what());
    }
    catch(...)
    {
        Log_Error(
            "BootablesInterop::setTitle: unknown native exception.\r\n");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_BootablesInterop_setDiscId(
    JNIEnv* env,
    jclass clazz,
    jstring pathString,
    jstring discIdString)
{
    try
    {
        fs::path path;
        std::string discId;

        if(!GetBootablePath(env, pathString, &path))
            return;

        if(!JavaStringToUtf8(env, discIdString, &discId))
            return;

        BootablesDb::CClient::GetInstance().SetDiscId(
            path,
            discId.c_str());
    }
    catch(const std::exception& exception)
    {
        Log_Error(
            "BootablesInterop::setDiscId: %s\r\n",
            exception.what());
    }
    catch(...)
    {
        Log_Error(
            "BootablesInterop::setDiscId: unknown native exception.\r\n");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_BootablesInterop_setFavorite(
    JNIEnv* env,
    jclass clazz,
    jstring pathString,
    jboolean favorite)
{
    try
    {
        fs::path path;

        if(!GetBootablePath(env, pathString, &path))
            return;

        BootablesDb::CClient::GetInstance().SetFavorite(
            path,
            favorite == JNI_TRUE);
    }
    catch(const std::exception& exception)
    {
        Log_Error(
            "BootablesInterop::setFavorite: %s\r\n",
            exception.what());
    }
    catch(...)
    {
        Log_Error(
            "BootablesInterop::setFavorite: unknown native exception.\r\n");
    }
}
