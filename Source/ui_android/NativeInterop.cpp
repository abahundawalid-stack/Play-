extern "C" JNIEXPORT void JNICALL Java_com_virtualapplications_play_NativeInterop_setupGsHandler(
    JNIEnv* env, jobject obj, jobject surface)
{
    ANativeWindow* nativeWindow =
        surface != nullptr ? ANativeWindow_fromSurface(env, surface) : nullptr;

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
        CAppConfig::GetInstance().GetPreferenceInteger(PREF_VIDEO_GS_HANDLER);

    bool recreateNeeded =
        (gsHandler == nullptr) || (g_currentGsHandlerId != gsHandlerId);

    if(recreateNeeded)
    {
        // Ownership of nativeWindow transfers to the factory and then
        // to the newly constructed GS handler.
        switch(gsHandlerId)
        {
        default:
        case PREFERENCE_VALUE_VIDEO_GS_HANDLER_OPENGL:
            g_virtualMachine->CreateGSHandler(
                CGSH_OpenGLAndroid::GetFactoryFunction(nativeWindow));
            break;

        case PREFERENCE_VALUE_VIDEO_GS_HANDLER_VULKAN:
            g_virtualMachine->CreateGSHandler(
                CGSH_VulkanAndroid::GetFactoryFunction(nativeWindow));
            break;
        }

        g_currentGsHandlerId = gsHandlerId;

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
            dynamic_cast<INativeWindowUpdateListener*>(gsHandler);

        if(windowUpdateListener != nullptr)
        {
            // SetWindow takes ownership, including nullptr.
            windowUpdateListener->SetWindow(nativeWindow);
        }
        else if(nativeWindow != nullptr)
        {
            // No receiver: release the acquired reference here.
            ANativeWindow_release(nativeWindow);
        }
    }
}
