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
#include "CIA6526.h"
#include "Common/HexFormat.h"
#include "cpu.h"
#include "IRQLine.h"
#include "Joystick.h"
#include "Keyboard.h"
#include "Logging.h"
#include "Memory.h"

class CIA1 : public CIA6526
{
    public:
        CIA1();
        virtual ~CIA1();

        // Pointers
        inline void attachCassetteInstance(Cassette* cass) { this->cass = cass; }
        inline void attachCPUInstance(CPU* cpu) { this->cpu = cpu; }
        inline void attachIRQLineInstance(IRQLine* IRQ) { this->IRQ = IRQ; }
        inline void attachKeyboardInstance(Keyboard* keyb) { this->keyb = keyb; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachMemoryInstance(Memory* mem) { this->mem = mem; }
        inline void attachVicInstance(Vic* vic) { this->vic = vic; }
        void attachJoystickInstance(Joystick* joy);

        // Remove the Joystick(s)
        void detachJoystickInstance(Joystick* joy);

        // STate management
        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        // Reset everything to default
        void reset() override;

        void clearInterrupt(InterruptBit interruptBit);

        // ML Monitor access
        std::string dumpRegisters(const std::string& group) const;

    protected:
        void postTimerUpdates(uint32_t cycleaElapsed) override;

        inline int getCIANumber() const override { return 1; }
        inline const char* getCIAName() const override { return "CIA1"; }

        uint8_t readPortA() override;
        uint8_t readPortB() override;

        void irqLineChanged(bool active) override;

        TraceManager::Stamp makeCIAStamp() const override;

    private:
        // Non-owning pointers
        Cassette* cass;
        CPU* cpu;
        IRQLine* IRQ;
        Joystick* joy1;
        Joystick* joy2;
        Keyboard* keyb;
        Logging* logger;
        Memory* mem;
        Vic* vic;

        // Data ports
        uint8_t portAValue;

        uint8_t rowState; // Keyboard row state (key(s) pressed)
        uint8_t activeRow; // Keyboard active row
        int rowIndex; // Active row index

        // Cassette tape state
        bool prevReadLevel;
        bool cassetteReadLineLevel;
        bool gateWasOpenPrev;
};

#endif // CIA1_H
