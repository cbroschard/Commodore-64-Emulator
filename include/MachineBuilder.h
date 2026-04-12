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
