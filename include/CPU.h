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

#include <array>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <stdint.h>
#include "Common/BCD.h"
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
        inline void attachCIA2Instance(CIA2* cia2) { this->cia2 = cia2; }
        inline void attachIRQLineInstance(IRQLine* IRQ) { this->IRQ = IRQ; }
        inline void attachTraceManagerInstance(TraceManager* traceMgr) { this->traceMgr = traceMgr; }
        inline void attachVICInstance(Vic* vic) { this->vic = vic; }

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

        // State Management
        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);
        void saveStatePayload(StateWriter& wrtr) const;
        bool loadStatePayload(StateReader& rdr);
        void saveStateExtendedPayload(StateWriter& wrtr) const;
        bool loadStateExtendedPayload(const StateReader::Chunk& parentChunk, StateReader& rdr);

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
        void pulseNMI();

        // 1541/1571 SO
        void setSO(bool level);
        void pulseSO();

        // Bus arbitration
        inline void setRDY(bool high) { rdyLine = high; }
        inline void setAEC(bool high) { aecLine = high; }

        // Monitor helpers
        inline uint8_t getSR() const { return SR; }
        inline void setSEI() { setFlag(I, true); }
        inline void setCLI() { setFlag(I, false); irqSuppressOne = true; }

        // ML Monitor
        enum class CPUAddressDebugMode : uint8_t
        {
            None,
            IndirectX,
            IndirectY,
            IndirectYBoundary
        };

        enum class CpuBusCycleType : uint8_t
        {
            None,

            OpcodeFetch,

            Read,
            Write,

            DummyRead,
            DummyWrite,

            StackRead,
            StackWrite
        };

        struct CpuBusCycle
        {
            CpuBusCycleType type = CpuBusCycleType::None;
            uint16_t address = 0;
            uint8_t value = 0;

            bool isReadLike() const
            {
                return type == CpuBusCycleType::OpcodeFetch ||
                       type == CpuBusCycleType::Read ||
                       type == CpuBusCycleType::DummyRead ||
                       type == CpuBusCycleType::StackRead;
            }

            bool isWriteLike() const
            {
                return type == CpuBusCycleType::Write ||
                       type == CpuBusCycleType::DummyWrite ||
                       type == CpuBusCycleType::StackWrite;
            }

            bool usesExternalBus() const
            {
                return isReadLike() || isWriteLike();
            }
        };

        struct CPUAddressDebugState
        {
            bool valid = false;

            CPUAddressDebugMode mode = CPUAddressDebugMode::None;

            uint16_t operandPC = 0;
            uint8_t zpOperand = 0;

            uint8_t indexValue = 0;
            uint8_t indexedZP = 0;

            uint8_t pointerLowAddr = 0;
            uint8_t pointerHighAddr = 0;

            uint8_t pointerLowValue = 0;
            uint8_t pointerHighValue = 0;

            uint16_t baseAddress = 0;
            uint16_t effectiveAddress = 0;

            bool pageCrossed = false;
            bool dummyReadUsed = false;
            uint16_t dummyReadAddress = 0;

            uint8_t valueRead = 0;

            uint32_t totalCycles = 0;
        };

        enum class InterruptEntryType : uint8_t
        {
            None,
            IRQ,
            NMI,
            BRK
        };

        struct CPUInterruptEntryDebugState
        {
            InterruptEntryType type = InterruptEntryType::None;

            uint8_t spBefore        = 0;
            uint8_t spAfter         = 0;

            uint16_t acceptedAtPC   = 0;
            uint16_t pushedReturnPC = 0;
            uint8_t pushedSR        = 0;

            uint16_t vectorAddress  = 0;
            uint16_t vectorTarget   = 0;

            uint32_t totalCycles    = 0;
        };

        struct CPUIrqDebugState
        {
            uint16_t pc = 0;
            uint8_t sr = 0;

            uint16_t lastOpcodePC = 0;
            uint8_t lastOpcode = 0xEA;

            bool iFlag = false;
            bool irqLineActive = false;

            bool nmiPending = false;
            bool nmiLine = false;
            bool irqSuppressOne = false;

            bool rdyLine = true;
            bool aecLine = true;
            bool soLevel = true;

            uint32_t cyclesRemaining = 0;
            uint32_t totalCycles = 0;
        };

        struct CPUBranchDebugState
        {
            bool valid = false;

            uint16_t opcodePC = 0;
            uint8_t opcode = 0;
            const char* mnemonic = "";

            bool condition = false;
            bool taken = false;

            int8_t offset = 0;
            uint16_t operandPC = 0;
            uint16_t oldPC = 0;
            uint16_t newPC = 0;

            bool pageCrossed = false;
            uint16_t takenDummyRead = 0;
            uint16_t pageCrossDummyRead = 0;

            uint8_t extraCycles = 0;
            uint32_t totalCycles = 0;
        };

        struct CPUCycleDebugState
        {
            uint16_t lastOpcodePC = 0;
            uint8_t lastOpcode = 0xEA;

            uint32_t totalCycles = 0;
            uint32_t cyclesRemaining = 0;
            uint32_t cyclesPerFrame = 0;
            uint32_t frameCycle = 0;

            bool betweenInstructions = false;
            bool rdyLine = true;
            bool aecLine = true;
            bool halted = false;

            VideoMode mode = VideoMode::NTSC;

            int raster = 0;
            int dot = 0;

            bool busCycleActive = false;
            CpuBusCycleType busCycleType = CpuBusCycleType::None;
            uint16_t busAddress = 0;
            uint8_t busValue = 0;
        };

        struct CPUJMPDebugState
        {
            bool valid = false;

            uint16_t jmpOpcodePC = 0;
            uint8_t opcode = 0;

            bool indirect = false;

            uint16_t operandAddress = 0;
            uint16_t pointerAddress = 0;

            uint16_t lowReadAddress = 0;
            uint16_t highReadAddress = 0;

            uint8_t lowByte = 0;
            uint8_t highByte = 0;

            bool indirectPageBug = false;

            uint16_t finalPC = 0;

            uint32_t totalCycles = 0;
        };

        struct CPUJSRDebugState
        {
            bool valid = false;

            uint16_t jsrOpcodePC = 0;
            uint16_t targetPC = 0;
            uint16_t pushedReturn = 0;

            uint8_t pushedHigh = 0;
            uint8_t pushedLow = 0;

            uint8_t spBefore = 0;
            uint8_t spAfter = 0;

            uint32_t totalCycles = 0;
        };

        struct CPURTSDebugState
        {
            bool valid = false;

            uint16_t rtsOpcodePC = 0;
            uint16_t pulledReturn = 0;
            uint16_t finalPC = 0;

            uint8_t pulledLow = 0;
            uint8_t pulledHigh = 0;

            uint8_t spBefore = 0;
            uint8_t spAfter = 0;

            uint32_t totalCycles = 0;
        };

        struct CPURTIDebugState
        {
            bool valid = false;

            uint16_t rtiOpcodePC = 0;

            uint8_t pulledSR = 0;
            uint8_t finalSR = 0;

            uint8_t pulledPCL = 0;
            uint8_t pulledPCH = 0;
            uint16_t returnPC = 0;

            uint8_t spBefore = 0;
            uint8_t spAfter = 0;

            bool oldI = false;
            bool newI = false;
            bool irqSuppressSet = false;

            uint32_t totalCycles = 0;
        };

        struct CPUPHADebugState
        {
            bool valid = false;

            uint16_t phaOpcodePC = 0;

            uint8_t pushedA = 0;

            uint8_t spBefore = 0;
            uint8_t spAfter = 0;

            uint32_t totalCycles = 0;
        };

        struct CPUPHPDebugState
        {
            bool valid = false;

            uint16_t phpOpcodePC = 0;

            uint8_t internalSR = 0;
            uint8_t pushedSR = 0;

            uint8_t spBefore = 0;
            uint8_t spAfter = 0;

            uint32_t totalCycles = 0;
        };

        struct CPUPLADebugState
        {
            bool valid = false;

            uint16_t plaOpcodePC = 0;

            uint8_t pulledA = 0;
            uint8_t finalA = 0;

            bool zFlag = false;
            bool nFlag = false;

            uint8_t spBefore = 0;
            uint8_t spAfter = 0;

            uint32_t totalCycles = 0;
        };

        struct CPUPLPDebugState
        {
            bool valid = false;

            uint16_t plpOpcodePC = 0;

            uint8_t pulledSR = 0;
            uint8_t finalSR = 0;

            uint8_t spBefore = 0;
            uint8_t spAfter = 0;

            bool oldI = false;
            bool newI = false;
            bool irqSuppressSet = false;

            uint32_t totalCycles = 0;
        };

        inline CPUAddressDebugState getLastAddressDebugState() const { return lastAddressDebug; }
        inline CPUInterruptEntryDebugState getLastInterruptEntryDebugState() const { return lastInterruptEntry; }
        inline CPUBranchDebugState getLastBranchDebugState() const { return lastBranch; }
        inline CPUJMPDebugState getLastJMPDebugState() const { return lastJMP; }
        inline CPUJSRDebugState getLastJSRDebugState() const { return lastJSR; }
        inline CPURTSDebugState getLastRTSDebugState() const { return lastRTS; }
        inline CPUPHADebugState getLastPHADebugState() const { return lastPHA; }
        inline CPUPHPDebugState getLastPHPDebugState() const { return lastPHP; }
        inline CPUPLADebugState getLastPLADebugState() const { return lastPLA; }
        inline CPUPLPDebugState getLastPLPDebugState() const { return lastPLP; }
        inline CPURTIDebugState getLastRTIDebugState() const { return lastRTI; }

        CPUIrqDebugState getIrqDebugState() const;
        CPUCycleDebugState getCycleDebugState() const;
        CPUState getState() const;

        inline bool isAtInstructionBoundary() const { return cycles <= 0; }
        inline void forceInstructionBoundaryForMonitor() { cycles = 0; }
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

        // Bus Arbitration
        inline void setVICBusArbitrationEnabled(bool enabled) { vicBusArbitrationEnabled = enabled; }
        inline bool isVICBusArbitrationEnabled() const { return vicBusArbitrationEnabled; }
        inline void setUseMicroOpsForTest(bool enabled) { useMicroOpsForTest = enabled; }
        inline bool getUseMicroOpsForTest() const { return useMicroOpsForTest; }

        // ML Monitor
        inline void setLog(bool enable) { setLogging = enable; }
        inline uint32_t getTotalCycles() const { return totalCycles; }

    protected:

    private:

        // non-owning pointers
        CIA2* cia2;
        IRQLine* IRQ;
        Logging* logger;
        CPUBus* mem;
        TraceManager* traceMgr;
        Vic* vic;

        CpuBusCycle currentBusCycle {};
        bool busCycleActive;

        enum class CpuMicroOpKind : uint8_t
        {
            None,

            OpcodeFetch,
            OperandRead,
            OperandReadToAddress,
            OperandReadHighToAddress,
            MemoryRead,
            MemoryWrite,
            MemoryRMWWrite,

            OperandReadToZP,
            ApplyIndirectXIndex,
            ReadPointerLow,
            ReadPointerHigh,
            BuildPointerAddress,
            ApplyIndirectYIndex,
            ConditionalPageCrossDummyRead,

            ReadJmpIndirectLow,
            ReadJmpIndirectHigh,

            OperandReadToBranchOffset,
            EvaluateBranchCondition,
            BranchTakenDummyRead,
            ApplyBranchOffset,
            BranchPageCrossDummyRead,

            DummyRead,
            DummyWrite,

            StackRead,
            StackWrite,

            Internal,

            ApplyZeroPageIndex,
            ApplyAbsoluteIndex
        };

        enum class CpuMicroAction : uint8_t
        {
            None,

            FinishNOP,

            LoadAFromTemp,
            LoadXFromTemp,
            LoadYFromTemp,

            StoreA,
            StoreX,
            StoreY,

            TransferAToX,
            TransferAToY,
            TransferXToA,
            TransferYToA,
            TransferSPToX,
            TransferXToSP,

            IncrementX,
            IncrementY,
            IncrementTemp,
            DecrementTemp,
            DecrementX,
            DecrementY,

            ClearCarry,
            SetCarry,
            ClearInterruptDisable,
            SetInterruptDisable,
            ClearDecimal,
            SetDecimal,
            ClearOverflow,

            OrAWithTemp,
            AndAWithTemp,
            EorAWithTemp,

            CompareAWithTemp,
            CompareXWithTemp,
            CompareYWithTemp,

            AddWithCarryFromTemp,
            SubtractWithCarryFromTemp,

            BitTestWithTemp,

            ShiftLeftA,
            RotateLeftA,
            ShiftRightA,
            RotateRightA,

            ShiftLeftTemp,
            RotateLeftTemp,
            ShiftRightTemp,
            RotateRightTemp,

            JumpToMicroAddress,

            BranchIfCarryClear,
            BranchIfCarrySet,
            BranchIfZeroSet,
            BranchIfMinusSet,
            BranchIfZeroClear,
            BranchIfMinusClear,
            BranchIfOverflowClear,
            BranchIfOverflowSet,

            PushA,
            PushProcessorStatus,
            PullA,
            PullProcessorStatus,

            PrepareJSRReturnAddress,
            PushJSRReturnHigh,
            PushJSRReturnLow,
            PullRTSLow,
            PullRTSHigh,
            FinishRTS,

            PrepareBRKReturnAddress,
            PushBRKReturnHigh,
            PushBRKReturnLow,
            PushBRKStatus,
            ReadBRKVectorLow,
            ReadBRKVectorHigh,
            FinishBRK,

            PullRTIStatus,
            PullRTIPCLow,
            PullRTIPCHigh,
            FinishRTI
        };

        enum class CpuIndexReg : uint8_t
        {
            None,
            X,
            Y
        };

        struct CpuMicroOp
        {
            CpuMicroOpKind kind = CpuMicroOpKind::None;
            CpuBusCycleType busType = CpuBusCycleType::None;

            uint16_t address = 0;
            uint8_t value = 0;

            bool useMicroAddress = false;
            CpuIndexReg index = CpuIndexReg::None;

            CpuMicroAction action = CpuMicroAction::None;
        };

        std::array<CpuMicroOp, 32> microOps {};
        uint8_t microOpCount;
        uint8_t microOpIndex;

        bool microInstructionActive;
        bool executingMicroOp;

        uint8_t activeOpcode;
        uint16_t activeOpcodePC;

        bool useMicroOpsForTest;
        uint16_t microAddress;
        uint16_t microBaseAddress;
        bool microPageCrossed;
        uint8_t microTemp;
        uint8_t microZP;
        uint8_t microPtrLow;
        uint8_t microPtrHigh;
        uint16_t microPointerAddress;
        uint8_t microJmpLow;
        uint8_t microJmpHigh;
        int8_t microBranchOffset;
        bool microBranchTaken;
        uint16_t microOldPC;
        uint16_t microReturnAddress;
        uint8_t microReturnLow;
        uint8_t microReturnHigh;
        uint8_t microStatus;
        uint8_t microVectorLow;
        uint8_t microVectorHigh;

        // Debug
        CPUAddressDebugState lastAddressDebug;
        CPUBranchDebugState lastBranch;
        CPUInterruptEntryDebugState lastInterruptEntry;
        CPUJMPDebugState lastJMP;
        CPUJSRDebugState lastJSR;
        CPURTSDebugState lastRTS;
        CPURTIDebugState lastRTI;
        CPUPHADebugState lastPHA;
        CPUPHPDebugState lastPHP;
        CPUPLADebugState lastPLA;
        CPUPLPDebugState lastPLP;

        // NMI scheduling
        bool nmiPending;
        bool nmiLine;

        // IRQ delay
        bool irqSuppressOne;

        // Jam handling
        JamMode jamMode;
        bool halted;

        bool pendingOpcodeFetch;
        uint16_t pendingOpcodeAddress;

        // Process commands
        uint8_t fetchOpcode();
        uint8_t fetchOperand();
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

        // Last executed instruction diagnostics
        uint16_t lastOpcodePC;
        uint8_t lastOpcode;

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


        uint8_t cpuRead(uint16_t address, CpuBusCycleType type);
        void cpuWrite(uint16_t address, uint8_t value, CpuBusCycleType type);

        // OpCode Table to point to all functions
        std::array<std::function<void()>, 256> opcodeTable;
        void initializeOpcodeTable();

        bool rdyLine;
        bool aecLine;
        bool vicBusArbitrationEnabled;

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

        void branchIf(bool condition, const char* mnemonic, uint8_t opcode);

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

        // Bus Arbitration
        bool shouldRDYStallForCurrentBusCycle() const;
        bool shouldAECBlockCurrentBusCycle() const;
        bool isReadLikeBusCycle(CpuBusCycleType type) const;
        bool isWriteLikeBusCycle(CpuBusCycleType type) const;
        bool shouldRDYStallForBusCycle(CpuBusCycleType type) const;
        bool shouldAECBlockBusCycle(CpuBusCycleType type) const;
        bool tryFetchOpcode(uint8_t& opcode);

        // Micro OP
        void clearMicroOps();
        void pushMicroOp(const CpuMicroOp& op);
        bool executeCurrentMicroOp();
        void buildMicroOpsForOpcode(uint8_t opcode);
        void buildAbsoluteLoad(CpuMicroAction action);
        void buildAbsoluteIndexedLoad(CpuIndexReg index, CpuMicroAction action);
        void buildAbsoluteStore(CpuMicroAction action);
        void buildAbsoluteIndexedStore(CpuIndexReg index, CpuMicroAction action);
        void buildImmediateAction(CpuMicroAction action);
        void buildInternalAction(CpuMicroAction action);
        void buildZeroPageReadAction(CpuMicroAction action);
        void buildZeroPageStore(CpuMicroAction action);
        void buildZeroPageIndexedLoad(CpuIndexReg index, CpuMicroAction action);
        void buildZeroPageIndexedStore(CpuIndexReg index, CpuMicroAction action);
        void buildIndirectXRead(CpuMicroAction action);
        void buildIndirectYRead(CpuMicroAction action);
        void buildIndirectXStore(CpuMicroAction action);
        void buildIndirectYStore(CpuMicroAction action);
        void buildZeroPageRMW(CpuMicroAction action);
        void buildAbsoluteRMW(CpuMicroAction action);
        void buildZeroPageIndexedRMW(CpuIndexReg index, CpuMicroAction action);
        void buildAbsoluteIndexedRMW(CpuIndexReg index, CpuMicroAction action);
        void buildBranch(CpuMicroAction action);
        void buildStackPush(CpuMicroAction action);
        void buildStackPull(CpuMicroAction action);
        void buildJSR();
        void buildRTS();
        void buildBRK();
        void buildRTI();
        bool canExecuteOpcodeWithMicroOps(uint8_t opcode) const;
        bool tickMicroOps();

        // Micro Op Helpers
        uint8_t getIndexValue(CpuIndexReg index) const;
        void compareRegisterWithTemp(uint8_t reg);
        void adcValue(uint8_t value);
        void sbcValue(uint8_t value);
        uint8_t applyRMWAction(CpuMicroAction action, uint8_t oldValue);

        // Tracing
        TraceManager::Stamp makeCpuStamp() const;
};

#endif // CPU_H
