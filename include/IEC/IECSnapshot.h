#ifndef IECSNAPSHOT_H_INCLUDED
#define IECSNAPSHOT_H_INCLUDED

#include <cstdint>

enum class IECDriveBusState
{
    IDLE,
    AWAITING_COMMAND,
    TALKING,
    LISTENING
};

struct IECDriveSnapshot
{
    // Observed bus levels
    bool atnLow = false;
    bool clkLow = false;
    bool dataLow = false;
    bool srqLow = false;

    // Drive line assertions
    bool drvAssertAtn = false;
    bool drvAssertClk = false;
    bool drvAssertData = false;
    bool drvAssertSrq = false;

    // Protocol state
    IECDriveBusState busState = IECDriveBusState::IDLE;
    bool listening = false;
    bool talking = false;
    int secondaryAddress = -1;

    // Bit shifting
    uint8_t shiftReg = 0;
    int bitsProcessed = 0;
};

#endif // IECSNAPSHOT_H_INCLUDED
