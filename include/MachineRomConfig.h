// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MACHINE_ROM_CONFIG_H
#define MACHINE_ROM_CONFIG_H

#include <string>

struct MachineRomConfig
{
    std::string kernalRom;
    std::string basicRom;
    std::string charRom;

    std::string d1541LoRom;
    std::string d1541HiRom;
    std::string d1571Rom;
    std::string d1581Rom;
};

#endif // MACHINE_ROM_CONFIG_H
