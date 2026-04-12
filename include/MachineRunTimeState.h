// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MACHINE_RUNTIME_STATE_H
#define MACHINE_RUNTIME_STATE_H

#include <atomic>
#include "CPUTiming.h"

struct MachineRuntimeState
{
    std::atomic<bool>& running;
    std::atomic<bool>& uiPaused;

    VideoMode& videoMode;
    const CPUConfig*& cpuCfg;

    bool& pendingBusPrime;
    bool& busPrimedAfterBoot;
};

#endif // MACHINE_RUNTIME_STATE_H
