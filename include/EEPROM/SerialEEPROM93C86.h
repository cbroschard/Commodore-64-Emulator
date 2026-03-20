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

        void setCS(bool level) override;
        void setCLK(bool level) override;
        void setDI(bool level) override;

        bool getDO() const override { return dout; }

        void save(StateWriter& wrtr) const override;
        bool load(StateReader& rdr) override;

        bool saveRaw(std::ostream& out) const override;
        bool loadRaw(std::istream& in) override;

    protected:

    private:
        std::array<uint8_t, 2048> data{};

        enum class Command
        {
            None,
            Read,
            Write,
            Erase,
            Ewen,
            Ewds,
            Eral,
            Wral
        };

        Command currentCmd;
        uint16_t currentAddress;
        uint16_t outShiftReg;
        uint32_t outBitCount;
        bool writeEnableLatch;
        bool commandLatched;

        bool readDummyPending;

        bool cs;
        bool clk;
        bool di;
        bool dout;

        // protocol state
        uint32_t shiftReg;
        uint32_t bitCount;
        bool prevClk;

        // Helpers
        void resetTransaction();
        void handleRisingEdge();
        void decodeCommandIfReady();
        void prepareReadData();
        void commitWriteByte(uint8_t value);
        void eraseByte();
        void eraseAll();
        void writeAll(uint8_t value);
};

#endif // SERIALEEPROM93C86_H
