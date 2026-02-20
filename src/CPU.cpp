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
    cia2object(nullptr),
    IRQ(nullptr),
    logger(nullptr),
    mem(nullptr),
    traceMgr(nullptr),
    vicII(nullptr),
    nmiPending(false),
    nmiLine(false),
    irqSuppressOne(false),
    jamMode(JamMode::NopCompat),
    halted(false),
    cycles(0),
    totalCycles(0),
    elapsedCycles(0),
    lastCycleCount(0),
    A(0),
    X(0),
    Y(0),
    SP(0xFD),
    SR(0x20),
    setLogging(false),
    baHold(false)
{
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

    SR |= 0x20; // force U bit
    return true;
}

void CPU::saveState(StateWriter& wrtr)
{
    // CPU0 = registers (stable baseline)
    wrtr.beginChunk("CPU0");
    CPUState state = getState();
    state.save(wrtr);
    wrtr.endChunk();

    // CPUX = runtime/internal bits that affect behavior after load
    wrtr.beginChunk("CPUX");
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
    wrtr.writeBool(baHold);
    wrtr.endChunk();
}

bool CPU::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "CPU0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        CPUState state;
        if (!state.load(rdr)) return false;

        PC = state.PC;
        A  = state.A;
        X  = state.X;
        Y  = state.Y;
        SP = state.SP;
        SR = (state.SR | 0x20);

        rdr.exitChunkPayload(chunk);
        return true;
    }

    if (std::memcmp(chunk.tag, "CPUX", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

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

        // Optional / forward-compatible fields:
        const size_t end = chunk.payloadOffset + chunk.length;
        if (rdr.cursor() < end) { if (!rdr.readBool(soLevel)) return false; }
        if (rdr.cursor() < end) { if (!rdr.readBool(baHold)) return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Not a CPU chunk
    return false;
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
    SP              = 0xFD;
    SR              = 0x24;
    cycles          = 0;
    totalCycles     = 0;
    elapsedCycles   = 0;
    lastCycleCount  = 0;
    baHold          = false;
    setLogging      = false;
    nmiPending      = false;
    nmiLine         = false;
    irqSuppressOne  = false;
    soLevel         = true;

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
    if (asserted && !nmiLine) nmiPending = true;
    nmiLine = asserted;
}

void CPU::handleIRQ()
{
    if (IRQ)
    {
        activeSource = IRQ->getHighestPrioritySource();
        switch (activeSource)
        {
            case IRQLine::VICII:
            case IRQLine::CIA1:
            case IRQLine::D1541_IRQ:
            case IRQLine::D1571_IRQ:
            case IRQLine::D1581_IRQ:
            {
                executeIRQ();
                break;
            }
        }
    }
}

void CPU::handleNMI()
{
    if (!nmiPending) return;
    nmiPending = false;
    executeNMI();
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
    if (getFlag(I)) { irqSuppressOne = false; return; } // Skip if interrupts are disabled
    if (irqSuppressOne) { irqSuppressOne = false; return; }

    // Dummy read for accuracy
    mem->read(PC);

    push((PC >> 8) & 0xFF);     // High byte of PC
    push(PC & 0xFF);            // Low byte of PC

    // Push SR with B=0, bit 5=1
    uint8_t status = SR;
    status &= ~0x10; // clear B
    status |= 0x20;  // set unused bit 5
    push(status);

    setFlag(I, true); // Disable further interrupts

    // Fetch the IRQ vector
    uint16_t irqVector = mem->read(0xFFFE) | (mem->read(0xFFFF) << 8);
    PC = irqVector;

    cycles += 7;
}

void CPU::executeNMI()
{
    // Dummy read for accuracy
    mem->read(PC);

    //Save CPU state
    push((PC >> 8) & 0xFF); // high byte of PC
    push(PC & 0xFF); // low byte of PC

      // Push SR with B=0, bit 5=1
    uint8_t status = SR;
    status &= ~0x10; // clear B
    status |= 0x20;  // set unused bit 5
    push(status);

    // Disable interrupt flag
    setFlag(I, true);

    uint16_t nmiVector = mem->read(0xFFFA) | (mem->read(0xFFFB) << 8);
    PC = nmiVector;
    cycles += 7;
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
    uint8_t value = mem->read(address);
    return value;
}

uint8_t CPU::readABSX()
{
    uint16_t address = absXAddress();
    uint8_t value = mem->read(address);
    return value;
}

uint8_t CPU::readABSY()
{
    uint16_t address = absYAddress();
    uint8_t value = mem->read(address);
    return value;
}

uint8_t CPU::readImmediate()
{
    return fetch();
}

uint8_t CPU::readIndirectX()
{
    uint16_t address = indirectXAddress();
    return mem->read(address);
}

uint8_t CPU::readIndirectY()
{
    uint16_t address = indirectYAddress();
    return mem->read(address);
}

uint8_t CPU::readZP()
{
    uint16_t address = zpAddress();
    return mem->read(address);
}

uint8_t CPU::readZPX()
{
    uint16_t address = zpXAddress();
    return mem->read(address);
}

uint8_t CPU::readZPY()
{
    uint16_t address = zpYAddress();
    return mem->read(address);
}

uint16_t CPU::absAddress()
{
    uint16_t address = fetch() | (fetch() << 8);
    return address;
}

uint16_t CPU::absXAddress()
{
    uint16_t baseAddress = fetch() | (fetch() << 8);
    uint16_t effectiveAddress = (baseAddress + X) & 0xFFFF;

    return effectiveAddress;
}

uint16_t CPU::absYAddress()
{
    uint16_t baseAddress = fetch() | (fetch() << 8);
    uint16_t effectiveAddress = (baseAddress + Y) & 0xFFFF;

    return effectiveAddress;
}

uint16_t CPU::indirectXAddress()
{
    uint8_t zpAddress = (fetch() + X) & 0xFF;
    uint8_t lowByte = mem->read(zpAddress);
    uint8_t highByte = mem->read((zpAddress + 1) & 0xFF);
    uint16_t baseAddress = (lowByte | (highByte << 8));
    return baseAddress;
}

uint16_t CPU::indirectYAddress()
{
    uint8_t zpAddress = fetch();
    uint8_t lowByte = mem->read(zpAddress);
    uint8_t highByte = mem->read((zpAddress + 1) & 0xFF); // Handle zero-page wrap
    uint16_t baseAddress = (lowByte | (highByte << 8));
    uint16_t effectiveAddress = baseAddress + Y;

    return effectiveAddress;
}

uint16_t CPU::zpAddress()
{
    uint16_t address = fetch();
    return address;
}

uint16_t CPU::zpXAddress()
{
    uint16_t address = (fetch() + X) & 0XFF;
    return address;
}

uint16_t CPU::zpYAddress()
{
    uint16_t address = (fetch() + Y) & 0xFF;
    return address;
}

CPU::ReadByte CPU::readABSXAddressBoundary()
{
    uint16_t baseAddress = fetch() | (fetch() << 8);
    uint16_t effectiveAddress = (baseAddress + X);

    bool crossed = (baseAddress & 0xFF00) != (effectiveAddress & 0xFF00);
    if (crossed)
    {
        // Perform dummy read from the incorrect page (base high + old low+X)
        uint16_t dummy = (baseAddress & 0xFF00) | ((baseAddress + X) & 0x00FF);
        mem->read(dummy);
    }

    // Perform the "real" read now
    uint8_t value = mem->read(effectiveAddress);

    return { value, crossed };
}

CPU::ReadByte CPU::readABSYAddressBoundary()
{
    uint16_t baseAddress = fetch() | (fetch() << 8);
    uint16_t effectiveAddress = (baseAddress + Y);

    bool crossed = (baseAddress & 0xFF00) != (effectiveAddress & 0xFF00);
    if (crossed)
    {
        // Perform dummy read from the incorrect page (base high + old low+Y)
        uint16_t dummy = (baseAddress & 0xFF00) | ((baseAddress + Y) & 0x00FF);
        mem->read(dummy);
    }

    // Perform "real" read now
    uint8_t value = mem->read(effectiveAddress);

    return { value, crossed };
}

CPU::ReadByte CPU::readIndirectYAddressBoundary()
{
    uint8_t zpAddress = fetch();
    uint8_t lowByte = mem->read(zpAddress);
    uint8_t highByte = mem->read((zpAddress + 1) & 0xFF); // zero-page wrap
    uint16_t baseAddress = lowByte | (highByte << 8);
    uint16_t effectiveAddress = baseAddress + Y;

    bool crossed = (baseAddress & 0xFF00) != (effectiveAddress & 0xFF00);
    if (crossed)
    {
        // Dummy read from base address + low-page offset (wrong page)
        uint16_t dummy = (baseAddress & 0xFF00) | (effectiveAddress & 0x00FF);
        mem->read(dummy);
    }

    uint8_t value = mem->read(effectiveAddress & 0xFFFF);
    return { value, crossed };
}

void CPU::dummyReadWrongPageABSX(uint16_t address)
{
    uint16_t base = (address - X) & 0xFFFF;
    if ((base ^ address) & 0xFF00)
    {
        uint16_t dummy = (base & 0xFF00) | (address & 0x00FF);
        mem->read(dummy);
    }
}

void CPU::dummyReadWrongPageABSY(uint16_t address)
{
    uint16_t base = (address - Y) & 0xFFFF;
    if ((base ^ address) & 0xFF00)
    {
        uint16_t dummy = (base & 0xFF00) | (address & 0x00FF);
        mem->read(dummy);
    }
}

void CPU::dummyReadWrongPageINDY(uint16_t address)
{
    uint16_t base = (address - Y) & 0xFFFF;
    if ((base ^ address) & 0xFF00)
    {
        uint16_t dummy = (base & 0xFF00) | (address & 0x00FF);
        mem->read(dummy);
    }
}

void CPU::rmwWrite(uint16_t address, uint8_t oldValue, uint8_t newValue)
{
    // Perform "dummy write" first
    mem->write(address, oldValue);

    // Now we update to the real value
    mem->write(address, newValue);
}

void CPU::tick()
{
    if (halted) // Jam Halt
    {
        cycles = 1; // always force 1 cycle pending
        cycles--;   // consume it
        totalCycles++;
        return;
    }

    if (cycles <= 0)
    {
        handleNMI();
        handleIRQ();

        if (cycles <= 0)
        {
            if (baHold) { totalCycles++; return; }

            const uint16_t pcExec = PC;   // PC of the instruction to execute
            uint8_t opcode = fetch();

            // Log it
            if (logger && setLogging)
            {
                std::stringstream message;
                message << "PC = " << std::hex << static_cast<int>(PC - 1) << ", OPCODE = " << std::hex << static_cast<int>(opcode) << ", A = " << std::hex
                << static_cast<int>(A) << ", X = " << std::hex << static_cast<int>(X) << ", Y= " << std::hex << static_cast<int>(Y)
                << ", SP = " << std::hex << static_cast<int>(SP);
                logger->WriteLog(message.str());
            }
            decodeAndExecute(opcode);

            // If tracing is on capture it
            if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CPU))
            {
                traceMgr->recordCPUTrace(pcExec, opcode, traceMgr->makeStamp(totalCycles,
                    vicII ? vicII->getCurrentRaster() : 0, vicII ? vicII->getRasterDot() : 0));
            }

            // Update the cycles based on the table
            cycles += CYCLE_COUNTS[opcode];  // Set cycle count for the opcode
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

uint8_t CPU::fetch()
{
    uint8_t byte = mem->read(PC);
    PC = (PC + 1) & 0xFFFF; // Ensure PC wraps properly
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
    mem->write(0x100 + SP, value);
    SP = (SP - 1) & 0xFF;
}

uint8_t CPU::pop()
{
    SP = (SP +1) & 0xFF;
    uint8_t value = mem->read(0x100 + SP);
    return value;
}

//OPCODES implemented
void CPU::AAC()
{
    uint8_t value = fetch(); // Fetch the immediate value
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
        // NMOS 6502/6510 BCD correction
        uint16_t adj = sum;

        // +0x06 if low nibble overflowed 9
        if ( ((a0 & 0x0F) + (value & 0x0F) + cIn) > 9 )
            adj += 0x06;

        // +0x60 if > 0x99 (and set carry)
        if (adj > 0x99)
        {
            adj += 0x60;
            setFlag(C, 1);
        }
        else
        {
            setFlag(C, 0);
        }

        A = uint8_t(adj);
    }
    else
    {
        // Pure binary
        A = bin8;
        setFlag(C, sum > 0xFF);
    }

    setFlag(Z, A == 0);
    setFlag(N, (A & 0x80) != 0);
}

void CPU::AHX(uint8_t opcode)
{
    uint16_t base = 0;
    uint16_t address = 0;
    uint8_t value = A & X; // Compute A AND X

    // Determine addressing mode
    switch (opcode)
    {
        case 0x93: // (Indirect), Y
        {
            uint8_t zp = fetch();
            uint8_t lo = mem->read(zp);
            uint8_t hi = mem->read((uint8_t)(zp + 1));
            base = (uint16_t)lo | ((uint16_t)hi << 8);
            address = (base + Y) & 0xFFFF;
            if ((base & 0xFF00) != (address & 0xFF00))
                mem->read((base & 0xFF00) | (address & 0x00FF));
            else
                mem->read(address);
            break;
        }
        case 0x9F: // Absolute, Y
        {
            uint8_t lo = fetch();
            uint8_t hi = fetch();
            base = (uint16_t)lo | ((uint16_t)hi << 8);
            address = (base + Y) & 0xFFFF;
            if ((base & 0xFF00) != (address & 0xFF00))
                mem->read((base & 0xFF00) | (address & 0x00FF));
            else
                mem->read(address);
            break;
        }
    }

    value &= (( (base >> 8) + 1) & 0xFF);

    mem->write(address, value); // Store result at the effective address
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

    uint8_t oldValue = mem->read(address);
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
    int8_t offset = static_cast<int8_t>(fetch());

    // Dummy read for accuracy
    mem->read(PC);

    if (!getFlag(C))  // Branch if Carry Clear
    {
        cycles++; // Extra cycle for a taken branch
        uint16_t newPC = (PC + offset) & 0xFFFF; // handle wrapping
        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            mem->read((PC & 0xFF00) | (newPC & 0x00FF));
            cycles++; // Extra cycle if page boundary is crossed
        }
        PC = newPC;  // Update program counter
    }
}

void CPU::BCS()
{
    int8_t offset = static_cast<int8_t>(fetch());

    // Dummy read for accuracy
    mem->read(PC);

    if (getFlag(C))  // Branch if Carry Set
    {
        cycles++; // Extra cycle for a taken branch

        uint16_t newPC = (PC + offset) & 0xFFFF;

        // Extra cycle if page boundary is crossed
        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            mem->read((PC & 0xFF00) | (newPC & 0x00FF));
            cycles++;
        }

        PC = newPC;
    }
}

void CPU::BEQ()
{
    int8_t offset = static_cast<int8_t>(fetch());

    // Dummy read for accuracy
    mem->read(PC);

    if (getFlag(Z))
    {
        cycles++;

        uint16_t newPC = (PC + offset) & 0xFFFF; // handle wrapping

        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            mem->read((PC & 0xFF00) | (newPC & 0x00FF));
            cycles ++;
        }
        PC = newPC;
    }
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
    int8_t offset = static_cast<int8_t>(fetch());

    // Dummy read for accuracy
    mem->read(PC);

    uint16_t newPC = (PC + offset) & 0xFFFF;

    if (getFlag(N))
    {
        cycles++;
        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            mem->read((PC & 0xFF00) | (newPC & 0x00FF));
            cycles ++;
        }
        PC = newPC;
    }
}

void CPU::BNE()
{
    // Fetch the signed 8-bit branch offset
    int8_t offset = static_cast<int8_t>(fetch());

    // Dummy read for accuracy
    mem->read(PC);

    if (!getFlag(Z)) // Branch if Zero flag is NOT set
    {
        cycles++; // Add cycle for taking the branch

        uint16_t newPC = (PC + offset) & 0xFFFF; // Ensure 16-bit wrapping

        // Check if the branch crosses a page boundary
        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            mem->read((PC & 0xFF00) | (newPC & 0x00FF));
            cycles++; // Add cycle if page boundary crossed
        }

        // Update the program counter to the new address
        PC = newPC;
    }
}

void CPU::BPL()
{
    int8_t offset = static_cast<int8_t>(fetch());

    // Dummy read for accuracy
    mem->read(PC);

    if (!getFlag(N))  // Check if Negative flag is not set (i.e., positive)
    {
        uint16_t oldPC = PC;
        PC = (PC + offset) & 0xFFFF;

        // Base cycles for branch instruction
        cycles ++;

        // Check if crossing a page boundary
        if ((oldPC & 0xFF00) != (PC & 0xFF00))
        {
            mem->read((oldPC & 0xFF00) | (PC & 0x00FF));
            cycles++;  // Extra cycle for page boundary crossing
        }
    }
}

void CPU::BRK()
{
    uint16_t newPC = PC + 1;  // Point to the instruction after BRK

    // Dummy read for accuracy
    mem->read(PC);

    push((newPC >> 8) & 0xFF);
    push(newPC & 0xFF);
    setFlag(B, 1);
    push(SR | 0x20);

    // Set the interrupt disable flag (I flag) in the processor status
    setFlag(I, 1);
    setFlag(B, 0);

    // Load the new Program Counter (PC) from the interrupt vector (0xFFFE and 0xFFFF)
    uint16_t interruptVector = mem->read(0xFFFE) | (mem->read(0xFFFF) << 8);

    // Set the PC to the interrupt vector
    PC = interruptVector;
}

void CPU::BVC()
{
    int8_t offset = static_cast<int8_t>(fetch());

    // Dummy read for accuracy
    mem->read(PC);

    if (!getFlag(V)) // Branch if Overflow Clear
    {
        cycles++; // Extra cycle for taking the branch

        uint16_t newPC = (PC + offset) & 0xFFFF;

        // Extra cycle if page boundary is crossed
        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            mem->read((PC & 0xFF00) | (newPC & 0x00FF));
            cycles++;
        }

        PC = newPC;
    }
}

void CPU::BVS()
{
    int8_t offset = static_cast<int8_t>(fetch());

    // Dummy read for accuracy
    mem->read(PC);

    if (getFlag(V)) // Branch if Overflow Set
    {
        cycles++; // Extra cycle for taking the branch

        uint16_t newPC = (PC + offset) & 0xFFFF;

        // Extra cycle if page boundary is crossed
        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            mem->read((PC & 0xFF00) | (newPC & 0x00FF));
            cycles++;
        }

        PC = newPC;
    }
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
    uint8_t oldValue = mem->read(address);
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

    uint8_t oldValue = mem->read(address);
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

    uint8_t oldValue = mem->read(address);
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

    uint8_t oldValue = mem->read(address);
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
            // PC already incremented by fetch().
            break;
    }
}

void CPU::JMP(uint8_t opcode)
{
    uint16_t address = 0;

    switch (opcode)
    {
        case 0x4C:
        {
            address = fetch() | (fetch() << 8);
            PC = address;
            break;
        }
        case 0x6C: // JMP indirect
        {
            uint16_t pointer = fetch() | (fetch() << 8);
            uint8_t lowByte = mem->read(pointer);
            uint8_t highByte = mem->read((pointer & 0xFF00) | ((pointer + 1) & 0x00FF)); // Page-boundary bug
            PC = (highByte << 8) | lowByte;
            break;
        }
    }
}

void CPU::JSR()
{
    // Calculate the return address (next instruction address - 1) before fetching the target address
    uint16_t returnAddress = (PC + 1) & 0xFFFF;  // PC currently points to the low byte of the operand

    // Fetch the target address for the JSR instruction
    uint16_t address = absAddress();

    // Push the return address onto the stack (high byte first, then low byte)
    push((returnAddress >> 8) & 0xFF);  // Push high byte
    push(returnAddress & 0xFF);         // Push low byte

    // Update the program counter (PC) to the target address of the JSR subroutine
    PC = address;
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

    uint8_t oldValue = mem->read(address);
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
            mem->read(PC);
            break;

        case 0x04: case 0x44: case 0x64: // Zero-page
        {
            uint8_t zp = fetch();
            mem->read(zp);
            break;
        }
        case 0x14: case 0x34: case 0x54: case 0x74:
        case 0xD4: case 0xF4: // Zero-page,X
        {
            uint8_t zp = fetch();
            mem->read(uint8_t(zp + X));
            break;
        }
        case 0x0C: // Absolute
        {
            uint16_t lo = fetch(); uint16_t hi = fetch();
            uint16_t addr = uint16_t(lo | (hi << 8));
            mem->read(addr);
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
            fetch();
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
    // Dummy read for accuracy
    mem->read(PC);

    push(A);
}

void CPU::PHP()
{
    // Dummy read for accuracy
    mem->read(PC);

    push(SR | 0x30);
}

void CPU::PLA()
{
    mem->read(0x100 + ((SP + 1) & 0xFF)); // dummy stack read

    A = pop();
    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::PLP()
{
    mem->read(0x100 + ((SP + 1) & 0xFF)); // dummy stack read

    uint8_t status = pop();
    status |= 0x20;
    bool willEnableIRQ = ((status & I) == 0);
    SR = status;
    if (willEnableIRQ) irqSuppressOne = true;
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
    uint8_t oldValue = mem->read(address);
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

    uint8_t oldValue = mem->read(address);
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
    uint8_t oldValue = mem->read(address);
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
    uint8_t oldValue = mem->read(address);
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
    // Dummy read
    mem->read(PC);

    SR = pop();
    SR |= 0x20; // force bit 5 = 1
    bool willEnableIRQ = ((SR & I) == 0);
    uint8_t lo = pop(), hi = pop();
    PC = (hi << 8) | lo;
    if (willEnableIRQ) irqSuppressOne = true;
}

void CPU::RTS()
{
    // Dummy read
    mem->read(PC);

    uint8_t lowByte = pop();  // Pop low byte first
    uint8_t highByte = pop();  // Pop high byte second

    // Set the PC to the return address
    PC = (highByte << 8) | lowByte;
    PC = (PC + 1) & 0xFFFF;
}

void CPU::SAX(uint8_t opcode)
{
    uint16_t address = 0;
    switch (opcode)
    {
        case 0x83: address = indirectXAddress(); break;
        case 0x87: address = zpAddress(); break;
        case 0x8F: address = absAddress(); break;
        case 0x97: address = zpYAddress(); break;
    }

    uint8_t result = A & X; // Compute A AND X

    mem->write(address, result); // Store the result in memory
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

    if (getFlag(D)) {
        // NMOS 6502/6510 decimal-mode correction for SBC
        uint16_t adj = diff;

        // Low nibble borrow? (do the nibble subtraction and see if it went negative)
        int lo = (a0 & 0x0F) - (value & 0x0F) - (1 - cIn);
        if (lo < 0) adj -= 0x06;     // subtract 6 if low digit borrowed

        // High nibble borrow? (i.e., result > 0x99 in BCD sense)
        if (adj > 0x99) adj -= 0x60; // subtract 0x60 if high digit borrowed

        A = uint8_t(adj);
    } else {
        // Pure binary
        A = resBin;
    }

    setFlag(C, diff < 0x100);

    setFlag(Z, A == 0);
    setFlag(N, A & 0x80);
}

void CPU::SHX()
{
    uint16_t baseAddress = absAddress();
    uint16_t effectiveAddress = (baseAddress + Y) & 0xFFFF;
    uint8_t highByte = ((baseAddress >> 8) + 1) & 0xFF;
    uint8_t value = X & highByte;

    if ((baseAddress & 0xFF00) != (effectiveAddress & 0xFF00))
        mem->read((baseAddress & 0xFF00) | (effectiveAddress & 0x00FF));
    else
        mem->read(effectiveAddress);

    // Write the value
    mem->write(effectiveAddress, value);
}

void CPU::SHY()
{
    uint16_t baseAddress = absAddress();
    uint16_t effectiveAddress = (baseAddress + X) & 0xFFFF;
    uint8_t highByte = ((baseAddress >> 8) + 1) & 0xFF;
    uint8_t value = Y & highByte;  // Logical AND of Y and high byte + 1

    if ((baseAddress & 0xFF00) != (effectiveAddress & 0xFF00))
        mem->read((baseAddress & 0xFF00) | (effectiveAddress & 0x00FF));
    else
        mem->read(effectiveAddress);

    // Write the value
    mem->write(effectiveAddress, value);
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
    uint8_t oldValue = mem->read(address);
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
    uint8_t oldValue = mem->read(address);
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
            mem->write(ea, A);
            return;

        case 0x95: // ZP,X  (no dummy read)
            ea = zpXAddress();
            mem->write(ea, A);
            return;

        case 0x8D: // ABS
            ea = absAddress();
            mem->write(ea, A);
            return;

        case 0x9D: // ABS,X  (dummy read @ EA, always)
        {
            uint16_t base = fetch() | (fetch() << 8);
            ea = uint16_t(base + X);
            mem->read(ea);              // required dummy read
            mem->write(ea, A);
            return;
        }

        case 0x99: // ABS,Y  (dummy read @ EA, always)
        {
            uint16_t base = fetch() | (fetch() << 8);
            ea = uint16_t(base + Y);
            mem->read(ea);              // required dummy read
            mem->write(ea, A);
            return;
        }

        case 0x81: // (ZP,X)  (no dummy read)
            ea = indirectXAddress();
            mem->write(ea, A);
            return;

        case 0x91: // (ZP),Y  (dummy read @ EA, always)
        {
            uint8_t zp = fetch();
            uint8_t lo = mem->read(zp);
            uint8_t hi = mem->read(uint8_t(zp + 1));
            ea = (uint16_t(hi) << 8) | lo;
            ea = uint16_t(ea + Y);
            mem->read(ea);              // required dummy read
            mem->write(ea, A);
            return;
        }
    }
}

void CPU::STX(uint8_t opcode)
{
    uint16_t ea = 0;
    switch (opcode)
    {
        case 0x86: ea = zpAddress();  break;  // ZP
        case 0x8E: ea = absAddress(); break;  // ABS
        case 0x96: ea = zpYAddress(); break;  // ZP,Y  (no dummy read)
    }
    mem->write(ea, X);
}

void CPU::STY(uint8_t opcode)
{
    uint16_t ea = 0;
    switch (opcode)
    {
        case 0x84: ea = zpAddress();  break;  // ZP
        case 0x8C: ea = absAddress(); break;  // ABS
        case 0x94: ea = zpXAddress(); break;  // ZP,X  (no dummy read)
    }
    mem->write(ea, Y);
}

void CPU::TAS()
{
    uint16_t base = absAddress();               // fetch operand
    uint16_t effectiveAddress = (base + Y) & 0xFFFF;         // add index
    uint8_t high = base >> 8;

    // Update SP
    SP = A & X;

    // Value written = (A & X) & (high + 1)
    uint8_t value = (A & X) & (high + 1);

    // Dummy read for cycle accuracy
    if ((base & 0xFF00) != (effectiveAddress & 0xFF00)) mem->read((base & 0xFF00) | (effectiveAddress & 0x00FF));
    else mem->read(effectiveAddress);

    mem->write(effectiveAddress, value);
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

uint8_t CPU::debugRead(uint16_t address) const
{
    if (!mem) return 0xFF;
    return mem->read(address);
}
