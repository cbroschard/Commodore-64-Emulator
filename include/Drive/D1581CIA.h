// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1581CIA_H
#define D1581CIA_H

#include "Drive/DriveCIA.h"

// Forward declarations
class D1581;
class Peripheral;

class D1581CIA : public DriveCIA
{
    public:
        D1581CIA();
        ~D1581CIA() override;

        inline void attachPeripheralInstance(Peripheral* parentPeripheral) { this->parentPeripheral = parentPeripheral; }

        void reset() override;

        void setIECInputs(bool atnLow, bool clkLow, bool dataLow, bool srqLow);
        void refreshIECPortState();

        void primeAtnLevel(bool atnLow);

        // ML Monitor
        ciaIECDecodeView getIECDecodeView() const override;
        inline void recordDebugCIAWrite(uint16_t pc, uint16_t retTarget, uint16_t address, uint8_t reg, uint8_t value)
                                        { recordIECWrite(pc, retTarget, address, reg, value); }
        void recordDebugCIARead(uint16_t pc, uint16_t retTarget, uint16_t address, uint8_t reg, uint8_t value);

    protected:
        void portAOutputChanged(uint8_t pra, uint8_t ddra) override;
        void portBOutputChanged(uint8_t prb, uint8_t ddrb) override;
        void irqLineChanged(bool active) override;
        void serialOutputBit(bool bit) override;
        void serialOutputClockPulse() override;
        void serialOutputFinished() override;

    private:
        // Non-owning pointers
        Peripheral* parentPeripheral;

        enum CIA_PRA : uint8_t
        {
            PRA_SIDE    = 1u << 0, // 0 = side 0, 1 = side 1
            PRA_DRVRDY  = 1u << 1, // 1 = drive ready (input)
            PRA_MOTOR   = 1u << 2, // 0 = on, 1 = off
            PRA_DEVSW2  = 1u << 3, // device switch 2 (right)
            PRA_DEVSW1  = 1u << 4, // device switch 1 (left)
            PRA_ACTLED  = 1u << 5, // green LED
            PRA_ERRLED  = 1u << 6, // red LED
            PRA_DSKCH   = 1u << 7  // disk present/change
        };

        enum CIA_PRB : uint8_t
        {
            PRB_DATAIN  = 1u << 0,
            PRB_DATOUT  = 1u << 1,
            PRB_CLKIN   = 1u << 2,
            PRB_CLKOUT  = 1u << 3,
            PRB_ATNACK  = 1u << 4,
            PRB_BUSDIR  = 1u << 5,
            PRB_WRTPRO  = 1u << 6,
            PRB_ATNIN   = 1u << 7
        };

        bool iecAtnInLow;
        bool iecClkInLow;
        bool iecDataInLow ;
        bool iecSrqInLow;
        bool lastAtnLow ;

        bool updating;

        uint8_t makePortBPins() const;
        void updateInputPins();
        void applyIECOutputs();

        D1581* drive() const;

        struct IECWriteTrace
        {
            bool valid = false;
            uint16_t pc = 0;
            uint16_t retTarget = 0xFFFF;
            uint16_t address = 0;
            uint8_t reg = 0;
            uint8_t value = 0;
            uint8_t prAfter = 0;
            uint8_t ddrAfter = 0;
        };

        struct IECReadTrace
        {
            bool valid = false;
            uint16_t pc = 0;
            uint16_t retTarget = 0;
            uint16_t address = 0;
            uint8_t reg = 0;
            uint8_t value = 0;
        };

        struct IECOutputDecode
        {
            bool busDirOutput = false;
            bool atnAckDataLow = false;
            bool datOutAssertLow = false;
            bool clkOutAssertLow = false;
            bool finalDataLow = false;
            bool finalClkLow = false;
        };

        IECOutputDecode decodeIECOutputs() const;

        IECWriteTrace iecWriteHistory[16]{};
        uint8_t iecWriteHistoryPos = 0;

        IECReadTrace iecReadHistory[8]{};
        uint8_t iecReadHistoryPos = 0;

        uint8_t lastReadValue = 0xFF;
        uint32_t sameReadCount = 0;

        void recordIECWrite(uint16_t pc, uint16_t retTarget, uint16_t address, uint8_t reg, uint8_t value);
};

#endif // D1581CIA_H
