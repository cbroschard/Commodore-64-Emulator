// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef CIA1_H
#define CIA1_H

//Forward declarations
class Cassette;
class Keyboard;
class TraceManager;
class Vic;

#include <bitset>
#include <cstdint>
#include <iostream>
#include "Cassette.h"
#include "common.h"
#include "cpu.h"
#include "IRQLine.h"
#include "Joystick.h"
#include "Keyboard.h"
#include "Logging.h"
#include "Memory.h"
#include "StateReader.h"
#include "StateWriter.h"


class CIA1
{
    public:
        CIA1();
        virtual ~CIA1();

        // Pointers
        inline void attachCassetteInstance(Cassette* cass) { this->cass = cass; }
        inline void attachCPUInstance(CPU* processor) { this->processor = processor; }
        inline void attachIRQLineInstance(IRQLine* IRQ) { this->IRQ = IRQ; }
        inline void attachKeyboardInstance(Keyboard* keyb) { this->keyb = keyb; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachMemoryInstance(Memory* mem) { this->mem = mem; }
        inline void attachTraceManagerInstance(TraceManager* traceMgr) { this->traceMgr = traceMgr; }
        inline void attachVicInstance(Vic* vicII) { this->vicII = vicII; }
        void attachJoystickInstance(Joystick* joy);

        // Remove the Joystick(s)
        void detachJoystickInstance(Joystick* joy);

        // Setter for NTSC/PAL
        void setMode(VideoMode mode);

        // STate management
        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        // Reset everything to default
        void reset();

        // Register methods
        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

        // Main update timers method
        void updateTimers(uint32_t cyclesElapsed);

        // CNT handling
        void setCNTLine(bool level);

        //Interrupt handling
        enum InterruptBit : uint8_t
        {
            INTERRUPT_TIMER_A = 0x01,
            INTERRUPT_TIMER_B = 0x02,
            INTERRUPT_TOD_ALARM = 0x04,
            INTERRUPT_SERIAL_SHIFT_REGISTER = 0x08,
            INTERRUPT_FLAG_LINE = 0x10
        };

        void clearInterrupt(InterruptBit interruptBit);

        // ML Monitor access
        struct CIA1IRQSnapshot { uint8_t ier; };
        std::string dumpRegisters(const std::string& group) const;
        inline void setLog(bool enable) { setLogging = enable; }
        void setIERExact(uint8_t mask);
        inline void clearPendingIRQs() { (void)readRegister(0xDC0D); }
        inline void disableAllIRQs() { setIERExact(0); }
        inline uint8_t getIER() const { return interruptEnable & 0x1F; }
        inline uint8_t getIFR() const { return interruptStatus & 0x1F; }
        inline bool irqLineActive() const { return (interruptStatus & interruptEnable & 0x1F) != 0; }
        inline CIA1IRQSnapshot snapshotIRQs() const { return CIA1IRQSnapshot{getIER()}; }
        inline void restoreIRQs(const CIA1IRQSnapshot& snapshot) { setIERExact(snapshot.ier & 0x1F); }

    protected:

    private:

        // Non-owning pointers
        Cassette* cass;
        CPU* processor;
        IRQLine* IRQ;
        Joystick* joy1;
        Joystick* joy2;
        Keyboard* keyb;
        Logging* logger;
        Memory* mem;
        TraceManager* traceMgr;
        Vic* vicII;

        // Video
        VideoMode mode_;

        // Data ports
        uint8_t portAValue;
        uint8_t portA;
        uint8_t portB;

        uint8_t rowState; // Keyboard row state (key(s) pressed)
        uint8_t activeRow; // Keyboard active row
        int rowIndex; // Active row index

        // Data direction
        uint8_t dataDirectionPortA;
        uint8_t dataDirectionPortB;

        // Serial-Shift Register state
        uint8_t shiftReg; // 8-bit shift accumulator
        uint8_t shiftCount; // how many bits we’ve shifted so far

        // Timers
        uint16_t timerA;
        uint8_t timerALowByte;
        uint8_t timerAHighByte;
        uint16_t timerB;
        uint8_t timerBLowByte;
        uint8_t timerBHighByte;
        uint32_t todTicks;

        // Cassette tape state
        bool prevReadLevel;
        bool cassetteReadLineLevel;
        bool gateWasOpenPrev;

        // Timer A & B latch
        uint16_t timerASnap;
        bool timerALatched;
        uint16_t timerBSnap;
        bool timerBLatched;

        // TOD Increment Threshold
        uint32_t todIncrementThreshold;

        // ML Monitor logging
        bool setLogging;

        //TOD Clock and Alarm helpers
        void incrementTODClock(uint32_t& todTicks, uint8_t todClock[], uint32_t todIncrementThreshold);
        void checkTODAlarm(uint8_t todClock[], const uint8_t todAlarm[], bool& todAlarmTriggered, uint8_t& interruptStatus, uint8_t interruptEnable);

        // Timer control
        uint8_t timerAControl;
        uint8_t timerBControl;

        // Update Timers helpers
        void updateTimerA(uint32_t cyclesElapsed);
        void updateTimerB(uint32_t cyclesElapsed);
        void handleTimerBCascade();

        // TOD registers
        uint8_t todAlarm[4];
        uint8_t todClock[4];
        uint8_t todLatch[4];  // Latched TOD values
        bool todLatched; // Indicates if TOD values are latched
        bool todAlarmSetMode;
        bool todAlarmTriggered;

        // Serial
        uint8_t serialDataRegister;

        // Interrupt handling
        uint8_t interruptStatus;
        uint8_t interruptEnable;

        // Handle CNT mode
        bool cntLevel;
        bool lastCNT;
        void cntChangedA();
        void cntChangedB();

        void latchTODClock();

        // IFR Master bit handling
        void triggerInterrupt(InterruptBit interruptBit);
        void updateIRQLine();
        void clearIFR(InterruptBit interruptBit);
        void refreshMasterBit();

        // Mode
        enum InputMode : uint8_t
        {
            modeProcessor, // Direct processor polling
            modeCNT, // CNT signal-driven
            modeTimerA, // Timer A driven
            modeTimerACNT // Combined Timer A and CNT
        };

        InputMode inputMode;
};

#endif // CIA1_H
