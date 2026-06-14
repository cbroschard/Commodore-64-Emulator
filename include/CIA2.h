// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef CIA2_H
#define CIA2_H


// Forward declarations
class Cassette;
class CPU;
class IECBUS;
class Vic;

#include "CIA6526.h"
#include "Common/BCD.h"
#include "IECBUS.h"
#include "Logging.h"
#include "RS232Device.h"

class CIA2 : public CIA6526
{
    public:
        CIA2();
        virtual ~CIA2();

        inline void attachCPUInstance(CPU* cpu) { this->cpu = cpu; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachIECBusInstance(IECBUS* bus) { this->bus = bus; recomputeIEC(); }
        inline void attachRS232DeviceInstance(RS232Device* rs232dev) { this->rs232dev = rs232dev; }
        inline void attachVicInstance(Vic* vic) { this->vic = vic; }

        // State management
        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        // Reset all to defaults
        void reset();

        // Getter to find current VIC bank
        uint16_t getCurrentVICBank() const;

        // IECBUS connectivity
        void clkChanged(bool level);
        void dataChanged(bool state);
        void atnChanged(bool asserted);
        void srqChanged(bool level);

        // Setter for device number set by actual devices
        inline void setDeviceNumber(uint8_t number) { deviceNumber = number; }

        // ML Monitor access
        struct IECSnapshot
        {
            uint8_t pra = 0;
            uint8_t ddra = 0;

            bool atnOutReleased = true;
            bool clkOutReleased = true;
            bool dataOutReleased = true;

            bool clkInHigh = true;
            bool dataInHigh = true;
            bool srqInHigh = true;

            bool legacyProtocolEnabled = false;
            bool legacyListening = false;
            bool legacyTalking = false;
            uint8_t legacySecondaryAddress = 0xFF;
        };

        IECSnapshot snapshotIEC() const;
        std::string debugIECSnapshotString() const;

        std::string dumpRegisters(const std::string& group) const;

    protected:
        void postTimerUpdates(uint32_t cyclesElapsed) override;

        inline int getCIANumber() const override { return 2; }
        inline const char* getCIAName() const override { return "CIA2"; }

        uint8_t readPortA() override;
        uint8_t readPortB() override;

        void portAOutputChanged(uint8_t value) override;
        void portBOutputChanged(uint8_t value) override;

        void irqLineChanged(bool active) override;

        TraceManager::Stamp makeCIAStamp() const override;

    private:

        // non-owning pointers
        CPU* cpu;
        IECBUS* bus;
        Logging* logger;
        RS232Device* rs232dev;
        Vic* vic;

        // Constants
        static constexpr uint8_t VIC_BANK0     = 0x01;  // PA0
        static constexpr uint8_t VIC_BANK1     = 0x02;  // PA1
        static constexpr uint8_t MASK_ATN_OUT  = 0x08; // PA3
        static constexpr uint8_t MASK_CLK_OUT  = 0x10; // PA4
        static constexpr uint8_t MASK_DATA_OUT = 0x20; // PA5
        static constexpr uint8_t MASK_CLK_IN   = 0x40; // PA6
        static constexpr uint8_t MASK_DATA_IN  = 0x80; // PA7
        static constexpr uint8_t DSR_MASK      = 0x80;  // Data Set Ready PB7
        static constexpr uint8_t CTS_MASK      = 0x40;  // Clear To Send PB6
        static constexpr uint8_t DCD_MASK      = 0x10;  // Data Carrier Detect PB4
        static constexpr uint8_t RI_MASK       = 0x08;  // Ring Indicator PB3
        static constexpr uint8_t DTR_MASK      = 0x04;  // Data Terminal Ready PB2
        static constexpr uint8_t RTS_MASK      = 0x02;  // Request To Send PB1
        static constexpr uint8_t TXD_MASK      = 0x04;  // Transmit Data PA2
        static constexpr uint8_t RXD_MASK      = 0x01;  // Receive Data PB0

        // IECBUS
        uint8_t deviceNumber;
        uint8_t currentSecondaryAddress;
        bool listening;
        bool talking;
        bool lastClk; // Remember previous clock level
        bool atnLine;
        bool atnHandshakePending;
        bool atnHandshakeJustCleared;
        bool lastSrqLevel;
        bool lastDataLevel;
        bool lastAtnLevel;
        uint8_t iecCmdShiftReg;
        int iecCmdBitCount;
        void decodeIECCommand(uint8_t cmd);

        // NMI Level
        bool nmiAsserted;

        // IEC Debugging flag
        bool iecProtocolEnabled;

        // IEC helper
        void recomputeIEC();

        // RS232 helper
        void updateRS232Outputs();
};

#endif // CIA2_H
