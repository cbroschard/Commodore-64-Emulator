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
    std::string kernalROM;
    std::string basicROM;
    std::string charROM;

    std::string d1541LoROM;
    std::string d1541HiROM;
    std::string d1571ROM;
    std::string d1581ROM;
    std::string d2031LoROM;
    std::string d2031HiROM;
    std::string d4040LoROM;
    std::string d4040HiROM1;
    std::string d4040HiROM2;
};

#endif // MACHINE_ROM_CONFIG_H
