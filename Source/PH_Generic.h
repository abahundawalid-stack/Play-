#pragma once

#include <mutex>

#include "PadHandler.h"

class CPH_Generic : public CPadHandler
{
public:
    CPH_Generic();
    virtual ~CPH_Generic();

    static FactoryFunction GetFactoryFunction();

    void Update(uint8*) override;

    void SetButtonState(uint32, bool);
    void SetAxisState(uint32, float);

private:
    // Protects input state shared by Android input callbacks
    // and the emulation thread.
    std::mutex m_stateMutex;

    bool m_buttonStates[PS2::CControllerInfo::MAX_BUTTONS];
    float m_axisStates[PS2::CControllerInfo::MAX_BUTTONS];
};
