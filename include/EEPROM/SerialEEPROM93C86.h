// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef SERIALEEPROM93C86_H
#define SERIALEEPROM93C86_H

#include <array>
#include "EEPROM/IEEPROMDevice.h"

class SerialEEPROM93C86 : public IEEPROMDevice
{
    public:
        SerialEEPROM93C86();
        ~SerialEEPROM93C86();

        void reset() override;

        inline void setCS(bool level) override { cs = level; }
        inline void setCLK(bool level) override { clk = level; }
        inline void setDI(bool level) override { di = level; }

        bool getDO() const override { return dout; }

        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        bool savePersistence(const std::string& path) const override;
        bool loadPersistence(const std::string& path) override;

    protected:

    private:
        std::array<uint8_t, 2048> data{};

        bool cs;
        bool clk;
        bool di;
        bool dout;

        // protocol state
        uint32_t shiftReg;
        uint32_t bitCount;
        bool prevClk;
};

#endif // SERIALEEPROM93C86_H
