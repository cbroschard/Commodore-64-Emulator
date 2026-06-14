// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef CIA6526_H
#define CIA6526_H

#include <cstdint>
#include <string>
#include "Common/BCD.h"
#include "Common/VideoMode.h"
#include "Logging.h"
#include "StateReader.h"
#include "StateWriter.h"
#include "TraceManager.h"

class CIA6526
{
    public:
        CIA6526();
        virtual ~CIA6526();

        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachTraceManagerInstance(TraceManager* traceMgr) { this->traceMgr = traceMgr; }

        virtual void reset();

        void updateTimers(uint32_t cyclesElapsed);

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

        void setMode(VideoMode mode);

        void setCNTLine(bool level);

        // ML Monitor API
        struct CIAIRQSnapshot { uint8_t ier; };
        virtual std::string dumpRegisters(const std::string& group) const;
        inline void setLog(bool enable) { setLogging = enable; }
        inline bool getSetLog() const { return setLogging; }
        void setIERExact(uint8_t mask);
        inline void clearPendingIRQs() { (void)readRegister(0xDC0D); }
        inline void disableAllIRQs() { setIERExact(0); }
        inline uint8_t getIER() const { return interruptEnable & 0x1F; }
        inline uint8_t getIFR() const { return interruptStatus & 0x1F; }
        inline bool irqLineActive() const { return (interruptStatus & interruptEnable & 0x1F) != 0; }
        inline CIAIRQSnapshot snapshotIRQs() const { return CIAIRQSnapshot{getIER()}; }
        inline void restoreIRQs(const CIAIRQSnapshot& snapshot) { setIERExact(snapshot.ier & 0x1F); }

    protected:
        enum InterruptBit : uint8_t
        {
            INTERRUPT_TIMER_A = 0x01,
            INTERRUPT_TIMER_B = 0x02,
            INTERRUPT_TOD_ALARM = 0x04,
            INTERRUPT_SERIAL_SHIFT_REGISTER = 0x08,
            INTERRUPT_FLAG_LINE = 0x10
        };

        inline TraceManager* getTraceManager() const { return traceMgr; }

        void saveBaseState(StateWriter& wrtr) const;
        bool loadBaseState(StateReader& rdr);

        void saveBaseRuntimeState(StateWriter& wrtr) const;
        bool loadBaseRuntimeState(StateReader& rdr);

        virtual void postLoadState();

        virtual void postTimerUpdates(uint32_t cyclesElapsed) = 0;

        inline uint8_t getPortAOutput() const { return static_cast<uint8_t>(portA | ~ddrA); }
        inline uint8_t getPortBOutput() const { return static_cast<uint8_t>(portB | ~ddrB); }

        inline bool isPortAOutput(uint8_t mask) const { return (ddrA & mask) != 0; }
        inline bool isPortBOutput(uint8_t mask) const { return (ddrB & mask) != 0; }

        inline bool getPortALatchBit(uint8_t mask) const { return (portA & mask) != 0; }
        inline bool getPortBLatchBit(uint8_t mask) const { return (portB & mask) != 0; }

        inline uint8_t getPortALatch() const { return portA; }
        inline uint8_t getPortADDR() const { return ddrA; }

        virtual int getCIANumber() const = 0;
        virtual const char* getCIAName() const = 0;

        virtual uint8_t readPortA() = 0;
        virtual uint8_t readPortB() = 0;

        virtual void portAOutputChanged(uint8_t value) {}
        virtual void portBOutputChanged(uint8_t value) {}

        void triggerInterrupt(InterruptBit interruptBit);
        virtual void irqLineChanged(bool active) = 0;

        virtual TraceManager::Stamp makeCIAStamp() const = 0;

    private:
        // Non-owning pointers
        Logging* logger;
        TraceManager* traceMgr;

        enum class TimerBClockSource : uint8_t
        {
            Phi2,
            CNT,
            TimerA,
            TimerAWithCNT
        };

        uint8_t portA;
        uint8_t portB;
        uint8_t ddrA;
        uint8_t ddrB;

        uint16_t timerA;
        uint16_t timerASnap;
        bool timerALatched;

        uint16_t timerB;
        uint16_t timerBSnap;
        bool timerBLatched;

        uint8_t timerALowByte;
        uint8_t timerAHighByte;
        uint8_t timerBLowByte;
        uint8_t timerBHighByte;

        uint8_t timerAControl;
        uint8_t timerBControl;

        uint8_t interruptStatus;
        uint8_t interruptEnable;

        uint8_t serialDataRegister;

        uint8_t todClock[4];
        uint8_t todAlarm[4];
        uint8_t todLatch[4];

        uint32_t todTicks;
        uint32_t todIncrementThreshold;

        bool todLatched;
        bool todAlarmSetMode;
        bool todAlarmTriggered;

        bool cntLevel;
        bool lastCNT;

        // Serial-Shift Register state
        uint8_t shiftReg; // 8-bit shift accumulator
        uint8_t shiftCount; // how many bits we’ve shifted so far

        bool setLogging;

        VideoMode mode_;

        TimerBClockSource getTimerBClockSource() const;

        void updateTimerA(uint32_t cyclesElapsed);
        void updateTimerB(uint32_t cyclesElapsed);
        void handleTimerBCascade();

        void latchTODClock();

        void incrementTODClock(uint32_t& todTicks, uint8_t todClock[], uint32_t todIncrementThreshold);
        void checkTODAlarm(uint8_t todClock[], const uint8_t todAlarm[], bool& todAlarmTriggered, uint8_t& interruptStatus, uint8_t interruptEnable);

        void cntChangedA();
        void cntChangedB();

        void updateIRQLine();
        void clearIFR(InterruptBit interruptBit);
        void refreshMasterBit();
};

#endif // CIA6526_H
