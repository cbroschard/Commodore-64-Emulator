// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef CPUBUS_H_INCLUDED
#define CPUBUS_H_INCLUDED

#include <cstdint>

class CPUBus
{
public:
    virtual ~CPUBus() = default;

    virtual uint8_t read(uint16_t address) = 0;
    virtual void    write(uint16_t address, uint8_t value) = 0;

    virtual uint8_t peek(uint16_t address) const { return 0xFF; }
};

#endif // CPUBUS_H_INCLUDED
