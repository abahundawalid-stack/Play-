#include <cmath>
#include <mutex>
#include <cassert>
#include <cstring>
#include "PH_Generic.h"

CPH_Generic::CPH_Generic()
{
	memset(&m_buttonStates, 0, sizeof(m_buttonStates));
	memset(&m_axisStates, 0, sizeof(m_axisStates));
}

CPH_Generic::~CPH_Generic()
{
}

CPadHandler::FactoryFunction CPH_Generic::GetFactoryFunction()
{
	return []() { return new CPH_Generic(); };
}

void CPH_Generic::Update(uint8* ram)
{
    // Take a consistent snapshot of the input state.
    // The mutex is released before calling PadInterface methods.
    bool buttonStates[PS2::CControllerInfo::MAX_BUTTONS];
    float axisStates[PS2::CControllerInfo::MAX_BUTTONS];

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);

        memcpy(
            buttonStates,
            m_buttonStates,
            sizeof(buttonStates));

        memcpy(
            axisStates,
            m_axisStates,
            sizeof(axisStates));
    }

    // Process the snapshot without holding the input mutex.
    for(auto& interface : m_interfaces)
    {
        for(unsigned int i = 0;
            i < PS2::CControllerInfo::MAX_BUTTONS;
            i++)
        {
            auto button =
                static_cast<PS2::CControllerInfo::BUTTON>(i);

            if(PS2::CControllerInfo::IsAxis(button))
            {
                float buttonValue =
                    ((axisStates[i] + 1.0f) / 2.0f) * 255.0f;

                interface->SetAxisState(
                    0,
                    button,
                    static_cast<uint8>(buttonValue),
                    ram);
            }
            else
            {
                interface->SetButtonState(
                    0,
                    button,
                    buttonStates[i],
                    ram);
            }
        }
    }
}

void CPH_Generic::SetButtonState(uint32 buttonId, bool pressed)
{
    if(buttonId >= PS2::CControllerInfo::MAX_BUTTONS)
    {
        return;
    }

    auto button =
        static_cast<PS2::CControllerInfo::BUTTON>(buttonId);

    if(PS2::CControllerInfo::IsAxis(button))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_stateMutex);

    m_buttonStates[buttonId] = pressed;
}

void CPH_Generic::SetAxisState(uint32 buttonId, float value)
{
    if(buttonId >= PS2::CControllerInfo::MAX_BUTTONS)
    {
        return;
    }

    auto button =
        static_cast<PS2::CControllerInfo::BUTTON>(buttonId);

    if(!PS2::CControllerInfo::IsAxis(button))
    {
        return;
    }

    if(!std::isfinite(value))
    {
        value = 0.0f;
    }

    if(value < -1.0f)
    {
        value = -1.0f;
    }
    else if(value > 1.0f)
    {
        value = 1.0f;
    }

    std::lock_guard<std::mutex> lock(m_stateMutex);

    m_axisStates[buttonId] = value;
}

