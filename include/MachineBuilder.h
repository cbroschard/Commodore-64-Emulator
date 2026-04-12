// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MACHINEBUILDER_H
#define MACHINEBUILDER_H

#include "Computer.h"

class MachineBuilder
{
    public:
        MachineBuilder();
        virtual ~MachineBuilder();

        static void assemble(Computer* host,
                         MachineComponents& components_,
                         std::atomic<bool>& uiPaused,
                         std::atomic<bool>& running,
                         VideoMode& videoMode,
                         const CPUConfig*& cpuCfg,
                         std::string& basicRom,
                         std::string& kernalRom,
                         std::string& charRom,
                         std::string& d1541LoRom,
                         std::string& d1541HiRom,
                         std::string& d1571Rom,
                         std::string& d1581Rom,
                         bool& pendingBusPrime,
                         bool& busPrimedAfterBoot);
};

#endif // MACHINEBUILDER_H
