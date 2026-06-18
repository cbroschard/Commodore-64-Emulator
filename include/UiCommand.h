// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef UICOMMAND_H_INCLUDED
#define UICOMMAND_H_INCLUDED

#include <cstdint>
#include <string>
#include "Common/REUModel.h"

struct UiCommand
{
    enum class Type
    {
        SaveState,
        LoadState,
        WarmReset,
        ColdReset,
        SetPAL,
        SetNTSC,
        TogglePause,

        AttachDisk,
        AttachPRG,
        AttachPRGWithCartridge,
        AttachCRT,
        AttachT64,
        AttachTAP,

        CreateBlankDisk,

        EjectDisk,
        EjectCRT,
        EjectTape,

        PressButton,
        SetCartSwitch,

        LoadIDE64Image,
        CreateIDE64Image,
        SaveIDE64Image,
        EjectIDE64Image,

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

        SetMOS6581,
        SetMOS8580,

        SetREU,

        EnterMonitor,
        Quit
    };

    enum class DriveType
    {
        None,
        D1541,
        D1571,
        D1581
    };

    int deviceNum = 8;
    DriveType driveType = DriveType::D1541;

    Type type;
    std::string path;

    uint32_t ide64DeviceIndex   = 0;
    uint32_t ide64Sectors       = 0;
    bool ide64ReadOnly          = false;

    uint32_t buttonIndex        = 0;
    uint32_t switchIndex        = 0;
    uint32_t switchPos          = 0;

    REUModel reuModel           = REUModel::None;
};


#endif // UICOMMAND_H_INCLUDED
