// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CPU.h"
#include "StateWriter.h"

CPU::CPU() :
    // Initialize
    cia2(nullptr),
    IRQ(nullptr),
    logger(nullptr),
    mem(nullptr),
    traceMgr(nullptr),
    vic(nullptr),
    busCycleActive(false),
    microOpCount(0),
    microOpIndex(0),
    microInstructionActive(false),
    executingMicroOp(false),
    activeOpcode(0xEA),
    activeOpcodePC(0),
    useMicroOpsForTest(true),
    microTemp(0),
    nmiPending(false),
    nmiLine(false),
    irqSuppressOne(false),
    jamMode(JamMode::NopCompat),
    halted(false),
    pendingOpcodeFetch(false),
    pendingOpcodeAddress(0),
    cycles(0),
    totalCycles(0),
    elapsedCycles(0),
    lastCycleCount(0),
    A(0),
    X(0),
    Y(0),
    SP(0xFD),
    SR(0x20),
    PC(0),
    lastOpcodePC(0),
    lastOpcode(0xEA),
    setLogging(false),
    rdyLine(true),
    aecLine(true),
    vicBusArbitrationEnabled(false)
{
    currentBusCycle = {};
    initializeOpcodeTable();
}

CPU::~CPU() = default;

void CPU::CPUState::save(StateWriter& wrtr) const
{
    wrtr.writeU16(PC);
    wrtr.writeU8(A);
    wrtr.writeU8(X);
    wrtr.writeU8(Y);
    wrtr.writeU8(SP);
    wrtr.writeU8(SR);
}

bool CPU::CPUState::load(StateReader& rdr)
{
    if (!rdr.readU16(PC)) return false;
    if (!rdr.readU8(A))   return false;
    if (!rdr.readU8(X))   return false;
    if (!rdr.readU8(Y))   return false;
    if (!rdr.readU8(SP))  return false;
    if (!rdr.readU8(SR))  return false;

    SR = (SR | 0x20) & ~0x10; // force U bit high, clear internal B bit
    return true;
}

void CPU::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("CPU0");
    saveStatePayload(wrtr);
    wrtr.endChunk();

    wrtr.beginChunk("CPUX");
    saveStateExtendedPayload(wrtr);
    wrtr.endChunk();
}

bool CPU::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "CPU0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);
        const bool ok = loadStatePayload(rdr);
        rdr.exitChunkPayload(chunk);
        return ok;
    }

    if (std::memcmp(chunk.tag, "CPUX", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);
        const bool ok = loadStateExtendedPayload(chunk, rdr);
        rdr.exitChunkPayload(chunk);
        return ok;
    }

    return false;
}

void CPU::saveStatePayload(StateWriter& wrtr) const
{
    CPUState state = getState();
    state.save(wrtr);
}

bool CPU::loadStatePayload(StateReader& rdr)
{
    CPUState state;
    if (!state.load(rdr)) return false;

    PC = state.PC;
    A  = state.A;
    X  = state.X;
    Y  = state.Y;
    SP = state.SP;
    SR = (state.SR | 0x20) & ~0x10; // force U bit

    return true;
}

void CPU::saveStateExtendedPayload(StateWriter& wrtr) const
{
    wrtr.writeU8(static_cast<uint8_t>(jamMode));
    wrtr.writeBool(halted);

    wrtr.writeBool(nmiPending);
    wrtr.writeBool(nmiLine);
    wrtr.writeBool(irqSuppressOne);

    wrtr.writeU32(totalCycles);
    wrtr.writeU32(cycles);
    wrtr.writeU32(lastCycleCount);

    wrtr.writeU8(static_cast<uint8_t>(mode_));
    wrtr.writeBool(soLevel);
    wrtr.writeBool(rdyLine);
    wrtr.writeBool(aecLine);
}

bool CPU::loadStateExtendedPayload(const StateReader::Chunk& parentChunk, StateReader& rdr)
{
    uint8_t jm = 0;
    if (!rdr.readU8(jm)) return false;
    jamMode = static_cast<JamMode>(jm);

    if (!rdr.readBool(halted)) return false;
    if (!rdr.readBool(nmiPending)) return false;
    if (!rdr.readBool(nmiLine)) return false;
    if (!rdr.readBool(irqSuppressOne)) return false;

    if (!rdr.readU32(totalCycles)) return false;
    if (!rdr.readU32(cycles)) return false;
    if (!rdr.readU32(lastCycleCount)) return false;

    uint8_t vm = 0;
    if (!rdr.readU8(vm)) return false;
    mode_ = static_cast<VideoMode>(vm);
    setMode(mode_);

    const size_t end = parentChunk.payloadOffset + parentChunk.length;
    if (rdr.cursor() < end) { if (!rdr.readBool(soLevel)) return false; }
    if (rdr.cursor() < end) { if (!rdr.readBool(rdyLine)) return false; }
    if (rdr.cursor() < end) { if (!rdr.readBool(aecLine)) return false; }

    return true;
}

void CPU::reset()
{
    // Default Jam Mode
    jamMode = JamMode::FreezePC;
    halted = false;

    // reset registers
    A = 0;
    X = 0;
    Y = 0;

    // Set PC to reset vector
    PC = (mem->read(0xFFFC) | (mem->read(0xFFFD) << 8));

    // Defaults
    SP                          = 0xFD;
    SR                          = 0x24;
    lastOpcodePC                = PC;
    lastOpcode                  = 0xEA;
    cycles                      = 0;
    totalCycles                 = 0;
    elapsedCycles               = 0;
    lastCycleCount              = 0;
    currentBusCycle             = {};
    busCycleActive              = false;
    rdyLine                     = true;
    aecLine                     = true;
    pendingOpcodeFetch          = false;
    pendingOpcodeAddress        = 0;
    setLogging                  = false;
    nmiPending                  = false;
    nmiLine                     = false;
    irqSuppressOne              = false;
    soLevel                     = true;
    microOps                    = {};
    microOpCount                = 0;
    microOpIndex                = 0;
    microInstructionActive      = false;
    executingMicroOp            = false;
    activeOpcode                = 0xEA;
    activeOpcodePC              = 0;
    microTemp                   = 0;

    // if mode_ wasn’t set yet, assume NTSC
    if (CYCLES_PER_FRAME == 0) CYCLES_PER_FRAME = 17096;
}

void CPU::setMode(VideoMode mode)
{
    mode_ = mode;
    if (mode_ == VideoMode::NTSC)
    {
        CYCLES_PER_FRAME = 17096;
    }
    else
    {
        CYCLES_PER_FRAME = 19656;
    }
}

CPU::JamMode CPU::getJamMode() const
{
    return jamMode;
}

void CPU::setJamMode(JamMode mode)
{
    jamMode = mode;
}

void CPU::setNMILine(bool asserted)
{
    if (asserted && !nmiLine)
    {
        nmiPending = true;

        if (traceMgr)
            traceMgr->recordCPUNMI("NMI rising edge -> pending set", makeCpuStamp());
    }

    nmiLine = asserted;
}

void CPU::handleIRQ()
{
    // IRQs are sampled at instruction boundaries.
    // The one-instruction suppression after CLI/PLP/RTI expires
    // at the boundary even if no IRQ source is currently active.

    if (getFlag(I))
    {
        irqSuppressOne = false;
        return;
    }

    if (irqSuppressOne)
    {
        irqSuppressOne = false;
        return;
    }

    if (!IRQ || !IRQ->isIRQActive())
        return;

    if (traceMgr)
        traceMgr->recordCPUIRQ("IRQ line active", makeCpuStamp());

    executeIRQ();
}

void CPU::handleNMI()
{
    if (!nmiPending)
        return;

    if (traceMgr)
        traceMgr->recordCPUNMI("NMI pending consumed", makeCpuStamp());

    nmiPending = false;
    executeNMI();
}

void CPU::pulseNMI()
{
    if (traceMgr)
        traceMgr->recordCPUNMI("pulseNMI()", makeCpuStamp());

    setNMILine(true);
    setNMILine(false);
}

void CPU::setSO(bool level)
{
    if (soLevel && !level)
        SR |= V;

    soLevel = level;
}

void CPU::pulseSO()
{
    if (!soLevel) setSO(true);
    setSO(false); // falling edge => sets V
    setSO(true);  // back to idle so next pulse works
}

void CPU::executeIRQ()
{
    const uint16_t irqReturnPC = PC;
    const uint8_t spBefore = SP;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "IRQ accepted at PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << irqReturnPC
            << " SP=$" << std::setw(2) << int(spBefore);
        traceMgr->recordCPUIRQ(oss.str(), makeCpuStamp());
    }

    // IRQ entry dummy read.
    cpuRead(PC, CpuBusCycleType::DummyRead);

    // Push return PC high, then low
    push((PC >> 8) & 0xFF);
    push(PC & 0xFF);

    // Push SR with B=0, bit 5/U=1 for hardware IRQ
    uint8_t status = SR;
    status &= ~0x10; // clear B
    status |= 0x20;  // set U / unused bit

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "IRQ pushed return=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << irqReturnPC
            << " SR=$" << std::setw(2) << int(status);
        traceMgr->recordCPUIRQ(oss.str(), makeCpuStamp());
    }

    push(status);

    const uint8_t spAfter = SP;

    // Set interrupt disable
    setFlag(I, true);

    // Fetch IRQ/BRK vector
    const uint8_t vectorLo = cpuRead(0xFFFE, CpuBusCycleType::Read);
    const uint8_t vectorHi = cpuRead(0xFFFF, CpuBusCycleType::Read);
    const uint16_t irqVector = uint16_t(vectorLo) | (uint16_t(vectorHi) << 8);
    PC = irqVector;

    lastInterruptEntry.type = InterruptEntryType::IRQ;
    lastInterruptEntry.acceptedAtPC = irqReturnPC;
    lastInterruptEntry.pushedReturnPC = irqReturnPC;
    lastInterruptEntry.pushedSR = status;
    lastInterruptEntry.spBefore = spBefore;
    lastInterruptEntry.spAfter = spAfter;
    lastInterruptEntry.vectorAddress = 0xFFFE;
    lastInterruptEntry.vectorTarget = irqVector;
    lastInterruptEntry.totalCycles = totalCycles;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "IRQ vector -> PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << irqVector
            << " SP $" << std::setw(2) << int(spBefore)
            << "->$" << std::setw(2) << int(spAfter);
        traceMgr->recordCPUIRQ(oss.str(), makeCpuStamp());
    }

    cycles += 7;
}

void CPU::executeNMI()
{
    const uint16_t nmiReturnPC = PC;
    const uint8_t spBefore = SP;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "NMI accepted at PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << nmiReturnPC
            << " SP=$" << std::setw(2) << int(spBefore);
        traceMgr->recordCPUNMI(oss.str(), makeCpuStamp());
    }

    cpuRead(PC, CpuBusCycleType::DummyRead);

    push((PC >> 8) & 0xFF);
    push(PC & 0xFF);

    uint8_t status = SR;
    status &= ~0x10; // B=0 for hardware interrupt
    status |= 0x20;  // U/bit 5 set in pushed status

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "NMI pushed return=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << nmiReturnPC
            << " SR=$" << std::setw(2) << int(status);
        traceMgr->recordCPUNMI(oss.str(), makeCpuStamp());
    }

    push(status);

    const uint8_t spAfter = SP;

    setFlag(I, true);

    const uint8_t vectorLo = cpuRead(0xFFFA, CpuBusCycleType::Read);
    const uint8_t vectorHi = cpuRead(0xFFFB, CpuBusCycleType::Read);
    const uint16_t nmiVector = uint16_t(vectorLo) | (uint16_t(vectorHi) << 8);
    PC = nmiVector;

    lastInterruptEntry.type = InterruptEntryType::NMI;
    lastInterruptEntry.acceptedAtPC = nmiReturnPC;
    lastInterruptEntry.pushedReturnPC = nmiReturnPC;
    lastInterruptEntry.pushedSR = status;
    lastInterruptEntry.spBefore = spBefore;
    lastInterruptEntry.spAfter = spAfter;
    lastInterruptEntry.vectorAddress = 0xFFFA;
    lastInterruptEntry.vectorTarget = nmiVector;
    lastInterruptEntry.totalCycles = totalCycles;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "NMI vector -> PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << nmiVector
            << " SP $" << std::setw(2) << int(spBefore)
            << "->$" << std::setw(2) << int(spAfter);
        traceMgr->recordCPUNMI(oss.str(), makeCpuStamp());
    }

    cycles += 7;
}

uint8_t CPU::cpuRead(uint16_t address, CpuBusCycleType type)
{
    currentBusCycle = { type, address, 0 };
    busCycleActive = true;

    if (shouldRDYStallForBusCycle(type))
    {
        if (traceMgr)
            traceMgr->recordCPUBA("RDY/BA low during read-like CPU bus cycle", makeCpuStamp());
    }

    if (shouldAECBlockBusCycle(type))
    {
        if (traceMgr)
            traceMgr->recordCPUBA("AEC low during CPU read bus cycle", makeCpuStamp());
    }

    const uint8_t value = mem->read(address);

    busCycleActive = false;
    currentBusCycle = {};

    return value;
}

void CPU::cpuWrite(uint16_t address, uint8_t value, CpuBusCycleType type)
{
    currentBusCycle = { type, address, value };
    busCycleActive = true;

    if (shouldAECBlockBusCycle(type))
    {
        if (traceMgr)
            traceMgr->recordCPUBA("AEC low during CPU write bus cycle", makeCpuStamp());
    }

    mem->write(address, value);

    busCycleActive = false;
    currentBusCycle = {};
}

CPU::CPUIrqDebugState CPU::getIrqDebugState() const
{
    CPUIrqDebugState s;

    s.pc                = PC;
    s.sr                = SR;
    s.lastOpcodePC      = lastOpcodePC;
    s.lastOpcode        = lastOpcode;
    s.iFlag             = getFlag(I);
    s.irqLineActive     = IRQ && IRQ->isIRQActive();

    s.nmiPending        = nmiPending;
    s.nmiLine           = nmiLine;
    s.irqSuppressOne    = irqSuppressOne;

    s.rdyLine           = rdyLine;
    s.aecLine           = aecLine;
    s.soLevel           = soLevel;

    s.cyclesRemaining   = cycles;
    s.totalCycles       = totalCycles;

    return s;
}

CPU::CPUCycleDebugState CPU::getCycleDebugState() const
{
    CPUCycleDebugState s;

    s.lastOpcodePC          = lastOpcodePC;
    s.lastOpcode            = lastOpcode;
    s.totalCycles           = totalCycles;
    s.cyclesRemaining       = cycles;
    s.cyclesPerFrame        = CYCLES_PER_FRAME;
    s.frameCycle            = CYCLES_PER_FRAME ? (totalCycles % CYCLES_PER_FRAME) : 0;

    s.betweenInstructions   = cycles <= 0;
    s.rdyLine               = rdyLine;
    s.aecLine               = aecLine;
    s.halted                = halted;

    s.mode                  = mode_;

    s.raster                = vic ? vic->getCurrentRaster() : 0;
    s.dot                   = vic ? vic->getRasterDot() : 0;
    s.busCycleActive        = busCycleActive;
    s.busCycleType          = currentBusCycle.type;
    s.busAddress            = currentBusCycle.address;
    s.busValue              = currentBusCycle.value;

    return s;
}

CPU::CPUState CPU::getState() const
{
    CPUState st;
    st.PC = PC;
    st.A = A;
    st.X = X;
    st.Y = Y;
    st.SP = SP;
    st.SR = SR;
    return st;
}

void CPU::initializeOpcodeTable()
{
    opcodeTable.fill([this]() { NOP(0xEA); }); // Default to NOP

    opcodeTable[0x00] = [this]() { BRK(); };
    opcodeTable[0x01] = [this]() { ORA(0x01); };
    opcodeTable[0x02] = [this]() { JAM(); };
    opcodeTable[0x03] = [this]() { SLO(0x03); };
    opcodeTable[0x04] = [this]() { NOP(0x04); };
    opcodeTable[0x05] = [this]() { ORA(0x05); };
    opcodeTable[0x06] = [this]() { ASL(0x06); };
    opcodeTable[0x07] = [this]() { SLO(0x07); };
    opcodeTable[0x08] = [this]() { PHP(); };
    opcodeTable[0x09] = [this]() { ORA(0x09); };
    opcodeTable[0x0A] = [this]() { ASL(0x0A); };
    opcodeTable[0x0B] = [this]() { AAC(); };
    opcodeTable[0x0C] = [this]() { NOP(0x0C); };
    opcodeTable[0x0D] = [this]() { ORA(0x0D); };
    opcodeTable[0x0E] = [this]() { ASL(0x0E); };
    opcodeTable[0x0F] = [this]() { SLO(0x0F); };
    opcodeTable[0x10] = [this]() { BPL(); };
    opcodeTable[0x11] = [this]() { ORA(0x11); };
    opcodeTable[0x12] = [this]() { JAM(); };
    opcodeTable[0x13] = [this]() { SLO(0x13); };
    opcodeTable[0x14] = [this]() { NOP(0x14); };
    opcodeTable[0x15] = [this]() { ORA(0x15); };
    opcodeTable[0x16] = [this]() { ASL(0x16); };
    opcodeTable[0x17] = [this]() { SLO(0x17); };
    opcodeTable[0x18] = [this]() { CLC(); };
    opcodeTable[0x19] = [this]() { ORA(0x19); };
    opcodeTable[0x1A] = [this]() { NOP(0x1A); };
    opcodeTable[0x1B] = [this]() { SLO(0x1B); };
    opcodeTable[0x1C] = [this]() { NOP(0x1C); };
    opcodeTable[0x1D] = [this]() { ORA(0x1D); };
    opcodeTable[0x1E] = [this]() { ASL(0x1E); };
    opcodeTable[0x1F] = [this]() { SLO(0x1F); };
    opcodeTable[0x20] = [this]() { JSR(); };
    opcodeTable[0x21] = [this]() { AND(0x21); };
    opcodeTable[0x22] = [this]() { JAM(); };
    opcodeTable[0x23] = [this]() { RLA(0x23); };
    opcodeTable[0x24] = [this]() { BIT(0x24); };
    opcodeTable[0x25] = [this]() { AND(0x25); };
    opcodeTable[0x26] = [this]() { ROL(0x26); };
    opcodeTable[0x27] = [this]() { RLA(0x27); };
    opcodeTable[0x28] = [this]() { PLP(); };
    opcodeTable[0x29] = [this]() { AND(0x29); };
    opcodeTable[0x2A] = [this]() { ROL(0x2A); };
    opcodeTable[0x2B] = [this]() { AAC(); };
    opcodeTable[0x2C] = [this]() { BIT(0x2C); };
    opcodeTable[0x2D] = [this]() { AND(0x2D); };
    opcodeTable[0x2E] = [this]() { ROL(0x2E); };
    opcodeTable[0x2F] = [this]() { RLA(0x2F); };
    opcodeTable[0x30] = [this]() { BMI(); };
    opcodeTable[0x31] = [this]() { AND(0x31); };
    opcodeTable[0x32] = [this]() { JAM(); };
    opcodeTable[0x33] = [this]() { RLA(0x33); };
    opcodeTable[0x34] = [this]() { NOP(0x34); };
    opcodeTable[0x35] = [this]() { AND(0x35); };
    opcodeTable[0x36] = [this]() { ROL(0x36); };
    opcodeTable[0x37] = [this]() { RLA(0x37); };
    opcodeTable[0x38] = [this]() { SEC(); };
    opcodeTable[0x39] = [this]() { AND(0x39); };
    opcodeTable[0x3A] = [this]() { NOP(0x3A); };
    opcodeTable[0x3B] = [this]() { RLA(0x3B); };
    opcodeTable[0x3C] = [this]() { NOP(0x3C); };
    opcodeTable[0x3D] = [this]() { AND(0x3D); };
    opcodeTable[0x3E] = [this]() { ROL(0x3E); };
    opcodeTable[0x3F] = [this]() { RLA(0x3F); };
    opcodeTable[0x40] = [this]() { RTI(); };
    opcodeTable[0x41] = [this]() { EOR(0x41); };
    opcodeTable[0x42] = [this]() { JAM(); };
    opcodeTable[0x43] = [this]() { SRE(0x43); };
    opcodeTable[0x44] = [this]() { NOP(0x44); };
    opcodeTable[0x45] = [this]() { EOR(0x45); };
    opcodeTable[0x46] = [this]() { LSR(0x46); };
    opcodeTable[0x47] = [this]() { SRE(0x47); };
    opcodeTable[0x48] = [this]() { PHA(); };
    opcodeTable[0x49] = [this]() { EOR(0x49); };
    opcodeTable[0x4A] = [this]() { LSR(0x4A); };
    opcodeTable[0x4B] = [this]() { ALR(); };
    opcodeTable[0x4C] = [this]() { JMP(0x4C); };
    opcodeTable[0x4D] = [this]() { EOR(0x4D); };
    opcodeTable[0x4E] = [this]() { LSR(0x4E); };
    opcodeTable[0x4F] = [this]() { SRE(0x4F); };
    opcodeTable[0x50] = [this]() { BVC(); };
    opcodeTable[0x51] = [this]() { EOR(0x51); };
    opcodeTable[0x52] = [this]() { JAM(); };
    opcodeTable[0x53] = [this]() { SRE(0x53); };
    opcodeTable[0x54] = [this]() { NOP(0x54); };
    opcodeTable[0x55] = [this]() { EOR(0x55); };
    opcodeTable[0x56] = [this]() { LSR(0x56); };
    opcodeTable[0x57] = [this]() { SRE(0x57); };
    opcodeTable[0x58] = [this]() { CLI(); };
    opcodeTable[0x59] = [this]() { EOR(0x59); };
    opcodeTable[0x5A] = [this]() { NOP(0x5A); };
    opcodeTable[0x5B] = [this]() { SRE(0x5B); };
    opcodeTable[0x5C] = [this]() { NOP(0x5C); };
    opcodeTable[0x5D] = [this]() { EOR(0x5D); };
    opcodeTable[0x5E] = [this]() { LSR(0x5E); };
    opcodeTable[0x5F] = [this]() { SRE(0x5F); };
    opcodeTable[0x60] = [this]() { RTS(); };
    opcodeTable[0x61] = [this]() { ADC(0x61); };
    opcodeTable[0x62] = [this]() { JAM(); };
    opcodeTable[0x63] = [this]() { RRA(0x63); };
    opcodeTable[0x64] = [this]() { NOP(0x64); };
    opcodeTable[0x65] = [this]() { ADC(0x65); };
    opcodeTable[0x66] = [this]() { ROR(0x66); };
    opcodeTable[0x67] = [this]() { RRA(0x67); };
    opcodeTable[0x68] = [this]() { PLA(); };
    opcodeTable[0x69] = [this]() { ADC(0x69); };
    opcodeTable[0x6A] = [this]() { ROR(0x6A); };
    opcodeTable[0x6B] = [this]() { ARR(); };
    opcodeTable[0x6C] = [this]() { JMP(0x6C); };
    opcodeTable[0x6D] = [this]() { ADC(0x6D); };
    opcodeTable[0x6E] = [this]() { ROR(0x6E); };
    opcodeTable[0x6F] = [this]() { RRA(0x6F); };
    opcodeTable[0x70] = [this]() { BVS(); };
    opcodeTable[0x71] = [this]() { ADC(0x71); };
    opcodeTable[0x72] = [this]() { JAM(); };
    opcodeTable[0x73] = [this]() { RRA(0x73); };
    opcodeTable[0x74] = [this]() { NOP(0x74); };
    opcodeTable[0x75] = [this]() { ADC(0x75); };
    opcodeTable[0x76] = [this]() { ROR(0x76); };
    opcodeTable[0x77] = [this]() { RRA(0x77); };
    opcodeTable[0x78] = [this]() { SEI(); };
    opcodeTable[0x79] = [this]() { ADC(0x79); };
    opcodeTable[0x7A] = [this]() { NOP(0x7A); };
    opcodeTable[0x7B] = [this]() { RRA(0x7B); };
    opcodeTable[0x7C] = [this]() { NOP(0x7C); };
    opcodeTable[0x7D] = [this]() { ADC(0x7D); };
    opcodeTable[0x7E] = [this]() { ROR(0x7E); };
    opcodeTable[0x7F] = [this]() { RRA(0x7F); };
    opcodeTable[0x80] = [this]() { NOP(0x80); };
    opcodeTable[0x81] = [this]() { STA(0x81); };
    opcodeTable[0x82] = [this]() { NOP(0x82); };
    opcodeTable[0x83] = [this]() { SAX(0x83); };
    opcodeTable[0x84] = [this]() { STY(0x84); };
    opcodeTable[0x85] = [this]() { STA(0x85); };
    opcodeTable[0x86] = [this]() { STX(0x86); };
    opcodeTable[0x87] = [this]() { SAX(0x87); };
    opcodeTable[0x88] = [this]() { DEY(); };
    opcodeTable[0x89] = [this]() { NOP(0x89); };
    opcodeTable[0x8A] = [this]() { TXA(); };
    opcodeTable[0x8B] = [this]() { XAA(); };
    opcodeTable[0x8C] = [this]() { STY(0x8C); };
    opcodeTable[0x8D] = [this]() { STA(0x8D); };
    opcodeTable[0x8E] = [this]() { STX(0x8E); };
    opcodeTable[0x8F] = [this]() { SAX(0x8F); };
    opcodeTable[0x90] = [this]() { BCC(); };
    opcodeTable[0x91] = [this]() { STA(0x91); };
    opcodeTable[0x92] = [this]() { JAM(); };
    opcodeTable[0x93] = [this]() { AHX(0x93); };
    opcodeTable[0x94] = [this]() { STY(0x94); };
    opcodeTable[0x95] = [this]() { STA(0x95); };
    opcodeTable[0x96] = [this]() { STX(0x96); };
    opcodeTable[0x97] = [this]() { SAX(0x97); };
    opcodeTable[0x98] = [this]() { TYA(); };
    opcodeTable[0x99] = [this]() { STA(0x99); };
    opcodeTable[0x9A] = [this]() { TXS(); };
    opcodeTable[0x9B] = [this]() { TAS(); };
    opcodeTable[0x9C] = [this]() { SHY(); };
    opcodeTable[0x9D] = [this]() { STA(0x9D); };
    opcodeTable[0x9E] = [this]() { SHX(); };
    opcodeTable[0x9F] = [this]() { AHX(0x9F); };
    opcodeTable[0xA0] = [this]() { LDY(0xA0); };
    opcodeTable[0xA1] = [this]() { LDA(0xA1); };
    opcodeTable[0xA2] = [this]() { LDX(0xA2); };
    opcodeTable[0xA3] = [this]() { LAX(0xA3); };
    opcodeTable[0xA4] = [this]() { LDY(0xA4); };
    opcodeTable[0xA5] = [this]() { LDA(0xA5); };
    opcodeTable[0xA6] = [this]() { LDX(0xA6); };
    opcodeTable[0xA7] = [this]() { LAX(0xA7); };
    opcodeTable[0xA8] = [this]() { TAY(); };
    opcodeTable[0xA9] = [this]() { LDA(0xA9); };
    opcodeTable[0xAA] = [this]() { TAX(); };
    opcodeTable[0xAB] = [this]() { LAX(0xAB); };
    opcodeTable[0xAC] = [this]() { LDY(0xAC); };
    opcodeTable[0xAD] = [this]() { LDA(0xAD); };
    opcodeTable[0xAE] = [this]() { LDX(0xAE); };
    opcodeTable[0xAF] = [this]() { LAX(0xAF); };
    opcodeTable[0xB0] = [this]() { BCS(); };
    opcodeTable[0xB1] = [this]() { LDA(0xB1); };
    opcodeTable[0xB2] = [this]() { JAM(); };
    opcodeTable[0xB3] = [this]() { LAX(0xB3); };
    opcodeTable[0xB4] = [this]() { LDY(0xB4); };
    opcodeTable[0xB5] = [this]() { LDA(0xB5); };
    opcodeTable[0xB6] = [this]() { LDX(0xB6); };
    opcodeTable[0xB7] = [this]() { LAX(0xB7); };
    opcodeTable[0xB8] = [this]() { CLV(); };
    opcodeTable[0xB9] = [this]() { LDA(0xB9); };
    opcodeTable[0xBA] = [this]() { TSX(); };
    opcodeTable[0xBB] = [this]() { LAS(); };
    opcodeTable[0xBC] = [this]() { LDY(0xBC); };
    opcodeTable[0xBD] = [this]() { LDA(0xBD); };
    opcodeTable[0xBE] = [this]() { LDX(0xBE); };
    opcodeTable[0xBF] = [this]() { LAX(0xBF); };
    opcodeTable[0xC0] = [this]() { CPY(0xC0); };
    opcodeTable[0xC1] = [this]() { CMP(0xC1); };
    opcodeTable[0xC2] = [this]() { NOP(0xC2); };
    opcodeTable[0xC3] = [this]() { DCP(0xC3); };
    opcodeTable[0xC4] = [this]() { CPY(0xC4); };
    opcodeTable[0xC5] = [this]() { CMP(0xC5); };
    opcodeTable[0xC6] = [this]() { DEC(0xC6); };
    opcodeTable[0xC7] = [this]() { DCP(0xC7); };
    opcodeTable[0xC8] = [this]() { INY(); };
    opcodeTable[0xC9] = [this]() { CMP(0xC9); };
    opcodeTable[0xCA] = [this]() { DEX(); };
    opcodeTable[0xCB] = [this]() { AXS(); };
    opcodeTable[0xCC] = [this]() { CPY(0xCC); };
    opcodeTable[0xCD] = [this]() { CMP(0xCD); };
    opcodeTable[0xCE] = [this]() { DEC(0xCE); };
    opcodeTable[0xCF] = [this]() { DCP(0xCF); };
    opcodeTable[0xD0] = [this]() { BNE(); };
    opcodeTable[0xD1] = [this]() { CMP(0xD1); };
    opcodeTable[0xD2] = [this]() { JAM(); };
    opcodeTable[0xD3] = [this]() { DCP(0xD3); };
    opcodeTable[0xD4] = [this]() { NOP(0xD4); };
    opcodeTable[0xD5] = [this]() { CMP(0xD5); };
    opcodeTable[0xD6] = [this]() { DEC(0xD6); };
    opcodeTable[0xD7] = [this]() { DCP(0xD7); };
    opcodeTable[0xD8] = [this]() { CLD(); };
    opcodeTable[0xD9] = [this]() { CMP(0xD9); };
    opcodeTable[0xDA] = [this]() { NOP(0xDA); };
    opcodeTable[0xDB] = [this]() { DCP(0xDB); };
    opcodeTable[0xDC] = [this]() { NOP(0xDC); };
    opcodeTable[0xDD] = [this]() { CMP(0xDD); };
    opcodeTable[0xDE] = [this]() { DEC(0xDE); };
    opcodeTable[0xDF] = [this]() { DCP(0xDF); };
    opcodeTable[0xE0] = [this]() { CPX(0xE0); };
    opcodeTable[0xE1] = [this]() { SBC(0xE1); };
    opcodeTable[0xE2] = [this]() { NOP(0xE2); };
    opcodeTable[0xE3] = [this]() { ISC(0xE3); };
    opcodeTable[0xE4] = [this]() { CPX(0xE4); };
    opcodeTable[0xE5] = [this]() { SBC(0xE5); };
    opcodeTable[0xE6] = [this]() { INC(0xE6); };
    opcodeTable[0xE7] = [this]() { ISC(0xE7); };
    opcodeTable[0xE8] = [this]() { INX(); };
    opcodeTable[0xE9] = [this]() { SBC(0xE9); };
    opcodeTable[0xEA] = [this]() { NOP(0xEA); };
    opcodeTable[0xEB] = [this]() { SBC(0xEB); };
    opcodeTable[0xEC] = [this]() { CPX(0xEC); };
    opcodeTable[0xED] = [this]() { SBC(0xED); };
    opcodeTable[0xEE] = [this]() { INC(0xEE); };
    opcodeTable[0xEF] = [this]() { ISC(0xEF); };
    opcodeTable[0xF0] = [this]() { BEQ(); };
    opcodeTable[0xF1] = [this]() { SBC(0xF1); };
    opcodeTable[0xF2] = [this]() { JAM(); };
    opcodeTable[0xF3] = [this]() { ISC(0xF3); };
    opcodeTable[0xF4] = [this]() { NOP(0xF4); };
    opcodeTable[0xF5] = [this]() { SBC(0xF5); };
    opcodeTable[0xF6] = [this]() { INC(0xF6); };
    opcodeTable[0xF7] = [this]() { ISC(0xF7); };
    opcodeTable[0xF8] = [this]() { SED(); };
    opcodeTable[0xF9] = [this]() { SBC(0xF9); };
    opcodeTable[0xFA] = [this]() { NOP(0xFA); };
    opcodeTable[0xFB] = [this]() { ISC(0xFB); };
    opcodeTable[0xFC] = [this]() { NOP(0xFC); };
    opcodeTable[0xFD] = [this]() { SBC(0xFD); };
    opcodeTable[0xFE] = [this]() { INC(0xFE); };
    opcodeTable[0xFF] = [this]() { ISC(0xFF); };
}

uint8_t CPU::readABS()
{
    uint16_t address = absAddress();
    return cpuRead(address, CpuBusCycleType::Read);
}

uint8_t CPU::readABSX()
{
    uint16_t address = absXAddress();
    return cpuRead(address, CpuBusCycleType::Read);
}

uint8_t CPU::readABSY()
{
    uint16_t address = absYAddress();
    return cpuRead(address, CpuBusCycleType::Read);
}

uint8_t CPU::readImmediate()
{
    return fetchOperand();
}

uint8_t CPU::readIndirectX()
{
    const uint16_t address = indirectXAddress();
    return cpuRead(address, CpuBusCycleType::Read);
}

uint8_t CPU::readIndirectY()
{
    const uint16_t address = indirectYAddress();
    return cpuRead(address, CpuBusCycleType::Read);
}

uint8_t CPU::readZP()
{
    uint16_t address = zpAddress();
    return cpuRead(address, CpuBusCycleType::Read);
}

uint8_t CPU::readZPX()
{
    uint16_t address = zpXAddress();
    return cpuRead(address, CpuBusCycleType::Read);
}

uint8_t CPU::readZPY()
{
    uint16_t address = zpYAddress();
    return cpuRead(address, CpuBusCycleType::Read);
}

uint16_t CPU::absAddress()
{
    uint16_t address = fetchOperand() | (fetchOperand() << 8);
    return address;
}

uint16_t CPU::absXAddress()
{
    uint16_t baseAddress = fetchOperand() | (fetchOperand() << 8);
    uint16_t effectiveAddress = (baseAddress + X) & 0xFFFF;

    return effectiveAddress;
}

uint16_t CPU::absYAddress()
{
    uint16_t baseAddress = fetchOperand() | (fetchOperand() << 8);
    uint16_t effectiveAddress = (baseAddress + Y) & 0xFFFF;

    return effectiveAddress;
}

uint16_t CPU::indirectXAddress()
{
    const uint16_t operandPC = PC;
    const uint8_t zpOperand = fetchOperand();

    const uint8_t zpAddress = uint8_t(zpOperand + X);

    const uint8_t lowByte =
        cpuRead(zpAddress, CpuBusCycleType::Read);

    const uint8_t highAddr = uint8_t(zpAddress + 1);

    const uint8_t highByte =
        cpuRead(highAddr, CpuBusCycleType::Read);

    const uint16_t baseAddress =
        uint16_t(lowByte) | (uint16_t(highByte) << 8);

    lastAddressDebug.valid = true;
    lastAddressDebug.mode = CPUAddressDebugMode::IndirectX;
    lastAddressDebug.operandPC = operandPC;
    lastAddressDebug.zpOperand = zpOperand;
    lastAddressDebug.indexValue = X;
    lastAddressDebug.indexedZP = zpAddress;
    lastAddressDebug.pointerLowAddr = zpAddress;
    lastAddressDebug.pointerHighAddr = highAddr;
    lastAddressDebug.pointerLowValue = lowByte;
    lastAddressDebug.pointerHighValue = highByte;
    lastAddressDebug.baseAddress = baseAddress;
    lastAddressDebug.effectiveAddress = baseAddress;
    lastAddressDebug.pageCrossed = false;
    lastAddressDebug.dummyReadUsed = false;
    lastAddressDebug.dummyReadAddress = 0;
    lastAddressDebug.valueRead = 0;
    lastAddressDebug.totalCycles = totalCycles;

    return baseAddress;
}

uint16_t CPU::indirectYAddress()
{
    const uint16_t operandPC = PC;
    const uint8_t zpAddress = fetchOperand();

    const uint8_t lowAddr  = zpAddress;
    const uint8_t highAddr = uint8_t(zpAddress + 1);

    const uint8_t lowByte =
        cpuRead(lowAddr, CpuBusCycleType::Read);

    const uint8_t highByte =
        cpuRead(highAddr, CpuBusCycleType::Read);

    const uint16_t baseAddress =
        uint16_t(lowByte) | (uint16_t(highByte) << 8);

    const uint16_t effectiveAddress =
        uint16_t(baseAddress + Y);

    lastAddressDebug.valid = true;
    lastAddressDebug.mode = CPUAddressDebugMode::IndirectY;
    lastAddressDebug.operandPC = operandPC;
    lastAddressDebug.zpOperand = zpAddress;
    lastAddressDebug.indexValue = Y;
    lastAddressDebug.indexedZP = zpAddress;
    lastAddressDebug.pointerLowAddr = lowAddr;
    lastAddressDebug.pointerHighAddr = highAddr;
    lastAddressDebug.pointerLowValue = lowByte;
    lastAddressDebug.pointerHighValue = highByte;
    lastAddressDebug.baseAddress = baseAddress;
    lastAddressDebug.effectiveAddress = effectiveAddress;
    lastAddressDebug.pageCrossed = false;
    lastAddressDebug.dummyReadUsed = false;
    lastAddressDebug.dummyReadAddress = 0;
    lastAddressDebug.valueRead = 0;
    lastAddressDebug.totalCycles = totalCycles;

    return effectiveAddress;
}

uint16_t CPU::zpAddress()
{
    uint16_t address = fetchOperand();
    return address;
}

uint16_t CPU::zpXAddress()
{
    const uint8_t base = fetchOperand();

    // Zero-page indexed dummy read before applying X.
    cpuRead(base, CpuBusCycleType::DummyRead);

    return uint16_t((base + X) & 0xFF);
}

uint16_t CPU::zpYAddress()
{
    const uint8_t base = fetchOperand();

    // Zero-page indexed dummy read before applying Y.
    cpuRead(base, CpuBusCycleType::DummyRead);

    return uint16_t((base + Y) & 0xFF);
}

CPU::ReadByte CPU::readABSXAddressBoundary()
{
    const uint16_t baseAddress =
        uint16_t(fetchOperand()) |
        uint16_t(fetchOperand() << 8);

    const uint16_t effectiveAddress = uint16_t(baseAddress + X);

    const bool crossed =
        (baseAddress & 0xFF00) != (effectiveAddress & 0xFF00);

    if (crossed)
    {
        // Dummy read from old high byte + indexed low byte.
        const uint16_t dummy =
            uint16_t((baseAddress & 0xFF00) |
                     ((baseAddress + X) & 0x00FF));

        cpuRead(dummy, CpuBusCycleType::DummyRead);
    }

    const uint8_t value =
        cpuRead(effectiveAddress, CpuBusCycleType::Read);

    return { value, crossed };
}

CPU::ReadByte CPU::readABSYAddressBoundary()
{
    const uint16_t baseAddress =
        uint16_t(fetchOperand()) |
        uint16_t(fetchOperand() << 8);

    const uint16_t effectiveAddress = uint16_t(baseAddress + Y);

    const bool crossed =
        (baseAddress & 0xFF00) != (effectiveAddress & 0xFF00);

    if (crossed)
    {
        // Dummy read from old high byte + indexed low byte.
        const uint16_t dummy =
            uint16_t((baseAddress & 0xFF00) |
                     ((baseAddress + Y) & 0x00FF));

        cpuRead(dummy, CpuBusCycleType::DummyRead);
    }

    const uint8_t value =
        cpuRead(effectiveAddress, CpuBusCycleType::Read);

    return { value, crossed };
}

CPU::ReadByte CPU::readIndirectYAddressBoundary()
{
    const uint16_t operandPC = PC;
    const uint8_t zpAddress = fetchOperand();

    const uint8_t lowAddr  = zpAddress;
    const uint8_t highAddr = uint8_t(zpAddress + 1);

    const uint8_t lowByte =
        cpuRead(lowAddr, CpuBusCycleType::Read);

    const uint8_t highByte =
        cpuRead(highAddr, CpuBusCycleType::Read);

    const uint16_t baseAddress =
        uint16_t(lowByte) | (uint16_t(highByte) << 8);

    const uint16_t effectiveAddress =
        uint16_t(baseAddress + Y);

    const bool crossed =
        (baseAddress & 0xFF00) != (effectiveAddress & 0xFF00);

    uint16_t dummy = 0;

    if (crossed)
    {
        // Dummy read from old high byte + indexed low byte.
        dummy = uint16_t((baseAddress & 0xFF00) |
                         (effectiveAddress & 0x00FF));

        cpuRead(dummy, CpuBusCycleType::DummyRead);
    }

    const uint8_t value =
        cpuRead(effectiveAddress, CpuBusCycleType::Read);

    lastAddressDebug.valid = true;
    lastAddressDebug.mode = CPUAddressDebugMode::IndirectYBoundary;
    lastAddressDebug.operandPC = operandPC;
    lastAddressDebug.zpOperand = zpAddress;
    lastAddressDebug.indexValue = Y;
    lastAddressDebug.indexedZP = zpAddress;
    lastAddressDebug.pointerLowAddr = lowAddr;
    lastAddressDebug.pointerHighAddr = highAddr;
    lastAddressDebug.pointerLowValue = lowByte;
    lastAddressDebug.pointerHighValue = highByte;
    lastAddressDebug.baseAddress = baseAddress;
    lastAddressDebug.effectiveAddress = effectiveAddress;
    lastAddressDebug.pageCrossed = crossed;
    lastAddressDebug.dummyReadUsed = crossed;
    lastAddressDebug.dummyReadAddress = dummy;
    lastAddressDebug.valueRead = value;
    lastAddressDebug.totalCycles = totalCycles;

    return { value, crossed };
}

void CPU::branchIf(bool condition, const char* mnemonic, uint8_t opcode)
{
    const uint16_t opcodePC = uint16_t(PC - 1); // opcode already fetched
    const uint16_t operandPC = PC;

    const int8_t offset = static_cast<int8_t>(fetchOperand());

    lastBranch.valid = true;
    lastBranch.opcodePC = opcodePC;
    lastBranch.opcode = opcode;
    lastBranch.mnemonic = mnemonic;
    lastBranch.condition = condition;
    lastBranch.taken = false;
    lastBranch.offset = offset;
    lastBranch.operandPC = operandPC;
    lastBranch.oldPC = PC;
    lastBranch.newPC = PC;
    lastBranch.pageCrossed = false;
    lastBranch.takenDummyRead = 0;
    lastBranch.pageCrossDummyRead = 0;
    lastBranch.extraCycles = 0;
    lastBranch.totalCycles = totalCycles;

    if (!condition)
    {
        if (traceMgr)
        {
            std::ostringstream oss;
            oss << mnemonic << " not taken at PC=$"
                << std::hex << std::uppercase << std::setw(4)
                << std::setfill('0') << opcodePC
                << " offset=" << std::dec << int(offset);
            traceMgr->recordCPUExec(opcodePC, opcode, makeCpuStamp());
        }

        return;
    }

    // Branch taken: extra cycle and dummy read of next opcode address.
    cycles++;

    const uint16_t takenDummy = PC;
    cpuRead(takenDummy, CpuBusCycleType::DummyRead);

    const uint16_t oldPC = PC;
    const uint16_t newPC = uint16_t(PC + offset);

    bool crossed = false;
    uint16_t pageDummy = 0;
    uint8_t extraCycles = 1;

    if ((oldPC & 0xFF00) != (newPC & 0xFF00))
    {
        // Page-cross dummy read from old high byte + new low byte.
        pageDummy = uint16_t((oldPC & 0xFF00) | (newPC & 0x00FF));
        cpuRead(pageDummy, CpuBusCycleType::DummyRead);

        cycles++;
        extraCycles++;
        crossed = true;
    }

    PC = newPC;

    lastBranch.taken = true;
    lastBranch.oldPC = oldPC;
    lastBranch.newPC = newPC;
    lastBranch.pageCrossed = crossed;
    lastBranch.takenDummyRead = takenDummy;
    lastBranch.pageCrossDummyRead = pageDummy;
    lastBranch.extraCycles = extraCycles;
    lastBranch.totalCycles = totalCycles;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << mnemonic << " taken at PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << opcodePC
            << " from=$" << std::setw(4) << oldPC
            << " to=$" << std::setw(4) << newPC
            << " offset=" << std::dec << int(offset)
            << " pageCross=" << (crossed ? "yes" : "no")
            << " extraCycles=" << int(extraCycles);

        traceMgr->recordCPUExec(opcodePC, opcode, makeCpuStamp());
    }
}

void CPU::dummyReadWrongPageABSX(uint16_t address)
{
    const uint16_t base = uint16_t(address - X);

    // Absolute,X RMW dummy/read cycle.
    // If page crossed, this is old high byte + indexed low byte.
    // If not crossed, this equals the final effective address.
    const uint16_t dummy = uint16_t((base & 0xFF00) | (address & 0x00FF));

    cpuRead(dummy, CpuBusCycleType::DummyRead);
}

void CPU::dummyReadWrongPageABSY(uint16_t address)
{
    const uint16_t base = uint16_t(address - Y);
    const uint16_t dummy = uint16_t((base & 0xFF00) | (address & 0x00FF));

    cpuRead(dummy, CpuBusCycleType::DummyRead);
}

void CPU::dummyReadWrongPageINDY(uint16_t address)
{
    const uint16_t base = uint16_t(address - Y);
    const uint16_t dummy = uint16_t((base & 0xFF00) | (address & 0x00FF));

    cpuRead(dummy, CpuBusCycleType::DummyRead);
}

void CPU::rmwWrite(uint16_t address, uint8_t oldValue, uint8_t newValue)
{
    // RMW instructions perform a dummy write of the old value first.
    cpuWrite(address, oldValue, CpuBusCycleType::DummyWrite);

    // Then the modified value is written.
    cpuWrite(address, newValue, CpuBusCycleType::Write);
}

void CPU::tick()
{
    if (halted) // Jam Halt
    {
        if (traceMgr)
            traceMgr->recordCPUJam("CPU halted/jammed", makeCpuStamp());

        cycles = 1;
        cycles--;
        totalCycles++;
        return;
    }

    //if (useMicroOpsForTest && tickMicroOps())
        //return;

    if (cycles <= 0)
    {
        handleNMI();
        handleIRQ();

        if (cycles <= 0)
        {
            const uint16_t pcExec = PC;

            const uint8_t opcode = fetchOpcode();

            lastOpcodePC = pcExec;
            lastOpcode = opcode;

            if (logger && setLogging)
            {
                std::stringstream message;
                message << "PC = " << std::hex << static_cast<int>(PC - 1)
                        << ", OPCODE = " << std::hex << static_cast<int>(opcode)
                        << ", A = " << std::hex << static_cast<int>(A)
                        << ", X = " << std::hex << static_cast<int>(X)
                        << ", Y= " << std::hex << static_cast<int>(Y)
                        << ", SP = " << std::hex << static_cast<int>(SP);

                logger->WriteLog(message.str());
            }

            if (useMicroOpsForTest && canExecuteOpcodeWithMicroOps(opcode))
            {
                activeOpcode = opcode;
                activeOpcodePC = pcExec;

                buildMicroOpsForOpcode(opcode);

                // First hybrid stage:
                // Run the converted instruction body immediately,
                // but still use the existing instruction-level cycle table.
                while (microOpIndex < microOpCount)
                {
                    executeCurrentMicroOp();
                }

                clearMicroOps();

                if (traceMgr && traceMgr->isEnabled() &&
                    traceMgr->catOn(TraceManager::TraceCat::CPU) &&
                    traceMgr->cpuDetailOn(TraceManager::TraceDetail::CPU_EXEC))
                {
                    traceMgr->recordCPUExec(
                        pcExec,
                        opcode,
                        traceMgr->makeStamp(
                            totalCycles,
                            vic ? vic->getCurrentRaster() : 0,
                            vic ? vic->getRasterDot() : 0
                        )
                    );
                }

                cycles += CYCLE_COUNTS[opcode];
            }
            else
            {
                decodeAndExecute(opcode);

                if (traceMgr && traceMgr->isEnabled() &&
                    traceMgr->catOn(TraceManager::TraceCat::CPU) &&
                    traceMgr->cpuDetailOn(TraceManager::TraceDetail::CPU_EXEC))
                {
                    traceMgr->recordCPUExec(
                        pcExec,
                        opcode,
                        traceMgr->makeStamp(
                            totalCycles,
                            vic ? vic->getCurrentRaster() : 0,
                            vic ? vic->getRasterDot() : 0
                        )
                    );
                }

                cycles += CYCLE_COUNTS[opcode];
            }
        }
    }

    cycles--;
    totalCycles++;
}

uint32_t CPU::getElapsedCycles()
{
    elapsedCycles = totalCycles - lastCycleCount;
    lastCycleCount = totalCycles; // Update lastCycleCount for the next call
    return elapsedCycles;
}

uint8_t CPU::fetchOpcode()
{
    const uint8_t byte = cpuRead(PC, CpuBusCycleType::OpcodeFetch);
    PC = uint16_t((PC + 1) & 0xFFFF);
    return byte;
}

uint8_t CPU::fetchOperand()
{
    const uint8_t byte = cpuRead(PC, CpuBusCycleType::Read);
    PC = uint16_t((PC + 1) & 0xFFFF);
    return byte;
}

void CPU::decodeAndExecute(uint8_t opcode)
{
    if (opcodeTable[opcode])
    {
        opcodeTable[opcode]();
    }
    else
    {
        if (logger && setLogging)
        {
            logger->WriteLog("Unhandled opcode: " + std::to_string(opcode));
        }
    }
}

void CPU::setFlag(flags flag, bool sc)
{
    if (sc)
		SR |= flag;
	else
		SR &= ~flag;
}

void CPU::push(uint8_t value)
{
    const uint16_t address = uint16_t(0x0100 | SP);

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "PUSH addr=$"
            << std::hex << std::uppercase
            << std::setw(4) << std::setfill('0') << address
            << " value=$" << std::setw(2) << int(value)
            << " SP=$" << std::setw(2) << int(SP);

        traceMgr->recordCPUStack(oss.str(), makeCpuStamp());
    }

    cpuWrite(address, value, CpuBusCycleType::StackWrite);

    SP = uint8_t((SP - 1) & 0xFF);
}

uint8_t CPU::pop()
{
    SP = uint8_t((SP + 1) & 0xFF);

    const uint16_t address = uint16_t(0x0100 | SP);

    const uint8_t value = cpuRead(address, CpuBusCycleType::StackRead);

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "POP addr=$"
            << std::hex << std::uppercase
            << std::setw(4) << std::setfill('0') << address
            << " value=$" << std::setw(2) << int(value)
            << " SP=$" << std::setw(2) << int(SP);

        traceMgr->recordCPUStack(oss.str(), makeCpuStamp());
    }

    return value;
}

//OPCODES implemented
void CPU::AAC()
{
    uint8_t value = fetchOperand(); // Fetch the immediate value
    A &= value; // Perform AND operation on accumulator

    // Update flags
    setFlag(C, A & 0x80);
    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::ADC(uint8_t opcode)
{
    uint8_t value = 0;

    // Fetch operand by addressing mode
    switch (opcode) {
        case 0x61: value = readIndirectX(); break;
        case 0x65: value = readZP();        break;
        case 0x69: value = readImmediate(); break;
        case 0x6D: value = readABS();       break;
        case 0x71:
        {
            auto ret = readIndirectYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
        case 0x75: value = readZPX();       break;
        case 0x79:
        {
            auto ret = readABSYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
        case 0x7D:
        {
            auto ret = readABSXAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
    }

    const uint8_t a0  = A;
    const uint8_t cIn = getFlag(C) ? 1 : 0;

    // Binary sum (used for V and for non-BCD path)
    uint16_t sum  = uint16_t(a0) + uint16_t(value) + cIn; // 0..0x1FE
    uint8_t  bin8 = uint8_t(sum);

    // V is from binary addition even in decimal mode
    setFlag(V, ((~(a0 ^ value) & (a0 ^ bin8)) & 0x80) != 0);

    if (getFlag(D))
    {
        uint16_t adj = sum;

        if (((a0 & 0x0F) + (value & 0x0F) + cIn) > 9)
            adj += 0x06;

        if (adj > 0x99)
        {
            adj += 0x60;
            setFlag(C, true);
        }
        else
        {
            setFlag(C, false);
        }

        A = uint8_t(adj);

        // Important NMOS decimal-mode quirk:
        // N/Z come from the pre-adjust binary result.
        setFlag(Z, bin8 == 0);
        setFlag(N, (bin8 & 0x80) != 0);
    }
    else
    {
        // Pure binary
        A = bin8;
        setFlag(C, sum > 0xFF);

        setFlag(Z, A == 0);
        setFlag(N, (A & 0x80) != 0);
    }
}

void CPU::AHX(uint8_t opcode)
{
    uint16_t base = 0;
    uint16_t address = 0;

    switch (opcode)
    {
        case 0x93: // AHX (zp),Y
        {
            const uint8_t zp = fetchOperand();

            const uint8_t lo =
                cpuRead(zp, CpuBusCycleType::Read);

            const uint8_t hi =
                cpuRead(uint8_t(zp + 1), CpuBusCycleType::Read);

            base = uint16_t(lo) | (uint16_t(hi) << 8);
            address = uint16_t(base + Y);

            // Indexed-store dummy read: uncorrected high byte + indexed low byte.
            const uint16_t dummy =
                uint16_t((base & 0xFF00) | (address & 0x00FF));

            cpuRead(dummy, CpuBusCycleType::DummyRead);
            break;
        }

        case 0x9F: // AHX abs,Y
        {
            const uint8_t lo = fetchOperand();
            const uint8_t hi = fetchOperand();

            base = uint16_t(lo) | (uint16_t(hi) << 8);
            address = uint16_t(base + Y);

            // Indexed-store dummy read: uncorrected high byte + indexed low byte.
            const uint16_t dummy =
                uint16_t((base & 0xFF00) | (address & 0x00FF));

            cpuRead(dummy, CpuBusCycleType::DummyRead);
            break;
        }

        default:
            return;
    }

    const uint8_t value =
        uint8_t((A & X) & uint8_t((base >> 8) + 1));

    cpuWrite(address, value, CpuBusCycleType::Write);
}

void CPU::ALR()
{
    uint8_t value = readImmediate();
    A &= value;
    setFlag(C, A & 0x01);
    A >>= 1;
    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::AND(uint8_t opcode)
{
    uint8_t value = 0;

    switch(opcode)
    {
        case 0x21: value = readIndirectX(); break;
        case 0x25: value = readZP(); break;
        case 0x29: value = readImmediate(); break;
        case 0x2D: value = readABS(); break;
        case 0x31:
        {
            auto ret = readIndirectYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
        case 0x35: value = readZPX(); break;
        case 0x39:
        {
            auto ret = readABSYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
        case 0x3D:
        {
            auto ret = readABSXAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
    }
    A &= value;
    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::ARR()
{
    uint8_t imm = readImmediate();

    // AND then ROR
    A &= imm;
    bool c_in = getFlag(C);
    uint8_t ror = (A >> 1) | (c_in ? 0x80 : 0);

    if (!getFlag(D))
    {
        // Binary mode
        A = ror;
        setFlag(C, (A >> 6) & 1);
        setFlag(V, ((A >> 6) & 1) ^ ((A >> 5) & 1));
    }
    else
    {
        // Decimal mode (unofficial / unstable)
        uint8_t r = ror;
        uint8_t lo = r & 0x0F;
        uint8_t hi = r & 0xF0;

        bool carry = false;
        if (lo > 0x09) r += 0x06;
        if (hi > 0x90 || r > 0x99)
        {
            r += 0x60;
            carry = true;
        }

        setFlag(C, carry);
        setFlag(V, ((ror >> 6) & 1) ^ ((ror >> 5) & 1));
        A = r;
    }

    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::ASL(uint8_t opcode)
{
    if (opcode == 0x0A) // Accumulator
    {
        uint8_t value = A;
        uint8_t result = value << 1;
        A = result;

        // Update flags
        setFlag(C, (value & 0x80) != 0);  // Carry flag: Bit 7 of the original value
        setFlag(Z, result == 0);   // Zero flag: Result is zero
        setFlag(N, (result & 0x80) != 0); // Negative flag: Bit 7 of the result
        return;
    }

    uint16_t address = 0;

    switch(opcode)
    {
        case 0x06: address = zpAddress(); break;
        case 0x0E: address = absAddress(); break;
        case 0x16: address = zpXAddress(); break;
        case 0x1E: address = absXAddress(); dummyReadWrongPageABSX(address); break;
    }

    uint8_t oldValue = cpuRead(address, CpuBusCycleType::Read);
    uint8_t newValue = uint8_t(oldValue << 1);
    rmwWrite(address, oldValue, newValue);

    // Update flags
    setFlag(C, (oldValue & 0x80) != 0);  // Carry flag: Bit 7 of the original value
    setFlag(Z, newValue == 0);   // Zero flag: Result is zero
    setFlag(N, (newValue & 0x80) != 0); // Negative flag: Bit 7 of the result
}

void CPU::AXS()
{
    uint8_t value = readImmediate(); // Fetch the immediate value
    uint8_t andResult = A & X; // Perform AND between A and X
    int16_t temp = andResult - value; // Subtract the immediate value

    // Update X with the result (handle wrapping)
    X = temp & 0xFF;

    // Set flags
    setFlag(C, temp >= 0); // Set carry if no borrow
    setFlag(Z, X == 0);    // Set zero if result is zero
    setFlag(N, X & 0x80);  // Set negative if MSB is set
}

void CPU::BCC()
{
    branchIf(!getFlag(C), "BCC", 0x90);
}

void CPU::BCS()
{
    branchIf(getFlag(C), "BCS", 0xB0);
}

void CPU::BEQ()
{
    branchIf(getFlag(Z), "BEQ", 0xF0);
}

void CPU::BIT(uint8_t opcode)
{
    uint8_t value = 0;

    switch(opcode)
    {
        case 0x24: value = readZP(); break;
        case 0x2C: value = readABS(); break;
    }
    uint8_t result = A & value;
    setFlag(Z, result == 0);
    setFlag(N, value & 0x80);
    setFlag(V, value & 0x40);
}

void CPU::BMI()
{
    branchIf(getFlag(N), "BMI", 0x30);
}

void CPU::BNE()
{
    branchIf(!getFlag(Z), "BNE", 0xD0);
}

void CPU::BPL()
{
    branchIf(!getFlag(N), "BPL", 0x10);
}

void CPU::BRK()
{
    const uint16_t brkPC = uint16_t(PC - 1); // opcode address, since opcode fetch already advanced PC
    const uint16_t newPC = uint16_t(PC + 1); // BRK is treated as a 2-byte instruction
    const uint8_t spBefore = SP;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "BRK accepted at PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << brkPC
            << " return=$" << std::setw(4) << newPC
            << " SP=$" << std::setw(2) << int(spBefore);
        traceMgr->recordCPUIRQ(oss.str(), makeCpuStamp());
    }

    // BRK consumes the padding/signature byte as a dummy read.
    cpuRead(PC, CpuBusCycleType::DummyRead);

    push((newPC >> 8) & 0xFF);
    push(newPC & 0xFF);

    const uint8_t pushedStatus = SR | 0x30; // B=1, U=1

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "BRK pushed SR=$"
            << std::hex << std::uppercase << std::setw(2)
            << std::setfill('0') << int(pushedStatus);
        traceMgr->recordCPUIRQ(oss.str(), makeCpuStamp());
    }

    push(pushedStatus);

    const uint8_t spAfter = SP;

    // Set interrupt disable
    setFlag(I, true);

    const uint8_t vectorLo = cpuRead(0xFFFE, CpuBusCycleType::Read);
    const uint8_t vectorHi = cpuRead(0xFFFF, CpuBusCycleType::Read);
    const uint16_t vector = uint16_t(vectorLo) | (uint16_t(vectorHi) << 8);
    PC = vector;

    lastInterruptEntry.type = InterruptEntryType::BRK;
    lastInterruptEntry.acceptedAtPC = brkPC;
    lastInterruptEntry.pushedReturnPC = newPC;
    lastInterruptEntry.pushedSR = pushedStatus;
    lastInterruptEntry.spBefore = spBefore;
    lastInterruptEntry.spAfter = spAfter;
    lastInterruptEntry.vectorAddress = 0xFFFE;
    lastInterruptEntry.vectorTarget = vector;
    lastInterruptEntry.totalCycles = totalCycles;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "BRK vector -> PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << vector
            << " SP $" << std::setw(2) << int(spBefore)
            << "->$" << std::setw(2) << int(spAfter);
        traceMgr->recordCPUIRQ(oss.str(), makeCpuStamp());
    }
}

void CPU::BVC()
{
    branchIf(!getFlag(V), "BVC", 0x50);
}

void CPU::BVS()
{
    branchIf(getFlag(V), "BVS", 0x70);
}

void CPU::CMP(uint8_t opcode)
{
    uint8_t value = 0;

    switch(opcode)
    {
        case 0xC1: value = readIndirectX(); break;
        case 0xC5: value = readZP(); break;
        case 0xC9: value = readImmediate(); break;
        case 0xCD: value = readABS(); break;
        case 0xD1:
        {
            auto ret = readIndirectYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
        case 0xD5: value = readZPX(); break;
        case 0xD9:
        {
            auto ret = readABSYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
        case 0xDD:
        {
            auto ret = readABSXAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
    }

    uint8_t result = A - value;
    setFlag(Z, result == 0);
    setFlag(N, result & 0x80);
    setFlag(C, A >= value);
}

void CPU::CPX(uint8_t opcode)
{
    uint8_t value = 0;

    switch(opcode)
    {
        case 0xE0: value = readImmediate(); break;
        case 0xE4: value = readZP(); break;
        case 0xEC: value = readABS(); break;
    }

    uint8_t result = X - value;
    setFlag(Z, result == 0);
    setFlag(N, result & 0x80);
    setFlag(C, X >= value);
}

void CPU::CPY(uint8_t opcode)
{
    uint8_t value = 0;

    switch(opcode)
    {
        case 0xC0: value = readImmediate(); break;
        case 0xC4: value = readZP(); break;
        case 0xCC: value = readABS(); break;
    }

    uint8_t result = Y - value;
    setFlag(Z, result == 0);
    setFlag(N, result & 0x80);
    setFlag(C, Y >= value);
}

void CPU::DCP(uint8_t opcode)
{
    uint16_t address = 0;

    switch (opcode)
    {
        case 0xC3: address = indirectXAddress(); break;
        case 0xC7: address = zpAddress(); break;
        case 0xCF: address = absAddress(); break;
        case 0xD3: address = indirectYAddress(); dummyReadWrongPageINDY(address); break;
        case 0xD7: address = zpXAddress(); break;
        case 0xDB: address = absYAddress(); dummyReadWrongPageABSY(address); break;
        case 0xDF: address = absXAddress(); dummyReadWrongPageABSX(address); break;
    }

    // Decrement memory value
    uint8_t oldValue = cpuRead(address, CpuBusCycleType::Read);
    uint8_t newValue = uint8_t(oldValue - 1);
    rmwWrite(address, oldValue, newValue);

    uint8_t diff = uint8_t(A - newValue);

    // Update flags
    setFlag(C, A >= newValue);
    setFlag(Z, diff == 0);
    setFlag(N, diff & 0x80);
}

void CPU::DEC(uint8_t opcode)
{
    uint16_t address = 0;

    switch(opcode)
    {
        case 0xC6: address = zpAddress(); break;
        case 0xCE: address = absAddress(); break;
        case 0xD6: address = zpXAddress(); break;
        case 0xDE: address = absXAddress(); dummyReadWrongPageABSX(address); break;
    }

    uint8_t oldValue = cpuRead(address, CpuBusCycleType::Read);
    uint8_t newValue = uint8_t(oldValue - 1);
    rmwWrite(address, oldValue, newValue);
    setFlag(Z, newValue == 0);
    setFlag(N, newValue & 0x80);
}

void CPU::DEX()
{
    X--;
    setFlag(Z, X == 0);
    setFlag(N, X & 0x80);
}

void CPU::DEY()
{
    Y--;
    setFlag(Z, Y == 0);
    setFlag(N, Y & 0x80);
}

void CPU::EOR(uint8_t opcode)
{
     uint8_t value = 0;

    switch(opcode)
    {
        case 0x41: value = readIndirectX(); break;
        case 0x45: value = readZP(); break;
        case 0x49: value = readImmediate(); break;
        case 0x4D:  value = readABS(); break;
        case 0x51:
        {
            auto ret = readIndirectYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
        case 0x55: value = readZPX(); break;
        case 0x59:
        {
            auto ret = readABSYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
        case 0x5D:
        {
            auto ret = readABSXAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
    }
    A ^= value;
    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::INC(uint8_t opcode)
{
    uint16_t address = 0;

    switch(opcode)
    {
        case 0xE6: address = zpAddress(); break;
        case 0xEE: address = absAddress(); break;
        case 0xF6: address = zpXAddress(); break;
        case 0xFE: address = absXAddress(); dummyReadWrongPageABSX(address); break;
    }

    uint8_t oldValue = cpuRead(address, CpuBusCycleType::Read);
    uint8_t newValue = uint8_t(oldValue + 1);
    rmwWrite(address, oldValue, newValue);
    setFlag(Z, newValue == 0);
    setFlag(N, newValue & 0x80);
}

void CPU::INX()
{
    X = (X + 1) & 0xFF;
    setFlag(Z, X == 0);
    setFlag(N, X & 0x80);
}

void CPU::INY()
{
    Y = (Y + 1) & 0xFF;
    setFlag(Z, Y == 0);
    setFlag(N, Y & 0x80);
}

void CPU::ISC(uint8_t opcode)
{
    uint16_t address = 0;

    switch (opcode)
    {
        case 0xE3: address = indirectXAddress(); break;
        case 0xE7: address = zpAddress(); break;
        case 0xEF: address = absAddress(); break;
        case 0xF3: address = indirectYAddress(); dummyReadWrongPageINDY(address); break;
        case 0xF7: address = zpXAddress(); break;
        case 0xFB: address = absYAddress(); dummyReadWrongPageABSY(address); break;
        case 0xFF: address = absXAddress(); dummyReadWrongPageABSX(address); break;
    }

    uint8_t oldValue = cpuRead(address, CpuBusCycleType::Read);
    uint8_t newValue = uint8_t(oldValue + 1);
    rmwWrite(address, oldValue, newValue);

    const uint8_t a0 = A;
    const uint8_t m  = newValue;
    const uint16_t tmp = uint16_t(a0) - uint16_t(m) - (1 - (getFlag(C) ? 1 : 0));

    A = uint8_t(tmp);

    // Flags
    setFlag(C, tmp < 0x100);
    setFlag(Z, A == 0);
    setFlag(N, (A & 0x80) != 0);
    const uint8_t res8 = uint8_t(tmp);
    setFlag(V, ((a0 ^ m) & (a0 ^ res8) & 0x80) != 0);
}

void CPU::JAM()
{
    switch (jamMode)
    {
        case JamMode::Halt:
            // Strict NMOS behavior: CPU stops executing
            halted = true;
            break;

        case JamMode::FreezePC:
            // Stay stuck on this opcode forever.
            // Decrement PC so next fetch will re-read the JAM.
            PC = (PC - 1) & 0xFFFF;
            break;

        case JamMode::NopCompat:
            // Compatibility hack: do nothing (acts like a NOP).
            // PC already incremented by fetchOperand().
            break;
    }
}

void CPU::JMP(uint8_t opcode)
{
    const uint16_t jmpPC = uint16_t(PC - 1); // opcode address, since opcode fetch already advanced PC

    switch (opcode)
    {
        case 0x4C: // JMP abs
        {
            const uint16_t operandAddress = PC;

            const uint8_t lowByte  = fetchOperand();
            const uint8_t highByte = fetchOperand();

            const uint16_t address =
                uint16_t(lowByte) | (uint16_t(highByte) << 8);

            PC = address;

            lastJMP.valid = true;
            lastJMP.jmpOpcodePC = jmpPC;
            lastJMP.opcode = opcode;
            lastJMP.indirect = false;
            lastJMP.operandAddress = operandAddress;
            lastJMP.pointerAddress = 0;
            lastJMP.lowReadAddress = operandAddress;
            lastJMP.highReadAddress = uint16_t(operandAddress + 1);
            lastJMP.lowByte = lowByte;
            lastJMP.highByte = highByte;
            lastJMP.indirectPageBug = false;
            lastJMP.finalPC = PC;
            lastJMP.totalCycles = totalCycles;

            if (traceMgr)
            {
                std::ostringstream oss;
                oss << "JMP abs at PC=$"
                    << std::hex << std::uppercase << std::setw(4)
                    << std::setfill('0') << jmpPC
                    << " target=$" << std::setw(4) << PC;

                traceMgr->recordCPUStack(oss.str(), makeCpuStamp());
            }

            break;
        }

        case 0x6C: // JMP (indirect)
        {
            const uint16_t operandAddress = PC;

            const uint8_t ptrLow  = fetchOperand();
            const uint8_t ptrHigh = fetchOperand();

            const uint16_t pointer =
                uint16_t(ptrLow) | (uint16_t(ptrHigh) << 8);

            const uint16_t lowReadAddress = pointer;

            // NMOS 6502/6510 indirect JMP page-boundary bug:
            // if pointer is $xxFF, high byte is read from $xx00, not $(xx+1)00.
            const uint16_t highReadAddress =
                uint16_t((pointer & 0xFF00) | ((pointer + 1) & 0x00FF));

            const uint8_t lowByte =
                cpuRead(lowReadAddress, CpuBusCycleType::Read);

            const uint8_t highByte =
                cpuRead(highReadAddress, CpuBusCycleType::Read);

            PC = uint16_t(lowByte) | (uint16_t(highByte) << 8);

            lastJMP.valid = true;
            lastJMP.jmpOpcodePC = jmpPC;
            lastJMP.opcode = opcode;
            lastJMP.indirect = true;
            lastJMP.operandAddress = operandAddress;
            lastJMP.pointerAddress = pointer;
            lastJMP.lowReadAddress = lowReadAddress;
            lastJMP.highReadAddress = highReadAddress;
            lastJMP.lowByte = lowByte;
            lastJMP.highByte = highByte;
            lastJMP.indirectPageBug = ((pointer & 0x00FF) == 0x00FF);
            lastJMP.finalPC = PC;
            lastJMP.totalCycles = totalCycles;

            if (traceMgr)
            {
                std::ostringstream oss;
                oss << "JMP indirect at PC=$"
                    << std::hex << std::uppercase << std::setw(4)
                    << std::setfill('0') << jmpPC
                    << " pointer=$" << std::setw(4) << pointer
                    << " lo@$" << std::setw(4) << lowReadAddress
                    << " hi@$" << std::setw(4) << highReadAddress
                    << " final=$" << std::setw(4) << PC
                    << " pageBug=" << (lastJMP.indirectPageBug ? "yes" : "no");

                traceMgr->recordCPUStack(oss.str(), makeCpuStamp());
            }

            break;
        }

        default:
            break;
    }
}

void CPU::JSR()
{
    const uint16_t jsrPC = uint16_t(PC - 1); // opcode address, since opcode fetch already advanced PC
    const uint8_t spBefore = SP;

    // PC currently points to the low byte of the JSR target
    // because opcode $20 has already been fetched by tick().
    const uint8_t lo = fetchOperand();

    // JSR internal/stack timing read.
    // After fetching low target byte, PC points to the high target byte.
    cpuRead(uint16_t(0x0100 | SP), CpuBusCycleType::DummyRead);

    const uint8_t hi = fetchOperand();

    // 6502 pushes address of the last operand byte.
    // After fetching hi, PC points to next instruction, so return is PC - 1.
    const uint16_t returnAddress = uint16_t(PC - 1);

    const uint8_t returnHigh = uint8_t((returnAddress >> 8) & 0xFF);
    const uint8_t returnLow  = uint8_t(returnAddress & 0xFF);

    push(returnHigh);
    push(returnLow);

    const uint8_t spAfter = SP;

    const uint16_t target = uint16_t(lo) | (uint16_t(hi) << 8);
    PC = target;

    lastJSR.valid = true;
    lastJSR.jsrOpcodePC = jsrPC;
    lastJSR.targetPC = target;
    lastJSR.pushedReturn = returnAddress;
    lastJSR.pushedHigh = returnHigh;
    lastJSR.pushedLow = returnLow;
    lastJSR.spBefore = spBefore;
    lastJSR.spAfter = spAfter;
    lastJSR.totalCycles = totalCycles;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "JSR at PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << jsrPC
            << " target=$" << std::setw(4) << target
            << " pushed return=$" << std::setw(4) << returnAddress
            << " SP $" << std::setw(2) << int(spBefore)
            << "->$" << std::setw(2) << int(spAfter);

        traceMgr->recordCPUStack(oss.str(), makeCpuStamp());
    }
}

void CPU::LAS()
{
    auto ret = readABSYAddressBoundary();
    addPageCrossIf(ret.crossed);
    uint8_t result = ret.value & SP;
    // Update A, X, and SP with the result
    A = result;
    X = result;
    SP = result;

    // Set flags
    setFlag(Z, result == 0);
    setFlag(N, result & 0x80);
}

void CPU::LAX(uint8_t opcode)
{
    uint8_t value = 0;

    switch (opcode)
    {
        case 0xA3: value = readIndirectX(); break;
        case 0xA7: value = readZP(); break;
        case 0xAB:
        {
            value = readImmediate();
            uint8_t result = A & value;
            A = result;
            X = result;
            setFlag(Z, result == 0);
            setFlag(N, result & 0x80);
            return;
        }
        case 0xAF: value = readABS(); break;
        case 0xB3:
        {
            auto ret = readIndirectYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }

        case 0xB7: value = readZPY(); break;
        case 0xBF:
        {
            auto ret = readABSYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }

    }
    // Load value from memory into A and X
    A = value;
    X = value;

    // Update flags
    setFlag(Z, value == 0);
    setFlag(N, value & 0x80);
}

void CPU::LDA(uint8_t opcode)
{
    uint8_t value = 0;

    switch (opcode)
    {
        case 0xA1: value = readIndirectX(); break;
        case 0xA5: value = readZP(); break;
        case 0xA9: value = readImmediate(); break;
        case 0xAD: value = readABS();  break;
        case 0xB1:
        {
            auto ret = readIndirectYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
        case 0xB5: value = readZPX(); break;
        case 0xB9:
        {
            auto ret = readABSYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
        case 0xBD:
        {
            auto ret = readABSXAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
    }

    // Store the value in the accumulator
    A = value;

    // Set flags
    setFlag(Z, A == 0);   // Zero flag
    setFlag(N, A & 0x80); // Negative flag
}

void CPU::LDX(uint8_t opcode)
{
    switch(opcode)
    {
        case 0xA2: X = readImmediate(); break;
        case 0xA6: X = readZP(); break;
        case 0xAE: X = readABS(); break;
        case 0xB6: X = readZPY(); break;
        case 0xBE:
        {
            auto ret = readABSYAddressBoundary();
            addPageCrossIf(ret.crossed);
            X = ret.value;
            break;
        }
    }
    setFlag(Z, X == 0);
    setFlag(N, X & 0x80);
}

void CPU::LDY(uint8_t opcode)
{
    switch(opcode)
    {
        case 0xA0: Y = readImmediate(); break;
        case 0xA4: Y = readZP(); break;
        case 0xAC: Y = readABS(); break;
        case 0xB4: Y = readZPX(); break;
        case 0xBC:
        {
            auto ret = readABSXAddressBoundary();
            addPageCrossIf(ret.crossed);
            Y = ret.value;
            break;
        }
    }
    setFlag(Z, Y == 0);
    setFlag(N, Y & 0x80);
}

void CPU::LSR(uint8_t opcode)
{
    if (opcode == 0x4A)
    {
        bool carry = A & 0x01;
            A >>= 1;

            setFlag(C, carry);
            setFlag(Z, A == 0);
            setFlag(N, false);
            return;
    }

    uint16_t address = 0;

    switch(opcode)
    {
        case 0x46: address = zpAddress(); break;
        case 0x4E: address = absAddress(); break;
        case 0x56: address = zpXAddress(); break;
        case 0x5E: address = absXAddress(); dummyReadWrongPageABSX(address); break;
    }

    uint8_t oldValue = cpuRead(address, CpuBusCycleType::Read);
    uint8_t newValue = uint8_t(oldValue >> 1);
    rmwWrite(address, oldValue, newValue);

    // Set flags
    setFlag(C, (oldValue & 1) != 0);
    setFlag(Z, newValue == 0);
    setFlag(N, 0);

}

void CPU::NOP(uint8_t opcode)
{
    switch (opcode)
    {
        case 0xEA:
        case 0x1A: case 0x3A: case 0x5A: case 0x7A:
        case 0xDA: case 0xFA:
            // 1 byte, implied
            cpuRead(PC, CpuBusCycleType::DummyRead);
            break;

        case 0x04: case 0x44: case 0x64: // Zero-page
        {
            uint8_t zp = fetchOperand();
            cpuRead(zp, CpuBusCycleType::Read);
            break;
        }
        case 0x14: case 0x34: case 0x54: case 0x74:
        case 0xD4: case 0xF4: // Zero-page,X
        {
            const uint16_t addr = zpXAddress();
            cpuRead(addr, CpuBusCycleType::Read);
            break;
        }
        case 0x0C: // Absolute
        {
            uint16_t lo = fetchOperand(); uint16_t hi = fetchOperand();
            uint16_t addr = uint16_t(lo | (hi << 8));
            cpuRead(addr, CpuBusCycleType::Read);
            break;
        }
        case 0x1C: case 0x3C: case 0x5C: case 0x7C:
        case 0xDC: case 0xFC: // Absolute,X
        {
            auto ret = readABSXAddressBoundary();
            addPageCrossIf(ret.crossed);
            (void)ret.value; // value is discarded, this is a “read”
            break;
        }
        case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2:
            fetchOperand();
            break;
    }
}

void CPU::ORA(uint8_t opcode)
{
    uint8_t value = 0;

    switch(opcode)
    {
        case 0x01: value = readIndirectX(); break;
        case 0x05: value = readZP(); break;
        case 0x09: value = readImmediate(); break;
        case 0x0D: value = readABS(); break;
        case 0x11:
        {
            auto ret = readIndirectYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
        case 0x15: value = readZPX(); break;
        case 0x19:
            {
            auto ret = readABSYAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
        case 0x1D:
        {
            auto ret = readABSXAddressBoundary();
            addPageCrossIf(ret.crossed);
            value = ret.value;
            break;
        }
    }
    A |= value;
    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::PHA()
{
    const uint16_t phaPC = uint16_t(PC - 1); // opcode address, since opcode fetch already advanced PC
    const uint8_t spBefore = SP;
    const uint8_t pushedValue = A;

    // PHA dummy read / throwaway read.
    // PC already points to the byte after opcode $48.
    cpuRead(PC, CpuBusCycleType::DummyRead);

    push(pushedValue);

    const uint8_t spAfter = SP;

    lastPHA.valid = true;
    lastPHA.phaOpcodePC = phaPC;
    lastPHA.pushedA = pushedValue;
    lastPHA.spBefore = spBefore;
    lastPHA.spAfter = spAfter;
    lastPHA.totalCycles = totalCycles;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "PHA at PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << phaPC
            << " pushed A=$" << std::setw(2) << int(pushedValue)
            << " SP $" << std::setw(2) << int(spBefore)
            << "->$" << std::setw(2) << int(spAfter);

        traceMgr->recordCPUStack(oss.str(), makeCpuStamp());
    }
}

void CPU::PHP()
{
    const uint16_t phpPC = uint16_t(PC - 1); // opcode address, since opcode fetch already advanced PC
    const uint8_t spBefore = SP;

    // PHP dummy read / throwaway read.
    // PC already points to the byte after opcode $08.
    cpuRead(PC, CpuBusCycleType::DummyRead);

    // Push status with B=1 and U=1.
    // Internal SR should not permanently store B.
    const uint8_t pushedStatus = SR | 0x30;

    push(pushedStatus);

    const uint8_t spAfter = SP;

    lastPHP.valid = true;
    lastPHP.phpOpcodePC = phpPC;
    lastPHP.internalSR = SR;
    lastPHP.pushedSR = pushedStatus;
    lastPHP.spBefore = spBefore;
    lastPHP.spAfter = spAfter;
    lastPHP.totalCycles = totalCycles;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "PHP at PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << phpPC
            << " internal SR=$" << std::setw(2) << int(SR)
            << " pushed SR=$" << std::setw(2) << int(pushedStatus)
            << " SP $" << std::setw(2) << int(spBefore)
            << "->$" << std::setw(2) << int(spAfter);

        traceMgr->recordCPUStack(oss.str(), makeCpuStamp());
    }
}

void CPU::PLA()
{
    const uint16_t plaPC = uint16_t(PC - 1); // opcode address, since opcode fetch already advanced PC
    const uint8_t spBefore = SP;

    // PLA dummy read / throwaway read.
    // PC already points to the byte after opcode $68.
    cpuRead(PC, CpuBusCycleType::DummyRead);

    const uint8_t pulledValue = pop();

    A = pulledValue;

    setFlag(Z, A == 0);
    setFlag(N, (A & 0x80) != 0);

    const uint8_t spAfter = SP;

    lastPLA.valid = true;
    lastPLA.plaOpcodePC = plaPC;
    lastPLA.pulledA = pulledValue;
    lastPLA.finalA = A;
    lastPLA.zFlag = getFlag(Z);
    lastPLA.nFlag = getFlag(N);
    lastPLA.spBefore = spBefore;
    lastPLA.spAfter = spAfter;
    lastPLA.totalCycles = totalCycles;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "PLA at PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << plaPC
            << " pulled A=$" << std::setw(2) << int(pulledValue)
            << " final A=$" << std::setw(2) << int(A)
            << " Z=" << (lastPLA.zFlag ? "1" : "0")
            << " N=" << (lastPLA.nFlag ? "1" : "0")
            << " SP $" << std::setw(2) << int(spBefore)
            << "->$" << std::setw(2) << int(spAfter);

        traceMgr->recordCPUStack(oss.str(), makeCpuStamp());
    }
}

void CPU::PLP()
{
    const uint16_t plpPC = uint16_t(PC - 1); // opcode address, since opcode fetch already advanced PC
    const uint8_t spBefore = SP;

    // PLP dummy read / throwaway read.
    // PC already points to the byte after opcode $28.
    cpuRead(PC, CpuBusCycleType::DummyRead);

    const bool oldI = (SR & I) != 0;

    const uint8_t pulledStatus = pop();

    // U forced high, B cleared internally.
    SR = (pulledStatus | 0x20) & ~0x10;

    const bool newI = (SR & I) != 0;
    const bool suppress = oldI && !newI;

    const uint8_t spAfter = SP;

    lastPLP.valid = true;
    lastPLP.plpOpcodePC = plpPC;
    lastPLP.pulledSR = pulledStatus;
    lastPLP.finalSR = SR;
    lastPLP.spBefore = spBefore;
    lastPLP.spAfter = spAfter;
    lastPLP.oldI = oldI;
    lastPLP.newI = newI;
    lastPLP.irqSuppressSet = suppress;
    lastPLP.totalCycles = totalCycles;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "PLP at PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << plpPC
            << " pulled SR=$" << std::setw(2) << int(pulledStatus)
            << " final SR=$" << std::setw(2) << int(SR)
            << " SP $" << std::setw(2) << int(spBefore)
            << "->$" << std::setw(2) << int(spAfter);

        if (suppress)
            oss << " IRQ suppress set";

        traceMgr->recordCPUIRQ(oss.str(), makeCpuStamp());
    }

    if (suppress)
        irqSuppressOne = true;
}

void CPU::RLA(uint8_t opcode)
{
    uint16_t address = 0;

    // Determine addressing mode
    switch (opcode)
    {
        case 0x23: address = indirectXAddress(); break;
        case 0x27: address = zpAddress(); break;
        case 0x2F: address = absAddress(); break;
        case 0x33: address = indirectYAddress(); dummyReadWrongPageINDY(address); break;
        case 0x37: address = zpXAddress(); break;
        case 0x3B: address = absYAddress(); dummyReadWrongPageABSY(address); break;
        case 0x3F: address = absXAddress(); dummyReadWrongPageABSX(address); break;
    }
    // Perform Rotate Left (ROL) on memory value
    uint8_t oldValue = cpuRead(address, CpuBusCycleType::Read);
    bool carry = (oldValue & 0x80) != 0;
    uint8_t newValue = uint8_t((oldValue << 1) | (getFlag(C) ? 1 : 0));
    rmwWrite(address, oldValue, newValue);

    // Update Carry flag
    setFlag(C, carry);

    // AND the result with the accumulator
    A &= newValue;

    // Update flags
    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::ROL(uint8_t opcode)
{

    if (opcode == 0x2A)
    {
        uint8_t value = A;
        bool carry = (value & 0x80) != 0;
        value = ((value << 1) | (getFlag(C) ? 1 : 0));
        A = value;

        // Set Flags
        setFlag(N, value & 0x80);
        setFlag(Z, value == 0);
        setFlag(C, carry);
        return;
    }

    uint16_t address = 0;

    switch(opcode)
    {
        case 0x26: address = zpAddress();  break;
        case 0x2E: address = absAddress(); break;
        case 0x36: address = zpXAddress(); break;
        case 0x3E: address = absXAddress(); dummyReadWrongPageABSX(address); break;
    }

    uint8_t oldValue = cpuRead(address, CpuBusCycleType::Read);
    bool carry = (oldValue & 0x80) != 0;
    uint8_t newValue = uint8_t((oldValue << 1) | (getFlag(C)?1:0));
    rmwWrite(address, oldValue, newValue);

    // Set Flags
    setFlag(N, newValue & 0x80);
    setFlag(Z, newValue == 0);
    setFlag(C, carry);
}

void CPU::ROR(uint8_t opcode)
{
    if (opcode == 0x6A)
    {
        uint8_t value = A;
        bool oldCarry = getFlag(C);
        bool newCarry = (value & 0x01) != 0;
        value = (value >> 1) | (oldCarry ? 0x80 : 0);
        A = value;

        // Update processor flags
        setFlag(N, value & 0x80);
        setFlag(Z, value == 0);
        setFlag(C, newCarry);
        return;
    }

    uint16_t address = 0;

    switch(opcode)
    {
        case 0x66: address = zpAddress(); break;
        case 0x6E: address = absAddress(); break;
        case 0x76: address = zpXAddress(); break;
        case 0x7E: address = absXAddress(); dummyReadWrongPageABSX(address); break;
    }

    bool oldCarry = getFlag(C);
    uint8_t oldValue = cpuRead(address, CpuBusCycleType::Read);
    uint8_t newValue = uint8_t((oldValue >> 1) | (oldCarry ? 0x80 : 0));
    bool newCarry = (oldValue & 0x01) != 0;
    rmwWrite(address, oldValue, newValue);

    // Update processor flags
    setFlag(N, newValue & 0x80);
    setFlag(Z, newValue == 0);
    setFlag(C, newCarry);
}

void CPU::RRA(uint8_t opcode)
{
    uint16_t address = 0;

    // Determine addressing mode
    switch (opcode)
    {
        case 0x63: address = indirectXAddress(); break;
        case 0x67: address = zpAddress(); break;
        case 0x6F: address = absAddress(); break;
        case 0x73: address = indirectYAddress(); dummyReadWrongPageINDY(address); break;
        case 0x77: address = zpXAddress(); break;
        case 0x7B: address = absYAddress(); dummyReadWrongPageABSY(address); break;
        case 0x7F: address = absXAddress(); dummyReadWrongPageABSX(address); break;
    }
    // Perform Rotate Right (ROR) on memory value
    uint8_t oldValue = cpuRead(address, CpuBusCycleType::Read);
    bool carry = (oldValue & 0x01) != 0; // Capture old LSB
    uint8_t newValue = uint8_t((oldValue >> 1) | (getFlag(C) ? 0x80 : 0)); // Shift right and insert carry into MSB
    rmwWrite(address, oldValue, newValue);

    // Update Carry flag from old LSB
    setFlag(C, carry);

    // Perform ADC (Add with Carry) with the accumulator
    uint8_t oldA = A;
    uint16_t tempResult = uint16_t(oldA) + uint16_t(newValue) + getFlag(C);

    // Update accumulator
    A = tempResult & 0xFF;

    // Update flags
    setFlag(C, tempResult > 0xFF); // Carry flag: Set if addition overflows
    setFlag(Z, A == 0);           // Zero flag: Set if result is zero
    setFlag(N, A & 0x80);         // Negative flag: Set if MSB is 1
    setFlag(V, (~(oldA ^ newValue) & (oldA ^ tempResult) & 0x80) != 0); // Overflow flag
}

void CPU::RTI()
{
    const uint16_t rtiPC = uint16_t(PC - 1); // opcode address, since opcode fetch already advanced PC
    const uint8_t spBefore = SP;

    // RTI dummy read / throwaway read.
    // PC already points to the byte after opcode $40.
    cpuRead(PC, CpuBusCycleType::DummyRead);

    const bool oldI = (SR & I) != 0;

    const uint8_t pulledStatus = pop();

    // U forced high, B cleared internally.
    SR = (pulledStatus | 0x20) & ~0x10;

    const bool newI = (SR & I) != 0;

    const uint8_t lo = pop();
    const uint8_t hi = pop();

    PC = uint16_t(lo) | (uint16_t(hi) << 8);

    const uint8_t spAfter = SP;

    const bool suppress = oldI && !newI;

    lastRTI.valid = true;
    lastRTI.rtiOpcodePC = rtiPC;
    lastRTI.pulledSR = pulledStatus;
    lastRTI.finalSR = SR;
    lastRTI.pulledPCL = lo;
    lastRTI.pulledPCH = hi;
    lastRTI.returnPC = PC;
    lastRTI.spBefore = spBefore;
    lastRTI.spAfter = spAfter;
    lastRTI.oldI = oldI;
    lastRTI.newI = newI;
    lastRTI.irqSuppressSet = suppress;
    lastRTI.totalCycles = totalCycles;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "RTI at PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << rtiPC
            << " pulled SR=$" << std::setw(2) << int(pulledStatus)
            << " final SR=$" << std::setw(2) << int(SR)
            << " return=$" << std::setw(4) << PC
            << " SP $" << std::setw(2) << int(spBefore)
            << "->$" << std::setw(2) << int(spAfter);
        traceMgr->recordCPUIRQ(oss.str(), makeCpuStamp());
    }

    // Only suppress one IRQ check if RTI changed I from set to clear.
    if (suppress)
        irqSuppressOne = true;
}

void CPU::RTS()
{
    const uint16_t rtsPC = uint16_t(PC - 1); // opcode address, since opcode fetch already advanced PC
    const uint8_t spBefore = SP;

    // Dummy read
    cpuRead(PC, CpuBusCycleType::DummyRead);

    const uint8_t lowByte = pop();   // Pop low byte first
    const uint8_t highByte = pop();  // Pop high byte second

    const uint16_t pulledReturn = uint16_t(lowByte) | (uint16_t(highByte) << 8);

    // RTS returns to pulled address + 1.
    PC = uint16_t(pulledReturn + 1);

    const uint8_t spAfter = SP;

    lastRTS.valid = true;
    lastRTS.rtsOpcodePC = rtsPC;
    lastRTS.pulledReturn = pulledReturn;
    lastRTS.finalPC = PC;
    lastRTS.pulledLow = lowByte;
    lastRTS.pulledHigh = highByte;
    lastRTS.spBefore = spBefore;
    lastRTS.spAfter = spAfter;
    lastRTS.totalCycles = totalCycles;

    if (traceMgr)
    {
        std::ostringstream oss;
        oss << "RTS at PC=$"
            << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << rtsPC
            << " pulled return=$" << std::setw(4) << pulledReturn
            << " final PC=$" << std::setw(4) << PC
            << " SP $" << std::setw(2) << int(spBefore)
            << "->$" << std::setw(2) << int(spAfter);

        traceMgr->recordCPUStack(oss.str(), makeCpuStamp());
    }
}

void CPU::SAX(uint8_t opcode)
{
    uint16_t ea = 0;

    switch (opcode)
    {
        case 0x87: // ZP
            ea = zpAddress();
            break;

        case 0x97: // ZP,Y
            ea = zpYAddress();
            break;

        case 0x8F: // ABS
            ea = absAddress();
            break;

        case 0x83: // (ZP,X)
            ea = indirectXAddress();
            break;

        default:
            return;
    }

    cpuWrite(ea, uint8_t(A & X), CpuBusCycleType::Write);
}

void CPU::SBC(uint8_t opcode)
{
    uint8_t value = 0;

    switch (opcode)
    {
        case 0xE1: value = readIndirectX(); break;
        case 0xE5: value = readZP();        break;
        case 0xE9: value = readImmediate(); break;
        case 0xEB: value = readImmediate(); break;
        case 0xED: value = readABS();       break;
        case 0xF1: { auto r = readIndirectYAddressBoundary(); addPageCrossIf(r.crossed); value = r.value; break; }
        case 0xF5: value = readZPX();       break;
        case 0xF9: { auto r = readABSYAddressBoundary();       addPageCrossIf(r.crossed); value = r.value; break; }
        case 0xFD: { auto r = readABSXAddressBoundary();       addPageCrossIf(r.crossed); value = r.value; break; }
    }

    const uint8_t a0  = A;
    const uint8_t cIn = getFlag(C) ? 1 : 0;

    uint16_t diff   = uint16_t(a0) - uint16_t(value) - (1 - cIn);
    uint8_t  resBin = uint8_t(diff);

    setFlag(V, ((a0 ^ value) & (a0 ^ resBin) & 0x80) != 0);

    if (getFlag(D))
    {
        // NMOS 6502/6510 decimal-mode correction for SBC.
        // V is already based on the binary result above.
        // N/Z should be based on the binary 8-bit result, not final BCD A.
        uint16_t adj = diff;

        // Low nibble borrow?
        int lo = (a0 & 0x0F) - (value & 0x0F) - (1 - cIn);
        if (lo < 0)
            adj -= 0x06;

        // High digit borrow?
        if (adj > 0x99)
            adj -= 0x60;

        A = uint8_t(adj);

        // Important NMOS decimal-mode quirk:
        // N/Z come from the pre-adjust binary result.
        setFlag(Z, resBin == 0);
        setFlag(N, (resBin & 0x80) != 0);
    }
    else
    {
        // Pure binary
        A = resBin;

        setFlag(Z, A == 0);
        setFlag(N, (A & 0x80) != 0);
    }

    setFlag(C, diff < 0x100);
}

void CPU::SHX()
{
    const uint8_t lo = fetchOperand();
    const uint8_t hi = fetchOperand();

    const uint16_t base =
        uint16_t(lo) | (uint16_t(hi) << 8);

    const uint16_t address =
        uint16_t(base + Y);

    // Indexed store dummy read uses uncorrected high byte.
    const uint16_t dummy =
        uint16_t((base & 0xFF00) | (address & 0x00FF));

    cpuRead(dummy, CpuBusCycleType::DummyRead);

    const uint8_t value =
        uint8_t(X & uint8_t((base >> 8) + 1));

    cpuWrite(address, value, CpuBusCycleType::Write);
}

void CPU::SHY()
{
    const uint8_t lo = fetchOperand();
    const uint8_t hi = fetchOperand();

    const uint16_t base =
        uint16_t(lo) | (uint16_t(hi) << 8);

    const uint16_t address =
        uint16_t(base + X);

    // Indexed store dummy read uses uncorrected high byte.
    const uint16_t dummy =
        uint16_t((base & 0xFF00) | (address & 0x00FF));

    cpuRead(dummy, CpuBusCycleType::DummyRead);

    const uint8_t value =
        uint8_t(Y & uint8_t((base >> 8) + 1));

    cpuWrite(address, value, CpuBusCycleType::Write);
}

void CPU::SLO(uint8_t opcode)
{
    uint16_t address = 0;

    // Select addressing mode
    switch (opcode)
    {
        case 0x03: address = indirectXAddress(); break;
        case 0x07: address = zpAddress(); break;
        case 0x0F: address = absAddress(); break;
        case 0x13: address = indirectYAddress(); dummyReadWrongPageINDY(address); break;
        case 0x17: address = zpXAddress(); break;
        case 0x1B: address = absYAddress(); dummyReadWrongPageABSY(address); break;
        case 0x1F: address = absXAddress(); dummyReadWrongPageABSX(address); break;
    }

    // Read, shift left, and write back
    uint8_t oldValue = cpuRead(address, CpuBusCycleType::Read);
    setFlag(C, (oldValue & 0x80) != 0);  // old bit 7 → Carry
    uint8_t newValue = uint8_t(oldValue << 1);
    rmwWrite(address, oldValue, newValue);

    // ORA with accumulator
    A |= newValue;
    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::SRE(uint8_t opcode)
{
    uint16_t address = 0;

    // Select addressing mode
    switch (opcode)
    {
        case 0x43: address = indirectXAddress(); break;
        case 0x47: address = zpAddress(); break;
        case 0x4F: address = absAddress(); break;
        case 0x53: address = indirectYAddress(); dummyReadWrongPageINDY(address); break;
        case 0x57: address = zpXAddress(); break;
        case 0x5B: address = absYAddress(); dummyReadWrongPageABSY(address); break;
        case 0x5F: address = absXAddress(); dummyReadWrongPageABSX(address); break;
    }

    // Perform LSR on memory value
    uint8_t oldValue = cpuRead(address, CpuBusCycleType::Read);
    setFlag(C, (oldValue & 0x01) != 0);
    uint8_t newValue = uint8_t(oldValue >> 1);
    rmwWrite(address, oldValue, newValue);

    // EOR with accumulator
    A ^= newValue;
    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::STA(uint8_t opcode)
{
    uint16_t ea = 0;

    switch (opcode)
    {
        case 0x85: // ZP
            ea = zpAddress();
            cpuWrite(ea, A, CpuBusCycleType::Write);
            return;

        case 0x95: // ZP,X
            ea = zpXAddress();
            cpuWrite(ea, A, CpuBusCycleType::Write);
            return;

        case 0x8D: // ABS
            ea = absAddress();
            cpuWrite(ea, A, CpuBusCycleType::Write);
            return;

        case 0x9D: // ABS,X
        {
            const uint16_t base = uint16_t(fetchOperand()) | (uint16_t(fetchOperand()) << 8);
            ea = uint16_t(base + X);

            // Indexed store dummy read uses uncorrected high byte.
            const uint16_t dummy = uint16_t((base & 0xFF00) | (ea & 0x00FF));
            cpuRead(dummy, CpuBusCycleType::DummyRead);

            cpuWrite(ea, A, CpuBusCycleType::Write);
            return;
        }

        case 0x99: // ABS,Y
        {
            const uint16_t base = uint16_t(fetchOperand()) | (uint16_t(fetchOperand()) << 8);
            ea = uint16_t(base + Y);

            // Indexed store dummy read uses uncorrected high byte.
            const uint16_t dummy = uint16_t((base & 0xFF00) | (ea & 0x00FF));
            cpuRead(dummy, CpuBusCycleType::DummyRead);

            cpuWrite(ea, A, CpuBusCycleType::Write);
            return;
        }

        case 0x81: // (ZP,X)
            ea = indirectXAddress();
            cpuWrite(ea, A, CpuBusCycleType::Write);
            return;

        case 0x91: // (ZP),Y
        {
            const uint8_t zp = fetchOperand();
            const uint8_t lo = cpuRead(zp, CpuBusCycleType::Read);
            const uint8_t hi = cpuRead(uint8_t(zp + 1), CpuBusCycleType::Read);

            const uint16_t base = uint16_t(lo) | (uint16_t(hi) << 8);
            ea = uint16_t(base + Y);

            // Indexed indirect-Y store dummy read uses uncorrected high byte.
            const uint16_t dummy = uint16_t((base & 0xFF00) | (ea & 0x00FF));
            cpuRead(dummy, CpuBusCycleType::DummyRead);

            cpuWrite(ea, A, CpuBusCycleType::Write);
            return;
        }

        default:
            return;
    }
}

void CPU::STX(uint8_t opcode)
{
    uint16_t ea = 0;

    switch (opcode)
    {
        case 0x86: ea = zpAddress();  break;  // ZP
        case 0x8E: ea = absAddress(); break;  // ABS
        case 0x96: ea = zpYAddress(); break;  // ZP,Y
        default: return;
    }

    cpuWrite(ea, X, CpuBusCycleType::Write);
}

void CPU::STY(uint8_t opcode)
{
    uint16_t ea = 0;

    switch (opcode)
    {
        case 0x84: ea = zpAddress();  break;  // ZP
        case 0x8C: ea = absAddress(); break;  // ABS
        case 0x94: ea = zpXAddress(); break;  // ZP,X
        default: return;
    }

    cpuWrite(ea, Y, CpuBusCycleType::Write);
}

void CPU::TAS()
{
    const uint8_t lo = fetchOperand();
    const uint8_t hi = fetchOperand();

    const uint16_t base =
        uint16_t(lo) | (uint16_t(hi) << 8);

    const uint16_t address =
        uint16_t(base + Y);

    // Indexed store dummy read uses uncorrected high byte.
    const uint16_t dummy =
        uint16_t((base & 0xFF00) | (address & 0x00FF));

    cpuRead(dummy, CpuBusCycleType::DummyRead);

    SP = uint8_t(A & X);

    const uint8_t value =
        uint8_t(SP & uint8_t((base >> 8) + 1));

    cpuWrite(address, value, CpuBusCycleType::Write);
}

void CPU::TAX()
{
    X = A;
    setFlag(Z, X == 0);
    setFlag(N, X & 0x80);
}

void CPU::TAY()
{
    Y = A;
    setFlag(Z, Y == 0);
    setFlag(N, Y & 0x80);
}

void CPU::TSX()
{
    X = SP;
    setFlag(Z, X == 0);
    setFlag(N, X & 0x80);
}

void CPU::TXA()
{
    A = X;
    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::TYA()
{
    A = Y;
    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::XAA()
{
    uint8_t imm = readImmediate();
    A = (A | 0xEE) & X & imm;
    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

bool CPU::shouldRDYStallForBusCycle(CpuBusCycleType type) const
{
    if (!vicBusArbitrationEnabled)
        return false;

    if (!executingMicroOp)
        return false;

    return !rdyLine && isReadLikeBusCycle(type);
}

bool CPU::shouldAECBlockBusCycle(CpuBusCycleType type) const
{
    if (!vicBusArbitrationEnabled)
        return false;

    if (!executingMicroOp)
        return false;

    return !aecLine &&
           (isReadLikeBusCycle(type) || isWriteLikeBusCycle(type));
}

bool CPU::tryFetchOpcode(uint8_t& opcode)
{
    if (!pendingOpcodeFetch)
    {
        pendingOpcodeFetch = true;
        pendingOpcodeAddress = PC;
    }

    currentBusCycle = {
        CpuBusCycleType::OpcodeFetch,
        pendingOpcodeAddress,
        0
    };

    busCycleActive = true;

    if (shouldRDYStallForBusCycle(CpuBusCycleType::OpcodeFetch))
    {
        if (traceMgr)
            traceMgr->recordCPUBA("RDY/BA low stalls opcode fetch", makeCpuStamp());

        busCycleActive = false;
        currentBusCycle = {};
        return false;
    }

    if (shouldAECBlockBusCycle(CpuBusCycleType::OpcodeFetch))
    {
        if (traceMgr)
            traceMgr->recordCPUBA("AEC low blocks opcode fetch", makeCpuStamp());

        busCycleActive = false;
        currentBusCycle = {};
        return false;
    }

    opcode = mem->read(pendingOpcodeAddress);

    PC = uint16_t((PC + 1) & 0xFFFF);

    pendingOpcodeFetch = false;
    pendingOpcodeAddress = 0;

    busCycleActive = false;
    currentBusCycle = {};

    return true;
}

bool CPU::isReadLikeBusCycle(CpuBusCycleType type) const
{
    switch (type)
    {
        case CpuBusCycleType::OpcodeFetch:
        case CpuBusCycleType::Read:
        case CpuBusCycleType::DummyRead:
        case CpuBusCycleType::StackRead:
            return true;

        default:
            return false;
    }
}

bool CPU::isWriteLikeBusCycle(CpuBusCycleType type) const
{
    switch (type)
    {
        case CpuBusCycleType::Write:
        case CpuBusCycleType::DummyWrite:
        case CpuBusCycleType::StackWrite:
            return true;

        default:
            return false;
    }
}

void CPU::clearMicroOps()
{
    microOps = {};
    microOpCount = 0;
    microOpIndex = 0;
    microInstructionActive = false;
    executingMicroOp = false;
}

void CPU::pushMicroOp(const CpuMicroOp& op)
{
    if (microOpCount >= microOps.size())
        return;

    microOps[microOpCount++] = op;
}

bool CPU::executeCurrentMicroOp()
{
    if (microOpIndex >= microOpCount)
        return true;

    CpuMicroOp& op = microOps[microOpIndex];

    currentBusCycle = {
        op.busType,
        op.address,
        op.value
    };

    busCycleActive = (op.busType != CpuBusCycleType::None);
    executingMicroOp = true;

    // Only real micro-ops are allowed to behaviorally stall.
    // Old instruction-level cpuRead/cpuWrite paths remain diagnostic-only.
    /*if (shouldRDYStallForBusCycle(op.busType))
    {
        if (traceMgr)
            traceMgr->recordCPUBA("RDY/BA low stalls CPU micro-op", makeCpuStamp());

        executingMicroOp = false;
        busCycleActive = false;
        currentBusCycle = {};
        return false;
    }

    if (shouldAECBlockBusCycle(op.busType))
    {
        if (traceMgr)
            traceMgr->recordCPUBA("AEC low blocks CPU micro-op", makeCpuStamp());

        executingMicroOp = false;
        busCycleActive = false;
        currentBusCycle = {};
        return false;
    }*/

    if (shouldRDYStallForBusCycle(op.busType))
    {
        if (traceMgr)
            traceMgr->recordCPUBA("RDY/BA low during CPU micro-op", makeCpuStamp());
    }

    if (shouldAECBlockBusCycle(op.busType))
    {
        if (traceMgr)
            traceMgr->recordCPUBA("AEC low during CPU micro-op", makeCpuStamp());
    }

    switch (op.kind)
    {
        case CpuMicroOpKind::OpcodeFetch:
        {
            activeOpcodePC = PC;
            activeOpcode = mem->read(PC);
            PC = uint16_t((PC + 1) & 0xFFFF);

            lastOpcodePC = activeOpcodePC;
            lastOpcode = activeOpcode;
            break;
        }

        case CpuMicroOpKind::OperandRead:
        {
            microTemp = mem->read(PC);
            PC = uint16_t((PC + 1) & 0xFFFF);
            break;
        }

        case CpuMicroOpKind::MemoryRead:
        {
            microTemp = mem->read(op.address);
            break;
        }

        case CpuMicroOpKind::MemoryWrite:
        {
            mem->write(op.address, op.value);
            break;
        }

        case CpuMicroOpKind::DummyRead:
        {
            (void)mem->read(op.address);
            break;
        }

        case CpuMicroOpKind::DummyWrite:
        {
            mem->write(op.address, op.value);
            break;
        }

        case CpuMicroOpKind::StackRead:
        {
            microTemp = mem->read(op.address);
            break;
        }

        case CpuMicroOpKind::StackWrite:
        {
            mem->write(op.address, op.value);
            break;
        }

        case CpuMicroOpKind::Internal:
        case CpuMicroOpKind::None:
        default:
            break;
    }

    switch (op.action)
    {
        case CpuMicroAction::FinishNOP:
            break;

        case CpuMicroAction::LoadAFromTemp:
            A = microTemp;
            setFlag(Z, A == 0);
            setFlag(N, (A & 0x80) != 0);
            break;

        case CpuMicroAction::LoadXFromTemp:
            X = microTemp;
            setFlag(Z, X == 0);
            setFlag(N, (X & 0x80) != 0);
            break;

        case CpuMicroAction::LoadYFromTemp:
            Y = microTemp;
            setFlag(Z, Y == 0);
            setFlag(N, (Y & 0x80) != 0);
            break;

        case CpuMicroAction::TransferAToX:
            X = A;
            setFlag(Z, X == 0);
            setFlag(N, (X & 0x80) != 0);
            break;

        case CpuMicroAction::TransferAToY:
            Y = A;
            setFlag(Z, Y == 0);
            setFlag(N, (Y & 0x80) != 0);
            break;

        case CpuMicroAction::TransferXToA:
            A = X;
            setFlag(Z, A == 0);
            setFlag(N, (A & 0x80) != 0);
            break;

        case CpuMicroAction::TransferYToA:
            A = Y;
            setFlag(Z, A == 0);
            setFlag(N, (A & 0x80) != 0);
            break;

        case CpuMicroAction::TransferSPToX:
            X = SP;
            setFlag(Z, X == 0);
            setFlag(N, (X & 0x80) != 0);
            break;

        case CpuMicroAction::TransferXToSP:
            SP = X;
            // TXS does not affect flags.
            break;

        case CpuMicroAction::IncrementX:
            X = uint8_t(X + 1);
            setFlag(Z, X == 0);
            setFlag(N, (X & 0x80) != 0);
            break;

        case CpuMicroAction::IncrementY:
            Y = uint8_t(Y + 1);
            setFlag(Z, Y == 0);
            setFlag(N, (Y & 0x80) != 0);
            break;

        case CpuMicroAction::DecrementX:
            X = uint8_t(X - 1);
            setFlag(Z, X == 0);
            setFlag(N, (X & 0x80) != 0);
            break;

        case CpuMicroAction::DecrementY:
            Y = uint8_t(Y - 1);
            setFlag(Z, Y == 0);
            setFlag(N, (Y & 0x80) != 0);
            break;

        case CpuMicroAction::None:
        default:
            break;
    }

    executingMicroOp = false;
    busCycleActive = false;
    currentBusCycle = {};

    microOpIndex++;
    return true;
}

void CPU::buildMicroOpsForOpcode(uint8_t opcode)
{
    microOpCount = 0;
    microOpIndex = 0;

    switch (opcode)
    {
        case 0xEA: // NOP implied
        {
            pushMicroOp({
                CpuMicroOpKind::DummyRead,
                CpuBusCycleType::DummyRead,
                PC,
                0,
                CpuMicroAction::FinishNOP
            });
            break;
        }

        case 0xA9: // LDA #imm
            buildImmediateLoad(CpuMicroAction::LoadAFromTemp);
            break;

        case 0xA2: // LDX #imm
            buildImmediateLoad(CpuMicroAction::LoadXFromTemp);
            break;

        case 0xA0: // LDY #imm
            buildImmediateLoad(CpuMicroAction::LoadYFromTemp);
            break;

        case 0xAA: // TAX
            buildInternalAction(CpuMicroAction::TransferAToX);
            break;

        case 0xA8: // TAY
            buildInternalAction(CpuMicroAction::TransferAToY);
            break;

        case 0x8A: // TXA
            buildInternalAction(CpuMicroAction::TransferXToA);
            break;

        case 0x98: // TYA
            buildInternalAction(CpuMicroAction::TransferYToA);
            break;

        case 0xBA: // TSX
            buildInternalAction(CpuMicroAction::TransferSPToX);
            break;

        case 0x9A: // TXS
            buildInternalAction(CpuMicroAction::TransferXToSP);
            break;

        case 0xE8: // INX
            buildInternalAction(CpuMicroAction::IncrementX);
            break;

        case 0xC8: // INY
            buildInternalAction(CpuMicroAction::IncrementY);
            break;

        case 0xCA: // DEX
            buildInternalAction(CpuMicroAction::DecrementX);
            break;

        case 0x88: // DEY
            buildInternalAction(CpuMicroAction::DecrementY);
            break;

        default:
            break;
    }
}

void CPU::buildImmediateLoad(CpuMicroAction action)
{
    pushMicroOp({
        CpuMicroOpKind::OperandRead,
        CpuBusCycleType::Read,
        PC,
        0,
        action
    });
}

void CPU::buildInternalAction(CpuMicroAction action)
{
    pushMicroOp({
        CpuMicroOpKind::Internal,
        CpuBusCycleType::None,
        0,
        0,
        action
    });
}

bool CPU::canExecuteOpcodeWithMicroOps(uint8_t opcode) const
{
    switch (opcode)
    {
        case 0xEA: // NOP implied

        case 0xA9: // LDA #imm
        case 0xA2: // LDX #imm
        case 0xA0: // LDY #imm

        case 0xAA: // TAX
        case 0xA8: // TAY
        case 0x8A: // TXA
        case 0x98: // TYA
        case 0xBA: // TSX
        case 0x9A: // TXS

        case 0xE8: // INX
        case 0xC8: // INY
        case 0xCA: // DEX
        case 0x88: // DEY
            return true;

        default:
            return false;
    }
}

bool CPU::tickMicroOps()
{
    if (!microInstructionActive)
        return false;

    if (!executeCurrentMicroOp())
    {
        totalCycles++;
        return true;
    }

    if (microOpIndex >= microOpCount)
    {
        clearMicroOps();
    }

    totalCycles++;
    return true;
}

uint8_t CPU::debugRead(uint16_t address) const
{
    if (!mem) return 0xFF;
    return mem->read(address);
}

TraceManager::Stamp CPU::makeCpuStamp() const
{
    if (!traceMgr)
        return { totalCycles, 0, 0 };

    return traceMgr->makeStamp(
        totalCycles,
        vic ? vic->getCurrentRaster() : 0,
        vic ? vic->getRasterDot() : 0
    );
}
