// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef CPU_H
#define CPU_H

// forward declarations
class CIA2;
class StateWriter;

#include <stdexcept>
#include <sstream>
#include <array>
#include <functional>
#include <stdint.h>
#include "common.h"
#include "CPUBus.h"
#include "IRQLine.h"
#include "Logging.h"
#include "StateReader.h"
#include "Vic.h"
#include "Debug/TraceManager.h"

class CPU
{
    public:

        CPU();
        virtual ~CPU();

        // Pointers
        inline void attachMemoryInstance(CPUBus* mem) { this->mem = mem; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachCIA2Instance(CIA2* cia2object) { this->cia2object = cia2object; }
        inline void attachIRQLineInstance(IRQLine* IRQ) { this->IRQ = IRQ; }
        inline void attachTraceManagerInstance(TraceManager* traceMgr) { this->traceMgr = traceMgr; }
        inline void attachVICInstance(Vic* vicII) { this->vicII = vicII; }

        struct CPUState
        {
            uint16_t PC;
            uint8_t A;
            uint8_t X;
            uint8_t Y;
            uint8_t SP;
            uint8_t SR;

            void save(StateWriter& wrtr) const;
            bool load(StateReader& rdr);
        };

        void saveState(StateWriter& wrtr);
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        // Jam handling
        enum class JamMode : uint8_t
        {
            Halt, // Stop CPU completely, strict mode
            FreezePC, // PC stays on JAM
            NopCompat // Treat as Noop
        };
        void setJamMode(JamMode mode);
        JamMode getJamMode() const;

        // Reset processor to defaults
        void reset();

        // Setter for Video mode NTSC/PAL
        void setMode(VideoMode mode);

        // Tick functionality
        void tick();
        uint32_t getElapsedCycles();

        // Cycles by opcode
        inline static constexpr std::array<uint8_t, 256> CYCLE_COUNTS =
        {{
            // 0x00 - 0x0F
            7, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6,
            // 0x10 - 0x1F
            2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
            // 0x20 - 0x2F
            6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,
            // 0x30 - 0x3F
            2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
            // 0x40 - 0x4F
            6, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6,
            // 0x50 - 0x5F
            2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
            // 0x60 - 0x6F
            6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6,
            // 0x70 - 0x7F
            2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
            // 0x80 - 0x8F
            2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
            // 0x90 - 0x9F
            2, 6, 2, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
            // 0xA0 - 0xAF
            2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
            // 0xB0 - 0xBF
            2, 5, 2, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
            // 0xC0 - 0xCF
            2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
            // 0xD0 - 0xDF
            2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
            // 0xE0 - 0xEF
            2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
            // 0xF0 - 0xFF
            2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7
        }};

        //CPU Flags
        enum flags : uint8_t
        {
            N = (1<<7), // Negative
            V = (1<<6), // Overflow
            U = (1<<5), // Unused
            B = (1<<4), // Break
            D = (1<<3), // Decimal
            I = (1<<2), // Disable interrupts
            Z = (1<<1), // Zero
            C = (1<<0), // Carry bit
        };

        // Flag operatons
        inline bool getFlag(flags flag) const { return (SR & flag) != 0; }
        void setFlag(flags flag, bool sc);

        // Access for IRQ handling
        inline void requestNMI() { nmiPending = true; }
        void setNMILine(bool asserted);
        void handleIRQ();
        void handleNMI();

        // 1541/1571 SO
        void setSO(bool level);
        void pulseSO();

        // BA flag hold
        inline void setBAHold(bool flag) { baHold = flag; }

        // Monitor helpers
        inline uint8_t getSR() const { return SR; }
        inline void setSEI() { setFlag(I, true); }
        inline void setCLI() { setFlag(I, false); irqSuppressOne = true; }

        // ML Monitor
        CPUState getState() const;
        inline uint16_t getPC() const { return PC; }
        inline void setPC(uint16_t value) { PC = value; }
        inline uint8_t getA() const { return A; }
        inline void setA(uint8_t value) { A = value; }
        inline uint8_t getX() const { return X; }
        inline void setX(uint8_t value) { X = value; }
        inline uint8_t getY() const { return Y; }
        inline void setY(uint8_t value) { Y = value; }
        inline uint8_t getSP() const { return SP; }
        uint8_t debugRead(uint16_t address) const;

        // ML Monitor logging
        inline void setLog(bool enable) { setLogging = enable; }
        inline uint32_t getTotalCycles() const { return totalCycles; }

    protected:

    private:

        // non-owning pointers
        CIA2* cia2object;
        IRQLine* IRQ;
        Logging* logger;
        CPUBus* mem;
        TraceManager* traceMgr;
        Vic* vicII;

        // NMI scheduling
        bool nmiPending;
        bool nmiLine;

        // IRQ delay
        bool irqSuppressOne;

        // Jam handling
        JamMode jamMode;
        bool halted;

        // Process commands
        uint8_t fetch();
        void decodeAndExecute(uint8_t opcode);

        // Reset vector
        uint16_t resetVectorLow;
        uint16_t resetVectorHigh;
        uint16_t resetAddress;

        // Clock Cycle timing
        uint32_t cycles;
        uint32_t totalCycles;
        uint32_t elapsedCycles;
        uint32_t lastCycleCount;
        inline bool isPhi2Low() { return (totalCycles & 1) == 0; } // Check if we can temp allow I/O access while CHAR ROM is enabled
        inline bool frameComplete() { return totalCycles >= CYCLES_PER_FRAME; }

        // CPU Registers
        uint8_t A;
        uint8_t X;
        uint8_t Y;
        uint8_t SP;
        uint8_t SR;
        uint16_t PC;

        // ML Monitor logging
        bool setLogging;

        // SO handling
        bool soLevel;

        // Video mode
        uint32_t CYCLES_PER_FRAME;
        VideoMode mode_;

        // IRQ handling
        uint8_t activeSource;
        void executeIRQ();
        void executeNMI();

        // OpCode Table to point to all functions
        std::array<std::function<void()>, 256> opcodeTable;
        void initializeOpcodeTable();

        bool baHold;

        //Helper functions
        uint8_t readABS();
        uint8_t readABSX();
        uint8_t readABSY();
        uint8_t readImmediate();
        uint8_t readIndirectX();
        uint8_t readIndirectY();
        uint8_t readZP();
        uint8_t readZPX();
        uint8_t readZPY();

        uint16_t absAddress();
        uint16_t absXAddress();
        uint16_t absYAddress();
        uint16_t indirectXAddress();
        uint16_t indirectYAddress();
        uint16_t zpAddress();
        uint16_t zpXAddress();
        uint16_t zpYAddress();

        // Struct to track page boundary for read only opcodes
        struct ReadByte
        {
            uint8_t value;
            bool crossed;
        };

        // RMW helper
        void rmwWrite(uint16_t address, uint8_t oldValue, uint8_t newValue);

        // Page boundary cross helpers
        inline void addPageCrossIf(bool crossed) { if (crossed) cycles++; }
        ReadByte readABSXAddressBoundary();
        ReadByte readABSYAddressBoundary();
        ReadByte readIndirectYAddressBoundary();

        // Dummy read helpers
        void dummyReadWrongPageABSX(uint16_t address);
        void dummyReadWrongPageABSY(uint16_t address);
        void dummyReadWrongPageINDY(uint16_t address);

        //Stack functions
        void push(uint8_t value);
        uint8_t pop();

        //Bitwise operators
        void AND(uint8_t opcode);
        void ASL(uint8_t opcode);
        void EOR(uint8_t opcode);
        void LSR(uint8_t opcode);
        void ORA(uint8_t opcode);
        void ROL(uint8_t opcode);
        void ROR(uint8_t opcode);

        //Branch instructions
        void BCC();
        void BEQ();
        void BMI();
        void BNE();
        void BPL();
        void BCS();
        void BVC();
        void BVS();

        //Compare instructions
        void BIT(uint8_t opcode);
        void CMP(uint8_t opcode);
        void CPX (uint8_t opcode);
        void CPY(uint8_t opcode);

        //Flag Instructions
        inline void CLC() { SR &= ~C; }
        inline void CLD() { SR &= ~D; }
        inline void CLI() { SR &= ~I; irqSuppressOne = true; }
        inline void CLV() { SR &= ~V; }
        inline void SEC() { setFlag(C,true); }
        inline void SEI() { setFlag(I,true); }
        inline void SED() { setFlag(D,true); }

        //Jump Instructions
        void JMP(uint8_t opcode);
        void JSR();
        void RTI();
        void RTS();

        //Math instructions
        void ADC(uint8_t opcode);
        void SBC(uint8_t opcode);

        //Memory instructions
        void DEC(uint8_t opcode);
        void INC(uint8_t opcode);
        void LDA(uint8_t opcode);
        void LDX(uint8_t opcode);
        void LDY(uint8_t opcode);
        void SAX(uint8_t opcode);
        void STA(uint8_t opcode);
        void STX(uint8_t opcode);
        void STY(uint8_t opcode);

        //Register Instructions
        void DEX();
        void DEY();
        void INX();
        void INY();
        void TAX();
        void TAY();
        void TSX();
        void TXA();
        inline void TXS() { SP = X; }
        void TYA();

        //Stack instructions
        void PHA();
        void PHP();
        void PLA();
        void PLP();

        //Illegal instructions
        void AAC();
        void AHX(uint8_t opcode);
        void ALR();
        void ARR();
        void AXS();
        void DCP(uint8_t opcode);
        void ISC(uint8_t opcode);
        void LAS();
        void LAX(uint8_t opcode);
        void RLA(uint8_t opcode);
        void RRA(uint8_t opcode);
        void SHX();
        void SHY();
        void SLO(uint8_t opcode);
        void SRE(uint8_t opcode);
        void TAS();
        void XAA();

        //Other instructions
        void BRK();
        void JAM();
        void NOP(uint8_t opcode);
};

#endif // CPU_H
