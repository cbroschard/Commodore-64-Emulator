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
class TraceManager;
class Vic;

#include <cstdint>
#include "common.h"
#include "IECBUS.h"
#include "Logging.h"
#include "RS232Device.h"

class CIA2
{
    public:
        CIA2();
        virtual ~CIA2();

        inline void attachCPUInstance(CPU* processor) { this->processor = processor; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachIECBusInstance(IECBUS* bus) { this->bus = bus; }
        inline void attachRS232DeviceInstance(RS232Device* rs232dev) { this->rs232dev = rs232dev; }
        inline void attachTraceManagerInstance(TraceManager* traceMgr) { this->traceMgr = traceMgr; }
        inline void attachVicInstance(Vic* vicII) { this->vicII = vicII; }

        // Setter for NTSC/PAL
        void setMode(VideoMode mode);

        // Reset all to defaults
        void reset();

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

        // Getter to find current VIC bank
        uint16_t getCurrentVICBank() const;

        // For interrupt control
        uint8_t status;

        // Main function to update the timers
        void updateTimers(uint32_t cyclesElapsed);

        //Interrupt handling
        enum InterruptBit : uint8_t
        {
            INTERRUPT_TIMER_A = 0x01,
            INTERRUPT_TIMER_B = 0x02,
            INTERRUPT_TOD_ALARM = 0x04,
            INTERRUPT_SERIAL_SHIFT_REGISTER = 0x08,
            INTERRUPT_FLAG_LINE = 0x10
        };

        // IECBUS connectivity
        void clkChanged(bool level);
        void dataChanged(bool state);
        void atnChanged(bool asserted);
        void srqChanged(bool level);

        // Setter for device number set by actual devices
        inline void setDeviceNumber(uint8_t number) { deviceNumber = number; }

        // CNT getter/setter
        inline bool getCNTLine() const { return cntLevel; }
        void setCNTLine(bool level);

        // ML Monitor access
        std::string dumpRegisters(const std::string& group) const;
        inline void setLog(bool enable) { setLogging = enable; }
        struct CIA2IRQSnapshot { uint8_t ier; };
        void setIERExact(uint8_t mask);
        inline void clearPendingIRQs() { (void)readRegister(0xDD0D); }
        inline void disableAllIRQs() { setIERExact(0); }
        inline uint8_t getIER() const { return interruptEnable & 0x1F; }
        inline uint8_t getIFR() const { return interruptStatus & 0x1F; }
        inline bool irqLineActive() const { return (interruptStatus & interruptEnable & 0x1F) != 0; }
        inline CIA2IRQSnapshot snapshotIRQs() const { return CIA2IRQSnapshot{getIER()}; }
        inline void restoreIRQs(const CIA2IRQSnapshot& snapshot) { setIERExact(snapshot.ier & 0x1F); }

    protected:

    private:

        // non-owning pointers
        CPU* processor;
        IECBUS* bus;
        Logging* logger;
        RS232Device* rs232dev;
        TraceManager* traceMgr;
        Vic* vicII;

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
        static constexpr uint8_t RXD_MASK      = 0x01;  // Receive Data PB0

        // Video mode
        VideoMode mode_; // NTSC or PAL

        // IECBUS
        uint8_t deviceNumber;
        uint8_t currentSecondaryAddress;
        uint8_t expectedSecondaryAddress;
        bool listening;
        bool talking;
        bool lastClk; // Remember previous clock level
        bool atnLine;
        bool atnHandshakePending;
        bool atnHandshakeJustCleared;
        bool lastSrqLevel;
        bool lastDataLevel;
        bool lastAtnLevel;
        uint8_t shiftReg; // IECBUS accumulated bits
        int bitCount;
        uint8_t iecCmdShiftReg;
        int iecCmdBitCount;
        bool lastClkOutHigh;
        void decodeIECCommand(uint8_t cmd);

        // Data ports
        uint8_t portA;
        uint8_t portB;

        // Data direction
        uint8_t dataDirectionPortA;
        uint8_t dataDirectionPortB;

        // Timers
        uint8_t timerALowByte;
        uint8_t timerAHighByte;
        uint8_t timerBLowByte;
        uint8_t timerBHighByte;
        uint16_t timerA;
        uint16_t timerB;
        uint32_t ticksA;
        uint32_t ticksB;
        uint8_t clkSelA;
        uint8_t clkSelB;

        // TOD registers
        uint8_t todAlarm[4];
        uint8_t todClock[4];
        uint8_t todLatch[4];
        uint32_t todTicks;
        uint32_t todIncrementThreshold;
        bool todLatched;
        bool todAlarmSetMode;
        bool todAlarmTriggered;

        uint32_t pendingTBCNTTicks;
        uint32_t pendingTBCASTicks;

        //TOD Clock and Alarm helpers
        void incrementTODClock(uint32_t& todTicks, uint8_t todClock[], uint32_t todIncrementThreshold);
        void checkTODAlarm(uint8_t todClock[], const uint8_t todAlarm[], bool& todAlarmTriggered, uint8_t& interruptStatus, uint8_t interruptEnable);

        // Timer control
        uint8_t timerAControl;
        uint8_t timerBControl;
        bool timerAPulseFlag;
        uint16_t timerASnap;
        uint16_t timerBSnap;
        bool timerALatched;
        bool timerBLatched;

        // Update Timers helpers
        void updateTimerA(uint32_t cyclesElapsed);
        void updateTimerB(uint32_t cyclesElapsed);

        // Interrupts
        uint8_t interruptEnable;
        uint8_t interruptStatus;
        bool nmiAsserted;

        // Serial
        uint8_t serialDataRegister;
        int outBit;

        // CNT
        bool cntLevel;
        bool lastCNT;

        // Cycles
        uint32_t accumulatedCyclesA;
        uint32_t accumulatedCyclesB;

        // IEC Debugging flag
        bool iecProtocolEnabled;

        // TOD Handling
        void latchTODClock();

        // ML Monitor logging
        bool setLogging;

        // NMI Handling
        void refreshNMI();

        // Timer B
        void tickTimerBOnce();
        void handleTimerBUnderflow();

        // IEC helper
        void recomputeIEC();
};

#endif // CIA2_H
