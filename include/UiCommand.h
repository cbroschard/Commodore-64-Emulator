// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef UICOMMAND_H_INCLUDED
#define UICOMMAND_H_INCLUDED

#include <string>

struct UiCommand
{
    enum class Type
    {
        WarmReset,
        ColdReset,
        SetPAL,
        SetNTSC,
        TogglePause,

        AttachDisk,
        AttachPRG,
        AttachCRT,
        AttachT64,
        AttachTAP,

        EjectDisk,
        EjectCRT,
        EjectTape,

        CassPlay,
        CassStop,
        CassRewind,
        CassEject,

        AssignPad1ToPort1,
        AssignPad1ToPort2,
        AssignPad2ToPort1,
        AssignPad2ToPort2,
        ClearPort1Pad,
        ClearPort2Pad,
        SwapPortPads,
        ToggleJoy1,
        ToggleJoy2,

        EnterMonitor,
        Quit
    };

    enum class DriveType
    {
        D1541,
        D1571,
        D1581
    };

    int deviceNum = 8;
    DriveType driveType = DriveType::D1541;

    Type type;
    std::string path;
};


#endif // UICOMMAND_H_INCLUDED
