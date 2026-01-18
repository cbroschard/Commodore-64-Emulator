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

        ToggleJoy1,
        ToggleJoy2,

        EnterMonitor,
        Quit
    };

    Type type;
    std::string path;
};


#endif // UICOMMAND_H_INCLUDED
