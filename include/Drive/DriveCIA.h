// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DRIVECIA_H
#define DRIVECIA_H

#include <cstdint>
#include "Drive/Drive.h"
#include "Drive/DriveChips.h"

// Forward declaration for struct
class DriveCIA;

enum class DriveCIAType { D1571, D1581 };

struct DriveCIAWiring
{
    void (*samplePortAPins)(DriveCIA& cia, Drive& drive, uint8_t& outPinsA) = nullptr;
    void (*samplePortBPins)(DriveCIA& cia, Drive& drive, uint8_t& outPinsB) = nullptr;

    void (*applyPortAOutputs)(DriveCIA& cia, Drive& drive, uint8_t pra, uint8_t ddra) = nullptr;
    void (*applyPortBOutputs)(DriveCIA& cia, Drive& drive, uint8_t prb, uint8_t ddrb) = nullptr;
};

class DriveCIA : public DriveCIABase
{
    public:
        DriveCIA();
        virtual ~DriveCIA();

        enum CIA_PRA : uint8_t
        {
            PRA_SIDE    = 1u << 0, // 0 = side 0, 1 = side 1
            PRA_DRVRDY  = 1u << 1, // 1 = drive ready (input)
            PRA_MOTOR   = 1u << 2, // 0 = on, 1 = off
            PRA_DEVSW2  = 1u << 3, // device switch 2 (right)
            PRA_DEVSW1  = 1u << 4, // device switch 1 (left)
            PRA_ERRLED  = 1u << 5, // red LED
            PRA_ACTLED  = 1u << 6, // green LED
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

        inline void attachPeripheralInstance(Peripheral* parentPeripheral) { this->parentPeripheral = parentPeripheral; }

        inline void enableAutoAtnAck(bool enabled) { autoAtnAckEnabled = enabled; }
        void notifyAtnInput(bool atnLow);

        void reset();
        void tick(uint32_t cycles);

        // API access
        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);
        void setFlagLine(bool level);
        inline void linesChanged() { updatePinsFromBus(); }

        inline bool checkIRQActive() const { return (interruptStatus & registers.interruptEnable & 0x7F) != 0; }

        void setWiring(const DriveCIAWiring* w) { wiring = w; }

        // ML Monitor
        inline ciaRegsView getRegsView() const override
        {
            return
                {
                    registers.portA,
                    registers.portB,
                    registers.ddrA,
                    registers.ddrB,
                    registers.timerALowByte,
                    registers.timerAHighByte,
                    registers.timerBLowByte,
                    registers.timerBHighByte,
                    registers.tod10th,
                    registers.todSeconds,
                    registers.todMinutes,
                    registers.todHours,
                    registers.serialData,
                    registers.interruptEnable,
                    registers.controlRegisterA,
                    registers.controlRegisterB,
                    timerACounter,
                    timerALatch,
                    timerBCounter,
                    timerBLatch
                };
        }

        void setIECInputs(bool atnLow, bool clkLow, bool dataLow);
        void primeAtnLevel(bool atnLow);

    protected:

    private:

        const DriveCIAWiring* wiring = nullptr;

        // Non-owning pointers
        Peripheral* parentPeripheral;

        // CRA ($0E)
        static constexpr uint8_t CRA_START    = 1 << 0;
        static constexpr uint8_t CRA_RUNMODE  = 1 << 3; // 1=one-shot
        static constexpr uint8_t CRA_LOAD     = 1 << 4; // strobe
        static constexpr uint8_t CRA_INMODE   = 1 << 5; // 1=count CNT rising edges

        // CRB ($0F)
        static constexpr uint8_t CRB_START    = 1 << 0;
        static constexpr uint8_t CRB_RUNMODE  = 1 << 3;
        static constexpr uint8_t CRB_LOAD     = 1 << 4;
        static constexpr uint8_t CRB_INMODE_MASK = (3 << 5); // bits 5-6
        static constexpr uint8_t CRB_INMODE_PHI2  = (0 << 5);
        static constexpr uint8_t CRB_INMODE_CNT   = (1 << 5);
        static constexpr uint8_t CRB_INMODE_TA    = (2 << 5); // Timer A underflow
        static constexpr uint8_t CRB_INMODE_TA_CNT= (3 << 5); // TA underflow while CNT high

        struct ciaRegs
        {
            // Ports and data direction
            uint8_t portA;
            uint8_t portB;
            uint8_t ddrA;
            uint8_t ddrB;

            // Timers
            uint8_t timerALowByte;
            uint8_t timerAHighByte;
            uint8_t timerBLowByte;
            uint8_t timerBHighByte;
            uint8_t tod10th;
            uint8_t todSeconds;
            uint8_t todMinutes;
            uint8_t todHours;

            // Serial/Interrupts/Control
            uint8_t serialData;
            uint8_t interruptEnable;
            uint8_t controlRegisterA;
            uint8_t controlRegisterB;
        } registers;

        //Interrupt handling
        enum InterruptBit : uint8_t
        {
            INTERRUPT_TIMER_A                   = 0x01,
            INTERRUPT_TIMER_B                   = 0x02,
            INTERRUPT_TOD_ALARM                 = 0x04,
            INTERRUPT_SERIAL_SHIFT_REGISTER     = 0x08,
            INTERRUPT_FLAG_LINE                 = 0x10
        };

        // Port pins
        uint8_t portAPins;
        uint8_t portBPins;

        bool cntLevel;
        bool lastCntLevel;

        // Flag line
        bool flagLine;
        bool lastFlagLine;

        // Timers
        uint16_t timerACounter;
        uint16_t timerALatch;
        uint16_t timerBCounter;
        uint16_t timerBLatch;
        bool timerARunning;
        bool timerBRunning;

        // TOD Alarm
        uint8_t todAlarm10th;
        uint8_t todAlarmSeconds;
        uint8_t todAlarmMinutes;
        uint8_t todAlarmHours;
        uint8_t todAlarmSetMode;
        bool    todAlarmTriggered;

        // Interrupt
        uint8_t interruptStatus;
        void triggerInterrupt(InterruptBit bit);

        // Handshake
        static constexpr uint16_t MIN_ACK_HOLD = 300; // cycles
        bool lastAtnLow;
        bool extDataLow;
        bool autoAtnAckEnabled;
        bool ackArmed;
        bool lastClkInLowForAck;
        uint16_t atnAckHoldCycles;
        bool atnAckArmedWhileClkLow;
        bool atnAckSawClkHigh;
        bool atnAckSawClkLow;

        // IEC input levels as seen from the host bus (C64 side)
        bool iecAtnInLow;
        bool iecClkInLow;
        bool iecDataInLow;

        // Forces PRB input bits to match the stored IEC inputs
        void applyIECInputsToPortBPins();

        // Port updates
        void updatePinsFromBus();
        void applyIECOutputs();
        void applyPortOutputs();
};

#endif // DRIVECIA_H
