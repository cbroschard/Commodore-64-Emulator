// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IDE64CONTROLLER_H
#define IDE64CONTROLLER_H

#include <cstdint>
#include "StateReader.h"
#include "StateWriter.h"

class IDE64Controller
{
    public:
        IDE64Controller();
        ~IDE64Controller();

        void reset();

        uint8_t readRegister(uint16_t address) const;
        void writeRegister(uint16_t address, uint8_t value);

        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

    protected:

    private:

        struct IDEBusRegisters
        {
            uint8_t taskFile[0x10] = {};   // DE20-DE2F
            uint8_t dataLo         = 0;    // DE30
            uint8_t dataHi         = 0;    // DE31
        } registers;
};

#endif // IDE64CONTROLLER_H
