// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MACHINE_COMPONENTS_H
#define MACHINE_COMPONENTS_H

#include <array>
#include <memory>
#include "Cartridge.h"
#include "cassette.h"
#include "CIA1.h"
#include "CIA2.h"
#include "CPU.h"
#include "EmulatorUI.h"
#include "IECBUS.h"
#include "IO.h"
#include "InputManager.h"
#include "InputRouter.h"
#include "IRQLine.h"
#include "keyboard.h"
#include "Logging.h"
#include "MediaManager.h"
#include "Memory.h"
#include "PLA.h"
#include "SID/SID.h"
#include "Vic.h"

class DebugManager;
class Drive;
class ResetController;
class StateManager;
class UIBridge;

struct MachineComponents
{
    std::unique_ptr<Cartridge> cart;
    std::unique_ptr<Cassette> cass;
    std::unique_ptr<CIA1> cia1;
    std::unique_ptr<CIA2> cia2;
    std::unique_ptr<CPU> cpu;
    std::unique_ptr<DebugManager> debug;
    std::array<std::unique_ptr<Drive>, 16> drives;
    std::unique_ptr<EmulatorUI> ui;
    std::unique_ptr<IECBUS> bus;
    std::unique_ptr<InputManager> inputMgr;
    std::unique_ptr<InputRouter> inputRouter;
    std::unique_ptr<IRQLine> irq;
    std::unique_ptr<Keyboard> keyb;
    std::unique_ptr<Logging> logger;
    std::unique_ptr<MediaManager> media;
    std::unique_ptr<Memory> mem;
    std::unique_ptr<PLA> pla;
    std::unique_ptr<ResetController> resetCtl;
    std::unique_ptr<SID> sid;
    std::unique_ptr<StateManager> stateMgr;
    std::unique_ptr<IO> io;
    std::unique_ptr<UIBridge> uiBridge;
    std::unique_ptr<Vic> vic;
};

#endif // MACHINE_COMPONENTS_H
