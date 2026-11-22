#include <jni.h>

#include <cmath>
#include <exception>
#include <string>

#include "MemStream.h"
#include "make_unique.h"
#include "xml/Writer.h"

#include "InputManager.h"
#include "NativeShared.h"

#include "../PH_Generic.h"
#include "../PS2VM.h"
#include "../VirtualPad.h"

#include "com_virtualapplications_play_InputManagerConstants.h"

// -----------------------------------------------------------------------------
// Java / C++ controller mapping verification
// -----------------------------------------------------------------------------
// These checks intentionally remain at compile time.
// A mismatch between Java button constants and PS2 controller definitions
// must stop the build instead of silently producing incorrect input mapping.

static_assert(
    com_virtualapplications_play_InputManagerConstants_ANALOG_LEFT_X ==
        PS2::CControllerInfo::ANALOG_LEFT_X,
    "Input code mismatch: ANALOG_LEFT_X");

static_assert(
    com_virtualapplications_play_InputManagerConstants_ANALOG_LEFT_Y ==
        PS2::CControllerInfo::ANALOG_LEFT_Y,
    "Input code mismatch: ANALOG_LEFT_Y");

static_assert(
    com_virtualapplications_play_InputManagerConstants_ANALOG_RIGHT_X ==
        PS2::CControllerInfo::ANALOG_RIGHT_X,
    "Input code mismatch: ANALOG_RIGHT_X");

static_assert(
    com_virtualapplications_play_InputManagerConstants_ANALOG_RIGHT_Y ==
        PS2::CControllerInfo::ANALOG_RIGHT_Y,
    "Input code mismatch: ANALOG_RIGHT_Y");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_UP ==
        PS2::CControllerInfo::DPAD_UP,
    "Input code mismatch: BUTTON_UP");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_DOWN ==
        PS2::CControllerInfo::DPAD_DOWN,
    "Input code mismatch: BUTTON_DOWN");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_LEFT ==
        PS2::CControllerInfo::DPAD_LEFT,
    "Input code mismatch: BUTTON_LEFT");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_RIGHT ==
        PS2::CControllerInfo::DPAD_RIGHT,
    "Input code mismatch: BUTTON_RIGHT");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_SELECT ==
        PS2::CControllerInfo::SELECT,
    "Input code mismatch: BUTTON_SELECT");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_START ==
        PS2::CControllerInfo::START,
    "Input code mismatch: BUTTON_START");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_SQUARE ==
        PS2::CControllerInfo::SQUARE,
    "Input code mismatch: BUTTON_SQUARE");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_TRIANGLE ==
        PS2::CControllerInfo::TRIANGLE,
    "Input code mismatch: BUTTON_TRIANGLE");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_CIRCLE ==
        PS2::CControllerInfo::CIRCLE,
    "Input code mismatch: BUTTON_CIRCLE");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_CROSS ==
        PS2::CControllerInfo::CROSS,
    "Input code mismatch: BUTTON_CROSS");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_L1 ==
        PS2::CControllerInfo::L1,
    "Input code mismatch: BUTTON_L1");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_L2 ==
        PS2::CControllerInfo::L2,
    "Input code mismatch: BUTTON_L2");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_L3 ==
        PS2::CControllerInfo::L3,
    "Input code mismatch: BUTTON_L3");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_R1 ==
        PS2::CControllerInfo::R1,
    "Input code mismatch: BUTTON_R1");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_R2 ==
        PS2::CControllerInfo::R2,
    "Input code mismatch: BUTTON_R2");

static_assert(
    com_virtualapplications_play_InputManagerConstants_BUTTON_R3 ==
        PS2::CControllerInfo::R3,
    "Input code mismatch: BUTTON_R3");

static_assert(
    com_virtualapplications_play_InputManagerConstants_MAX ==
        PS2::CControllerInfo::MAX_BUTTONS,
    "Input code mismatch: MAX_BUTTONS");

// -----------------------------------------------------------------------------
// VM reference
// -----------------------------------------------------------------------------

extern CPS2VM* g_virtualMachine;

// -----------------------------------------------------------------------------
// Input validation helpers
// -----------------------------------------------------------------------------

namespace
{
    bool IsValidButtonId(int buttonId)
    {
        return buttonId >= 0 &&
               buttonId < PS2::CControllerInfo::MAX_BUTTONS;
    }

    bool IsValidScreenSize(float width, float height)
    {
        return std::isfinite(width) &&
               std::isfinite(height) &&
               width > 0.0f &&
               height > 0.0f;
    }

    float SanitizeAxisValue(float value)
    {
        if(!std::isfinite(value))
        {
            return 0.0f;
        }

        if(value < -1.0f)
        {
            return -1.0f;
        }

        if(value > 1.0f)
        {
            return 1.0f;
        }

        return value;
    }

    CPH_Generic* GetGenericPadHandler()
    {
        // The VM is created by NativeInterop::createVirtualMachine().
        // Input can arrive before the VM is ready, so reject that state.
        CPS2VM* vm = g_virtualMachine;

        if(vm == nullptr)
        {
            return nullptr;
        }

        CPadHandler* padHandler = vm->GetPadHandler();

        if(padHandler == nullptr)
        {
            return nullptr;
        }

        // NativeInterop creates the Android pad handler using
        // CPH_Generic::GetFactoryFunction().
        // dynamic_cast prevents an invalid downcast if the factory changes.
        return dynamic_cast<CPH_Generic*>(padHandler);
    }
}

// -----------------------------------------------------------------------------
// Button input
// -----------------------------------------------------------------------------

void CInputManager::SetButtonState(int buttonId, bool pressed)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    if(!IsValidButtonId(buttonId))
    {
        return;
    }

    const auto button =
        static_cast<PS2::CControllerInfo::BUTTON>(buttonId);

    // Analog axes must be updated through SetAxisState().
    if(PS2::CControllerInfo::IsAxis(button))
    {
        return;
    }

    CPH_Generic* padHandler = GetGenericPadHandler();

    if(padHandler == nullptr)
    {
        return;
    }

    padHandler->SetButtonState(
        static_cast<uint32>(buttonId),
        pressed);
}

// -----------------------------------------------------------------------------
// Analog input
// -----------------------------------------------------------------------------

void CInputManager::SetAxisState(int buttonId, float value)
{
    std::lock_guard<std::mutex> lock(
        GetVirtualMachineMutex());
    
    if(!IsValidButtonId(buttonId))
    {
        return;
    }

    const auto button =
        static_cast<PS2::CControllerInfo::BUTTON>(buttonId);

    // Reject digital buttons accidentally sent through the analog API.
    if(!PS2::CControllerInfo::IsAxis(button))
    {
        return;
    }

    const float safeValue = SanitizeAxisValue(value);

    CPH_Generic* padHandler = GetGenericPadHandler();

    if(padHandler == nullptr)
    {
        return;
    }

    padHandler->SetAxisState(
        static_cast<uint32>(buttonId),
        safeValue);
}

// -----------------------------------------------------------------------------
// JNI: digital button state
// -----------------------------------------------------------------------------

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_InputManager_setButtonState(
    JNIEnv* env,
    jobject obj,
    jint buttonId,
    jboolean pressed)
{
    CInputManager::GetInstance().SetButtonState(
        static_cast<int>(buttonId),
        pressed == JNI_TRUE);
}

// -----------------------------------------------------------------------------
// JNI: analog axis state
// -----------------------------------------------------------------------------

extern "C" JNIEXPORT void JNICALL
Java_com_virtualapplications_play_InputManager_setAxisState(
    JNIEnv* env,
    jobject obj,
    jint buttonId,
    jfloat value)
{
    CInputManager::GetInstance().SetAxisState(
        static_cast<int>(buttonId),
        static_cast<float>(value));
}

// -----------------------------------------------------------------------------
// JNI: virtual gamepad layout
// -----------------------------------------------------------------------------

extern "C" JNIEXPORT jstring JNICALL
Java_com_virtualapplications_play_InputManager_getVirtualPadItems(
    JNIEnv* env,
    jobject obj,
    jfloat screenWidth,
    jfloat screenHeight)
{
    if(env == nullptr || env->ExceptionCheck())
    {
        return nullptr;
    }

    try
    {
        // Invalid dimensions must not enter VirtualPad layout calculations.
        const bool validDimensions =
            IsValidScreenSize(screenWidth, screenHeight);

        CVirtualPad::ItemArray items;

        if(validDimensions)
        {
            items = CVirtualPad::GetItems(
                static_cast<float>(screenWidth),
                static_cast<float>(screenHeight));
        }

        auto documentNode =
            std::make_unique<Framework::Xml::CNode>(
                "Document",
                true);

        for(const auto& item : items)
        {
            auto itemNode =
                std::make_unique<Framework::Xml::CNode>(
                    "Item",
                    true);

            itemNode->InsertAttribute(
                "isAnalog",
                item.isAnalog ? "true" : "false");

            itemNode->InsertAttribute(
                "x1",
                std::to_string(item.x1).c_str());

            itemNode->InsertAttribute(
                "y1",
                std::to_string(item.y1).c_str());

            itemNode->InsertAttribute(
                "x2",
                std::to_string(item.x2).c_str());

            itemNode->InsertAttribute(
                "y2",
                std::to_string(item.y2).c_str());

            itemNode->InsertAttribute(
                "code0",
                std::to_string(item.code0).c_str());

            itemNode->InsertAttribute(
                "code1",
                std::to_string(item.code1).c_str());

            itemNode->InsertAttribute(
                "caption",
                item.caption.c_str());

            itemNode->InsertAttribute(
                "imageName",
                item.imageName.c_str());

            documentNode->InsertNode(
                std::move(itemNode));
        }

        Framework::CMemStream outputStream;

        Framework::Xml::CWriter::WriteDocument(
            outputStream,
            documentNode.get());

        const auto outputSize = outputStream.GetSize();

        const auto* outputBuffer = outputStream.GetBuffer();

        if(outputBuffer == nullptr && outputSize != 0)
        {
            return nullptr;
        }

        std::string resultString;

        if(outputSize != 0)
        {
            resultString.assign(
                outputBuffer,
                outputBuffer + outputSize);
        }

        // The current virtual-pad XML contains only:
        // - fixed ASCII captions,
        // - fixed ASCII image names,
        // - numeric coordinates and button IDs,
        // - XML markup.
        //
        // Therefore the generated string is valid ASCII, which is also
        // a valid subset of Modified UTF-8 accepted by NewStringUTF().
        return env->NewStringUTF(resultString.c_str());
    }
    catch(const std::exception&)
    {
        // Never allow a C++ exception to escape across the JNI boundary.
        return nullptr;
    }
}
