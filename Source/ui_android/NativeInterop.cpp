#include <jni.h>
#include <cassert>
#include <mutex>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <memory>

#include "android/AssetManager.h"
#include "android/ContentResolver.h"
#include "android/JavaVM.h"
#include "android/android_database_Cursor.h"
#include "android/android_net_Uri.h"
#include "android/android_os_ParcelFileDescriptor.h"
#include "android/java_security_MessageDigest.h"
#include "android/javax_crypto_Mac.h"
#include "android/javax_crypto_spec_SecretKeySpec.h"

#include "PathUtils.h"
#include "AppConfig.h"
#include "DefaultAppConfig.h"

#include "../DiskUtils.h"
#include "../PH_Generic.h"
#include "../PS2VM.h"
#include "../PS2VM_Preferences.h"
#include "../gs/GSH_Null.h"

#include "NativeShared.h"
#include "Log.h"
#include "NativeWindowUpdateListener.h"
#include "GSH_OpenGLAndroid.h"
#include "GSH_VulkanAndroid.h"
#include "SH_OpenSL.h"
#include "ui_shared/StatsManager.h"
#include "com_virtualapplications_play_Bootable.h"

#include "http/java_io_InputStream.h"
#include "http/java_io_OutputStream.h"
#include "http/java_net_HttpURLConnection.h"
#include "http/java_net_URL.h"

CPS2VM* g_virtualMachine = nullptr;
static std::mutex g_soundHandlerDiagnosticsMutex;
CPS2VM::NewFrameEvent::Connection g_OnNewFrameConnection;
CGSHandler::NewFrameEvent::Connection g_OnGsNewFrameConnection;

int g_currentGsHandlerId = -1;

#define PREF_VIDEO_GS_HANDLER ("video.gshandler")
#define PREF_AUDIO_ENABLEOUTPUT ("audio.enableoutput")

#define PREFERENCE_VALUE_VIDEO_GS_HANDLER_OPENGL 0
#define PREFERENCE_VALUE_VIDEO_GS_HANDLER_VULKAN 1

static void SetupSoundHandler()
{
    std::lock_guard<std::mutex> lock(
g_soundHandlerDiagnosticsMutex);
    
    assert(g_virtualMachine != nullptr);
    if(g_virtualMachine == nullptr) return;

    auto audioEnabled =
        CAppConfig::GetInstance().GetPreferenceBoolean(
            PREF_AUDIO_ENABLEOUTPUT);

    if(audioEnabled)
    {
        g_virtualMachine->CreateSoundHandler(
            &CSH_OpenSL::HandlerFactory);
    }
    else
    {
        g_virtualMachine->DestroySoundHandler();
    }
}

static void ResetVirtualMachine()
{
    assert(g_virtualMachine != nullptr);
    if(g_virtualMachine == nullptr) return;

    g_virtualMachine->Pause();
    g_virtualMachine->Reset();
    g_virtualMachine->ReloadSpuBlockCount();
    g_virtualMachine->ReloadFrameRateLimit();

    SetupSoundHandler();
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(
    JavaVM* vm,
    void* aReserved)
{
    if(vm == nullptr)
    {
        return JNI_ERR;
    }

    JNIEnv* env = nullptr;

    const jint envResult =
        vm->GetEnv(
            reinterpret_cast<void**>(&env),
            JNI_VERSION_1_6);

    if(envResult != JNI_OK || env == nullptr)
    {
        return JNI_ERR;
    }

    try
    {
        Framework::CJavaVM::SetJavaVM(vm);

        java::net::URL_ClassInfo::GetInstance().PrepareClassInfo();
        java::net::HttpURLConnection_ClassInfo::GetInstance().PrepareClassInfo();
        java::io::InputStream_ClassInfo::GetInstance().PrepareClassInfo();
        java::io::OutputStream_ClassInfo::GetInstance().PrepareClassInfo();
        java::security::MessageDigest_ClassInfo::GetInstance().PrepareClassInfo();
        javax::crypto::Mac_ClassInfo::GetInstance().PrepareClassInfo();
        javax::crypto::spec::SecretKeySpec_ClassInfo::GetInstance().PrepareClassInfo();

        android::content::ContentResolver_ClassInfo::GetInstance().PrepareClassInfo();
        android::database::Cursor_ClassInfo::GetInstance().PrepareClassInfo();
        android::net::Uri_ClassInfo::GetInstance().PrepareClassInfo();
        android::os::ParcelFileDescriptor_ClassInfo::GetInstance().PrepareClassInfo();

        com::virtualapplications::play::Bootable_ClassInfo::GetInstance().PrepareClassInfo();

        if(env->ExceptionCheck())
        {
            env->ExceptionDescribe();
            env->ExceptionClear();

            Framework::CJavaVM::SetJavaVM(nullptr);

            return JNI_ERR;
        }

        Log_Print(
            "JNI initialization completed. JNI version 1.6.\r\n");

        return JNI_VERSION_1_6;
    }
    catch(const std::exception& exception)
    {
        Log_Error(
            "JNI_OnLoad failed: %s\r\n",
            exception.what());

        if(env->ExceptionCheck())
        {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }

        Framework::CJavaVM::SetJavaVM(nullptr);

        return JNI_ERR;
    }
    catch(...)
    {
        Log_Error(
            "JNI_OnLoad failed with unknown native exception.\r\n");

        if(env->ExceptionCheck())
        {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }

        Framework::CJavaVM::SetJavaVM(nullptr);

        return JNI_ERR;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_setFilesDirPath(
    JNIEnv* env,
    jobject obj,
    jstring dirPathString)
{
    auto dirPath = env->GetStringUTFChars(dirPathString, 0);
    Framework::PathUtils::SetFilesDirPath(dirPath);
    env->ReleaseStringUTFChars(dirPathString, dirPath);
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_setCacheDirPath(
    JNIEnv* env,
    jobject obj,
    jstring dirPathString)
{
    auto dirPath = env->GetStringUTFChars(dirPathString, 0);
    Framework::PathUtils::SetCacheDirPath(dirPath);
    env->ReleaseStringUTFChars(dirPathString, dirPath);
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_setAssetManager(
    JNIEnv* env,
    jobject obj,
    jobject assetManagerJava)
{
    auto assetManager =
        AAssetManager_fromJava(env, assetManagerJava);

    assert(assetManager != nullptr);

    Framework::Android::CAssetManager::GetInstance()
        .SetAssetManager(assetManager);
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_setContentResolver(
    JNIEnv* env,
    jclass clazz,
    jobject contentResolverJava)
{
    auto contentResolver =
        Framework::CJavaObject::CastTo<
            android::content::ContentResolver>(
                contentResolverJava);

    Framework::Android::CContentResolver::GetInstance()
        .SetContentResolver(std::move(contentResolver));
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_createVirtualMachine(
    JNIEnv* env,
    jobject obj)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());

    if(g_virtualMachine != nullptr)
    {
        return;
    }

    std::unique_ptr<CPS2VM> vm;

    bool initialized = false;

    try
    {
        vm.reset(new CPS2VM());

        vm->Initialize();
        initialized = true;

        vm->CreatePadHandler(
            CPH_Generic::GetFactoryFunction());

#ifdef PROFILE
        g_OnNewFrameConnection =
            vm->OnNewFrame.Connect(
                std::bind(
                    &CStatsManager::OnNewFrame,
                    &CStatsManager::GetInstance(),
                    vm.get()));
#endif

        CAppConfig::GetInstance().RegisterPreferenceInteger(
            PREF_VIDEO_GS_HANDLER,
            PREFERENCE_VALUE_VIDEO_GS_HANDLER_OPENGL);

        CAppConfig::GetInstance().RegisterPreferenceBoolean(
            PREF_AUDIO_ENABLEOUTPUT,
            true);

        CGSH_OpenGL::RegisterPreferences();

        g_currentGsHandlerId = -1;

        g_virtualMachine = vm.release();

        Log_Print(
            "Native virtual machine initialized.\r\n");
    }
    catch(const std::exception& exception)
    {
        g_OnNewFrameConnection.reset();

        if(initialized && vm != nullptr)
        {
            try
            {
                vm->Destroy();
            }
            catch(...)
            {
                Log_Error(
                    "VM cleanup after initialization failure failed.\r\n");
            }
        }

        ThrowJavaRuntimeException(
            env,
            exception.what());
    }
    catch(...)
    {
        g_OnNewFrameConnection.reset();

        if(initialized && vm != nullptr)
        {
            try
            {
                vm->Destroy();
            }
            catch(...)
            {
                Log_Error(
                    "VM cleanup after unknown initialization failure failed.\r\n");
            }
        }

        ThrowJavaRuntimeException(
            env,
            "Unknown native VM initialization failure.");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_destroyVirtualMachine(
    JNIEnv* env,
    jobject obj)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());

    CPS2VM* vm = g_virtualMachine;

    if(vm == nullptr)
    {
        return;
    }

    // Disconnect callbacks before the VM and its signals are destroyed.
    g_OnGsNewFrameConnection.reset();
    g_OnNewFrameConnection.reset();

    try
    {
        // CPS2VM::Destroy() performs:
        // - GS handler destruction
        // - pad handler destruction
        // - sound handler destruction
        // - emulation-thread termination
        // - emulation-thread join
        // - VM subsystem destruction
        vm->Destroy();
    }
    catch(const std::exception& exception)
    {
        ThrowJavaRuntimeException(
            env,
            exception.what());

        return;
    }
    catch(...)
    {
        ThrowJavaRuntimeException(
            env,
            "Unknown native VM shutdown failure.");

        return;
    }

    delete vm;

    g_virtualMachine = nullptr;

    g_currentGsHandlerId = -1;

    ClearGraphicsDiagnostics();

    Log_Print(
        "Native virtual machine destroyed.\r\n");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_virtualapplications_play_NativeInterop_isVirtualMachineCreated(
    JNIEnv* env,
    jobject obj)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    return g_virtualMachine != nullptr
        ? JNI_TRUE
        : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_virtualapplications_play_NativeInterop_hasState(
    JNIEnv* env,
    jobject obj,
    jint slot)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    if(g_virtualMachine == nullptr)
    {
        return JNI_FALSE;
    }

    try
    {
        auto statePath =
            g_virtualMachine->GenerateStatePath(slot);

        auto temporaryPath = statePath;
        temporaryPath += ".tmp";

        // Jangan tawarkan file state yang belum selesai ditulis.
        if(fs::exists(temporaryPath))
        {
            return JNI_FALSE;
        }

        return fs::exists(statePath)
            ? JNI_TRUE
            : JNI_FALSE;
    }
    catch(...)
    {
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_resumeVirtualMachine(
    JNIEnv* env,
    jobject obj)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    assert(g_virtualMachine != nullptr);
    if(g_virtualMachine == nullptr) return;

    g_virtualMachine->Resume();
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_pauseVirtualMachine(
    JNIEnv* env,
    jobject obj)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    assert(g_virtualMachine != nullptr);
    if(g_virtualMachine == nullptr) return;

    g_virtualMachine->Pause();
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_loadState(
    JNIEnv* env,
    jobject obj,
    jint slot)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    assert(g_virtualMachine != nullptr);
    if(g_virtualMachine == nullptr) return;

    auto stateFilePath =
        g_virtualMachine->GenerateStatePath(slot);

    auto resultFuture =
        g_virtualMachine->LoadState(stateFilePath);

    if(!resultFuture.get())
    {
        jclass exceptionClass =
            env->FindClass("java/lang/Exception");

        env->ThrowNew(exceptionClass, "LoadState failed.");
        return;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_saveState(
    JNIEnv* env,
    jobject obj,
    jint slot)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    assert(g_virtualMachine != nullptr);
    if(g_virtualMachine == nullptr) return;

    auto stateFilePath =
        g_virtualMachine->GenerateStatePath(slot);

    auto resultFuture =
        g_virtualMachine->SaveState(stateFilePath);

    if(!resultFuture.get())
    {
        jclass exceptionClass =
            env->FindClass("java/lang/Exception");

        env->ThrowNew(exceptionClass, "SaveState failed.");
        return;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_loadElf(
    JNIEnv* env,
    jobject obj,
    jstring selectedFilePath)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    assert(g_virtualMachine != nullptr);
    if(g_virtualMachine == nullptr) return;

    ResetVirtualMachine();

    try
    {
        g_virtualMachine->m_ee->m_os->BootFromFile(
            GetStringFromJstring(env, selectedFilePath));
    }
    catch(const std::exception& exception)
    {
        jclass exceptionClass =
            env->FindClass("java/lang/Exception");

        env->ThrowNew(exceptionClass, exception.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_bootDiskImage(
    JNIEnv* env,
    jobject obj,
    jstring selectedFilePath)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    assert(g_virtualMachine != nullptr);
    if(g_virtualMachine == nullptr) return;

    CAppConfig::GetInstance().SetPreferencePath(
        PREF_PS2_CDROM0_PATH,
        GetStringFromJstring(env, selectedFilePath));

    ResetVirtualMachine();

    try
    {
        g_virtualMachine->m_ee->m_os->BootFromCDROM();
    }
    catch(const std::exception& exception)
    {
        jclass exceptionClass =
            env->FindClass("java/lang/Exception");

        env->ThrowNew(exceptionClass, exception.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_setupGsHandler(
    JNIEnv* env,
    jclass clazz,
    jobject surface,
    jint width,
    jint height,
    jfloat refreshRate)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    ANativeWindow* nativeWindow = nullptr;

    if(surface != nullptr)
    {
        nativeWindow = ANativeWindow_fromSurface(env, surface);
    }

    CLog::GetInstance().Print(
        "nativeinterop",
        "[ORBIT2SX_BOOT] setupGsHandler surface=%p nativeWindow=%p valid=%u\r\n",
        surface,
        nativeWindow,
        nativeWindow != nullptr ? 1 : 0);
    
    // Tidak ada VM: lepaskan reference yang baru diperoleh.
    if(g_virtualMachine == nullptr)
    {
        if(nativeWindow != nullptr)
        {
            ANativeWindow_release(nativeWindow);
        }
        return;
    }

    auto gsHandler = g_virtualMachine->GetGSHandler();

    int gsHandlerId =
        CAppConfig::GetInstance().GetPreferenceInteger(
            PREF_VIDEO_GS_HANDLER);

    bool recreateNeeded =
        (gsHandler == nullptr) ||
        (g_currentGsHandlerId != gsHandlerId);

    CLog::GetInstance().Print(
        "nativeinterop",
        "[ORBIT2SX_BOOT] GS handler=%p id=%d currentId=%d recreate=%u\r\n",
        gsHandler,
        gsHandlerId,
        g_currentGsHandlerId,
        recreateNeeded ? 1 : 0);

    if(recreateNeeded)
    {
        // Factory menerima ownership reference awal.
        switch(gsHandlerId)
        {
        default:
        case PREFERENCE_VALUE_VIDEO_GS_HANDLER_OPENGL:
            g_virtualMachine->CreateGSHandler(
                CGSH_OpenGLAndroid::GetFactoryFunction(
                    nativeWindow));
            break;

        case PREFERENCE_VALUE_VIDEO_GS_HANDLER_VULKAN:
            g_virtualMachine->CreateGSHandler(
                CGSH_VulkanAndroid::GetFactoryFunction(
                    nativeWindow));
            break;
        }

        g_currentGsHandlerId = gsHandlerId;

        CLog::GetInstance().Print(
            "nativeinterop",
            "[ORBIT2SX_BOOT] GS handler creation requested id=%d\r\n",
            gsHandlerId);

        g_OnGsNewFrameConnection =
            g_virtualMachine->m_ee->m_gs->OnNewFrame.Connect(
                std::bind(
                    &CStatsManager::OnGsNewFrame,
                    &CStatsManager::GetInstance(),
                    std::placeholders::_1));
    }
    else
    {
        auto windowUpdateListener =
            dynamic_cast<INativeWindowUpdateListener*>(
                gsHandler);

        if(windowUpdateListener != nullptr)
        {
            CLog::GetInstance().Print(
                "nativeinterop",
                "[ORBIT2SX_BOOT] updating existing GS window=%p\r\n",
                nativeWindow);
            
            // SetWindow mengambil ownership reference,
            // termasuk ketika window bernilai nullptr.
            windowUpdateListener->SetWindow(nativeWindow);
        }
        else if(nativeWindow != nullptr)
        {
            // Tidak ada penerima: lepaskan reference.
            ANativeWindow_release(nativeWindow);
        }
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_NativeInterop_notifyPreferencesChanged(
    JNIEnv* env,
    jobject obj)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    if(g_virtualMachine == nullptr)
    {
        return;
    }

    SetupSoundHandler();

    g_virtualMachine->ReloadSpuBlockCount();
    g_virtualMachine->ReloadFrameRateLimit();

    auto gsHandler = g_virtualMachine->GetGSHandler();

    if(gsHandler != nullptr)
    {
        gsHandler->NotifyPreferencesChanged();
    }
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_virtualapplications_play_NativeInterop_getGraphicsDiagnostics(
    JNIEnv* env,
    jobject obj)
{
    const GraphicsDiagnostics diagnostics =
        GetGraphicsDiagnostics();

    jclass stringClass =
        env->FindClass("java/lang/String");

    if(stringClass == nullptr)
    {
        return nullptr;
    }

    jobjectArray result =
        env->NewObjectArray(4, stringClass, nullptr);

    env->DeleteLocalRef(stringClass);

    if(result == nullptr)
    {
        return nullptr;
    }

    const std::string values[4] = {
        diagnostics.vendor,
        diagnostics.renderer,
        diagnostics.version,
        diagnostics.shadingLanguageVersion
    };

    for(jsize i = 0; i < 4; i++)
    {
        jstring value =
            env->NewStringUTF(values[i].c_str());

        if(value == nullptr)
        {
            return result;
        }

        env->SetObjectArrayElement(result, i, value);
        env->DeleteLocalRef(value);
    }

    return result;
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_virtualapplications_play_NativeInterop_getAudioDiagnostics(
    JNIEnv* env,
    jobject obj)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    // [0] OpenSL handler active: 1=yes, 0=no, -1=unknown
    // [1] Queued buffers
    // [2] Buffer capacity
    // [3] Queue-empty events
    // [4] Backpressure wait count
    // [5] Total backpressure wait nanoseconds

    jlong values[6] = {
        -1, -1, -1, -1, -1, -1
    };

    if(g_virtualMachine != nullptr)
    {
        std::lock_guard<std::mutex> lock(
            g_soundHandlerDiagnosticsMutex);

        CSoundHandler* baseHandler =
            g_virtualMachine->GetSoundHandler();

        auto* soundHandler =
            dynamic_cast<CSH_OpenSL*>(baseHandler);

        values[0] = soundHandler != nullptr ? 1 : 0;

        if(soundHandler != nullptr)
        {
            values[1] = static_cast<jlong>(
                soundHandler->GetQueuedBufferCount());

            values[2] = static_cast<jlong>(
                soundHandler->GetBufferCapacity());

            values[3] = static_cast<jlong>(
                soundHandler->GetUnderrunCount());

            values[4] = static_cast<jlong>(
                soundHandler->GetBackpressureWaitCount());

            values[5] = static_cast<jlong>(
                soundHandler->GetBackpressureWaitNanoseconds());
        }
    }

    jlongArray result = env->NewLongArray(6);

    if(result != nullptr)
    {
        env->SetLongArrayRegion(
            result,
            0,
            6,
            values);
    }

    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_virtualapplications_play_StatsManager_getGsPipelineDiagnostics(
    JNIEnv* env,
    jclass clazz)
{
    const std::string diagnostics =
        CStatsManager::GetInstance()
            .GetGsPipelineDiagnostics();

    return env->NewStringUTF(diagnostics.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_virtualapplications_play_StatsManager_getRuntimeExecutionDiagnostics(
    JNIEnv* env,
    jclass clazz)
{
    const std::string diagnostics =
        CStatsManager::GetInstance()
            .GetRuntimeExecutionDiagnostics();

    return env->NewStringUTF(diagnostics.c_str());
}
