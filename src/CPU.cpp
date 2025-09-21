// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CPU.h"

CPU::CPU() :
    // Initialize
    jamMode(JamMode::FreezePC),
    halted(false),
    elapsedCycles(0),
    lastCycleCount(0),
    A(0),
    X(0),
    Y(0),
    SP(0xFD),
    SR(0x20),
    baHold(false)
{
    initializeOpcodeTable();
}

CPU::~CPU() = default;

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
    SP = 0xFD;
    SR = 0x34;
    cycles = 0;
    baHold = false;
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

CPU::JamMode CPU::getJamMode()
{
    return jamMode;
}

void CPU::setJamMode(JamMode mode)
{
    jamMode = mode;
}

void CPU::step()
{
    if (cycles <= 0)
    {
        uint8_t opcode = fetch();   // <-- advances PC
        decodeAndExecute(opcode);
        cycles += CYCLE_COUNTS[opcode]; // set up instruction cycles
    }
    cycles--;   // decrement once for this step
    totalCycles++;
}

void CPU::handleIRQ()
{
    if (IRQ)
    {
        //activeSource = IRQ->getActiveSources();
        activeSource = IRQ->getHighestPrioritySource();
        switch (activeSource)
        {
            case IRQLine::VICII:
            case IRQLine::CIA1_TIMER_A:
            case IRQLine::CIA1_TIMER_B:
            case IRQLine::CIA1_TOD:
            case IRQLine::CIA1_SERIAL:
            case IRQLine::CIA1_FLAG:
            {
                executeIRQ();
                break;
            }
        }
    }
}

void CPU::executeIRQ()
{
    if (getFlag(I))
    {
        if (logger)
        {
            logger->WriteLog("Interrupts Disabled so not executing IRQ");
        }
        return; // Skip if interrupts are disabled
    }
    push((PC >> 8) & 0xFF);     // High byte of PC
    push(PC & 0xFF);            // Low byte of PC

    // Push SR with B=0, bit 5=1
    uint8_t status = SR;
    status &= ~0x10; // clear B
    status |= 0x20;  // set unused bit 5
    push(status);

    SetFlag(I, true); // Disable further interrupts

    // Fetch the IRQ vector
    uint16_t irqVector = mem->read(0xFFFE) | (mem->read(0xFFFF) << 8);
    PC = irqVector;

    cycles += 7;
}

void CPU::executeNMI()
{
    //Save CPU state
    push((PC >> 8) & 0xFF); // high byte of PC
    push(PC & 0xFF); // low byte of PC

      // Push SR with B=0, bit 5=1
    uint8_t status = SR;
    status &= ~0x10; // clear B
    status |= 0x20;  // set unused bit 5
    push(status);

    // Disable interrupt flag
    SetFlag(I, true);

    uint16_t nmiVector = mem->read(0xFFFA) | (mem->read(0xFFFB) << 8);
    PC = nmiVector;
    cycles += 7;
}

void CPU::rtsFromQuickLoad()
{
    uint8_t low = pop();
    uint8_t high = pop();
    PC = ((high << 8) | low) & 0xFFFF;
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

    // Check for page boundary crossing
    if ((baseAddress & 0xFF00) != (effectiveAddress & 0xFF00))
    {
        cycles++; // Add extra cycle for boundary crossing
    }

    return effectiveAddress;
}

uint16_t CPU::absYAddress()
{
    uint16_t baseAddress = fetch() | (fetch() << 8);
    uint16_t effectiveAddress = (baseAddress + Y) & 0xFFFF;

    // Check for page boundary crossing
    if ((baseAddress & 0xFF00) != (effectiveAddress & 0xFF00))
    {
        cycles++; // Add extra cycle for boundary crossing
    }

    return effectiveAddress;
}

uint16_t CPU::immediateAddress()
{
    uint16_t address = fetch();
    return address;
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

    // Check for page boundary crossing
    if ((baseAddress & 0xFF00) != (effectiveAddress & 0xFF00))
    {
        cycles++;
    }

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

void CPU::tick()
{
    if (halted) // Jam Halt
    {
        cycles = 1; // always force 1 cycle pending
        cycles--;   // consume it
        totalCycles++;
        return;
    }
    // First check if we need to hold as BA is high
    if (baHold)
    {
        return;
    }

    if (cycles <= 0)
    {
        uint8_t opcode = fetch();

        // Log it
        if (logger)
        {
            std::stringstream message;
            message << "PC = " << std::hex << static_cast<int>(PC - 1) << ", OPCODE = " << std::hex << static_cast<int>(opcode) << ", A = " << std::hex
            << static_cast<int>(A) << ", X = " << std::hex << static_cast<int>(X) << ", Y= " << std::hex << static_cast<int>(Y)
            << ", SP = " << std::hex << static_cast<int>(SP);
            logger->WriteLog(message.str());
        }

        decodeAndExecute(opcode);

        // Update the cycles based on the table
        cycles += CYCLE_COUNTS[opcode];  // Set cycle count for the opcode
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
        if (logger)
        {
            logger->WriteLog("Unhandled opcode: " + std::to_string(opcode));
        }
    }
}

void CPU::SetFlag(flags flag, bool sc)
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
    A &= value;              // Perform AND operation on accumulator

    // Update flags
    SetFlag(C, A & 0x80);    // Set carry flag if MSB is set
    SetFlag(Z, A == 0);      // Set zero flag if result is zero
}

void CPU::ADC(uint8_t opcode)
{
    uint8_t value = 0;
    uint8_t oldA = A;  // Save original accumulator for overflow flag calculation
    uint16_t binaryResult = 0;

    // Determine addressing mode and fetch the operand.
    switch(opcode)
    {
        case 0x61: value = readIndirectX(); break;
        case 0x65: value = readZP();        break;
        case 0x69: value = readImmediate();   break;
        case 0x6D: value = readABS();         break;
        case 0x71: value = readIndirectY();   break;
        case 0x75: value = readZPX();         break;
        case 0x79: value = readABSY();        break;
        case 0x7D: value = readABSX();        break;
    }

    // Compute the unadjusted binary addition result (for overflow flag calculation)
    binaryResult = uint16_t(A) + uint16_t(value) + getFlag(C);

    if (getFlag(D))
    {
        // BCD (Decimal) mode addition:
        // Break A and value into their nibbles.
        int lowA   = A & 0x0F;
        int lowVal = value & 0x0F;
        int highA  = A >> 4;
        int highVal= value >> 4;

        // Add low nibbles along with the carry.
        int lowResult = lowA + lowVal + getFlag(C);
        int carry = 0;
        if (lowResult > 9) {
            lowResult -= 10;
            carry = 1;
        }

        // Add high nibbles plus any carry from the lower nibble.
        int highResult = highA + highVal + carry;
        if (highResult > 9) {
            highResult -= 10;
            SetFlag(C, 1);  // Carry set if result exceeds BCD range.
        } else {
            SetFlag(C, 0);
        }

        // Recombine the nibbles to form the final result.
        A = ((highResult << 4) & 0xF0) | (lowResult & 0x0F);
    }
    else
    {
        // Binary addition mode.
        A = binaryResult & 0xFF;
        SetFlag(C, binaryResult > 0xFF);
    }

    // Update Zero and Negative flags.
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);

    // Overflow flag (V) for ADC is computed from the binary result:
    // Overflow occurs when the sign of oldA and value are the same, but differ from the sign of the result.
    SetFlag(V, (((~(oldA ^ value)) & (oldA ^ binaryResult)) & 0x80) != 0);
}

void CPU::AHX(uint8_t opcode)
{
    uint16_t address = 0;
    uint8_t value = A & X; // Compute A AND X

    // Determine addressing mode
    switch (opcode) {
        case 0x93: // (Indirect), Y
            address = indirectYAddress();
            value &= (address >> 8); // Apply High Byte
            break;
        case 0x9F: // Absolute, Y
            address = absYAddress();
            value &= (address >> 8); // Apply High Byte
            break;
    }

    mem->write(address, value); // Store result at the effective address
}

void CPU::ALR()
{
    uint8_t value = readImmediate();
    A &= value;
    SetFlag(C, A & 0x01);
    A >>= 1;
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);
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
        case 0x31: value = readIndirectY(); break;
        case 0x35: value = readZPX(); break;
        case 0x39: value = readABSY(); break;
        case 0x3D: value = readABSX(); break;
    }
    A &= value;
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);
}

// ARR - AND then ROR (undocumented, opcode $6B)
void CPU::ARR()
{
    uint8_t value = readImmediate();  // Fetch operand
    A &= value;

    // Rotate right with carry in
    bool oldCarry = getFlag(C);
    A = (A >> 1) | (oldCarry ? 0x80 : 0);

    // Flags
    SetFlag(C, (A >> 6) & 1); // Carry = bit 6 of result
    SetFlag(V, ((A >> 6) & 1) ^ ((A >> 5) & 1)); // Overflow = bit6 ^ bit5
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);
}

void CPU::ASL(uint8_t opcode)
{
    uint8_t value = 0;
    uint8_t result = 0;

    switch(opcode)
    {
        case 0x06: // Zero Page
        {
            uint8_t address = fetch();
            value = mem->read(address);
            result = value << 1;
            mem->write(address, result);
            break;
        }
        case 0x0A: // Accumulator
        {
            value = A;           // Capture the original value of A
            result = value << 1; // Perform the shift
            A = result;          // Store the result back in A
            break;
        }
        case 0x0E: // Absolute
        {
            uint8_t lowByte = fetch();
            uint8_t highByte = fetch();
            uint16_t address = (highByte << 8) | lowByte;
            value = mem->read(address);
            result = value << 1;
            mem->write(address, result);
            break;
        }
        case 0x16: // Zero Page, X
        {
            uint8_t zpAddress = (fetch() + X) & 0xFF;
            value = mem->read(zpAddress);
            result = value << 1;
            mem->write(zpAddress, result);
            break;
        }
        case 0x1E: // Absolute, X
        {
            uint16_t baseAddress = fetch() | (fetch() << 8);
            uint16_t effectiveAddress = (baseAddress + X) & 0xFFFF;
            value = mem->read(effectiveAddress);
            result = value << 1;
            mem->write(effectiveAddress, result);
            break;
        }
    }

    // Update flags
    SetFlag(C, value & 0x80);  // Carry flag: Bit 7 of the original value
    SetFlag(Z, result == 0);   // Zero flag: Result is zero
    SetFlag(N, result & 0x80); // Negative flag: Bit 7 of the result
}

void CPU::AXS()
{
    uint8_t value = readImmediate(); // Fetch the immediate value
    uint8_t andResult = A & X; // Perform AND between A and X
    int16_t temp = andResult - value; // Subtract the immediate value

    // Update X with the result (handle wrapping)
    X = temp & 0xFF;

    // Set flags
    SetFlag(C, temp >= 0); // Set carry if no borrow
    SetFlag(Z, X == 0);    // Set zero if result is zero
    SetFlag(N, X & 0x80);  // Set negative if MSB is set
}

void CPU::BCC()
{
    int8_t offset = static_cast<int8_t>(fetch());
    if (!getFlag(C))  // Branch if Carry Clear
    {
        cycles++; // Extra cycle for a taken branch
        uint16_t newPC = (PC + offset) & 0xFFFF; // handle wrapping
        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            cycles++; // Extra cycle if page boundary is crossed
        }
        PC = newPC;  // Update program counter
    }
}

void CPU::BCS()
{
    int8_t offset = static_cast<int8_t>(fetch());

    if (getFlag(C))  // Check if Carry flag is set
    {
        uint16_t oldPC = PC;

        // Update PC by adding the signed offset
        PC = (PC + offset) & 0xFFFF;

         // Extra cycle if branch is taken
        cycles++;

        // Extra cycle if page boundary is crossed
        if ((oldPC & 0xFF00) != (PC & 0xFF00))
        {
            cycles++;
        }
    }
}

void CPU::BEQ()
{
    int8_t offset = static_cast<int8_t>(fetch());
    uint16_t newPC = (PC + offset) & 0xFFFF; // handle wrapping
    if (getFlag(Z))
    {
        cycles++;
        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            cycles ++;
        }
        PC = newPC;
    }
}

void CPU::BIT(uint8_t opcode)
{
    uint8_t value;

    switch(opcode)
    {
        case 0x24: value = readZP(); break;
        case 0x2C: value = readABS(); break;
    }
    uint8_t result = A & value;
    SetFlag(Z, result == 0);
    SetFlag(N, value & 0x80);
    SetFlag(V, value & 0x40);
}

void CPU::BMI()
{
    int8_t offset = static_cast<int8_t>(fetch());
    uint16_t newPC = (PC + offset) & 0xFFFF;

    if (getFlag(N))
    {
        cycles++;
        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            cycles ++;
        }
        PC = newPC;
    }

}

void CPU::BNE()
{
    // Fetch the signed 8-bit branch offset
    int8_t offset = static_cast<int8_t>(fetch());

    // Calculate the potential new program counter
    uint16_t newPC = (PC + offset) & 0xFFFF; // Ensure 16-bit wrapping

    if (!getFlag(Z)) // Branch if Zero flag is NOT set
    {
        cycles++; // Add cycle for taking the branch

        // Check if the branch crosses a page boundary
        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            cycles ++; // Add cycle if page boundary crossed
        }

        // Update the program counter to the new address
        PC = newPC;
    }
}

void CPU::BPL()
{
    int8_t offset = static_cast<int8_t>(fetch());

    if (!getFlag(N))  // Check if Negative flag is not set (i.e., positive)
    {
        uint16_t oldPC = PC;
        PC = (PC + offset) & 0xFFFF;

        // Base cycles for branch instruction
        cycles ++;

        // Check if crossing a page boundary
        if ((oldPC & 0xFF00) != (PC & 0xFF00))
        {
            cycles++;  // Extra cycle for page boundary crossing
        }
    }
}

void CPU::BRK() {
    uint16_t newPC = PC + 1;  // Point to the instruction after BRK

    push((newPC >> 8) & 0xFF);
    push(newPC & 0xFF);

    // Set the break flag (B flag) in the processor status
    SetFlag(B, 1);

    uint8_t status = SR;
    status |= 0x10; // set B flag
    status |= 0x20; // bit 5 always set
    push(status);

    // Set the interrupt disable flag (I flag) in the processor status
    SetFlag(I, 1);  // Set the interrupt disable flag
    SetFlag(B, 0); // Clear the B flag

    // Load the new Program Counter (PC) from the interrupt vector (0xFFFE and 0xFFFF)
    uint16_t interruptVector = mem->read(0xFFFE) | (mem->read(0xFFFF) << 8);

    // Set the PC to the interrupt vector
    PC = interruptVector;
}

void CPU::BVC()
{
    int8_t offset = static_cast<int8_t>(fetch());
    uint16_t newPC = PC;

    if (!getFlag(V))  // Branch if Overflow is clear
    {
        PC += offset;
        cycles ++;

        // Check if crossing a page boundary
        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            cycles++;  // Extra cycle for page boundary crossing
        }
    }
}

void CPU::BVS()
{
    int8_t offset = static_cast<int8_t>(fetch());
    uint16_t newPC = PC;

    if (getFlag(V))  // Branch if Overflow is set
    {
        PC += offset;
        cycles ++;

        // Check if crossing a page boundary
        if ((PC & 0xFF00) != (newPC & 0xFF00))
        {
            cycles++;  // Extra cycle for page boundary crossing
        }
    }
}

void CPU::CMP(uint8_t opcode)
{
    uint8_t value = 0;
    uint8_t result = 0;

    switch(opcode)
    {
        case 0xC1: value = readIndirectX(); break;
        case 0xC5: value = readZP(); break;
        case 0xC9: value = readImmediate(); break;
        case 0xCD: value = readABS(); break;
        case 0xD1: value = readIndirectY(); break;
        case 0xD5: value = readZPX(); break;
        case 0xD9: value = readABSY(); break;
        case 0xDD: value = readABSX(); break;
    }
    result = A - value;

    SetFlag(Z, result == 0);
    SetFlag(N, result & 0x80);
    SetFlag(C, A >= value);
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
    SetFlag(Z, (X - value) == 0);
    SetFlag(N, (X - value) & 0x80);
    SetFlag(C, X >= value);
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
    SetFlag(Z, (Y - value) == 0);
    SetFlag(N, (Y - value) & 0x80);
    SetFlag(C, Y >= value);
}

void CPU::DCP(uint8_t opcode)
{
    uint16_t address = 0;
    uint8_t value = 0;

    switch (opcode)
    {
        case 0xC3: address = indirectXAddress(); break;
        case 0xC7: address = zpAddress(); break;
        case 0xCF: address = absAddress(); break;
        case 0xD3: address = indirectYAddress(); break;
        case 0xD7: address = zpXAddress(); break;
        case 0xDB: address = absYAddress(); break;
        case 0xDF: address = absXAddress(); break;
    }

    // Decrement memory value
    value = mem->read(address);
    value = (value - 1) & 0xFF; // Ensure 8-bit wrapping
    mem->write(address, value);

    // Compare with accumulator
    uint8_t result = A - value;

    // Update flags
    SetFlag(C, A >= value);
    SetFlag(Z, result == 0);
    SetFlag(N, result & 0x80);
}

void CPU::DEC(uint8_t opcode)
{
    uint8_t value = 0;

    switch(opcode)
    {
        case 0xC6:
        {
            uint8_t address = zpAddress();
            value = mem->read(address);
            value--;
            mem->write(address,value);
            break;
        }
        case 0xCE:
        {
            uint16_t address = absAddress();
            value = mem->read(address);
            value--;
            mem->write(address,value);
            break;
        }
        case 0xD6:
        {
            uint8_t address = zpXAddress();
            value = mem->read(address);
            value--;
            mem->write(address, value);
            break;
        }
        case 0xDE:
        {
            uint16_t address = absXAddress();
            value = mem->read(address);
            value--;
            mem->write(address,value);
            break;
        }
    }

    SetFlag(Z, value == 0);
    SetFlag(N, value & 0x80);
}

void CPU::DEX()
{
    X--;
    SetFlag(Z, X == 0);
    SetFlag(N, X & 0x80);
}

void CPU::DEY()
{
    Y--;
    SetFlag(Z, Y == 0);
    SetFlag(N, Y & 0x80);
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
        case 0x51: value = readIndirectY(); break;
        case 0x55: value = readZPX(); break;
        case 0x59: value = readABSY(); break;
        case 0x5D: value = readABSX(); break;
    }
    A ^= value;
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);
}

void CPU::INC(uint8_t opcode)
{
    uint8_t value = 0;

    switch(opcode)
    {
        case 0xE6:
        {
            uint8_t address = zpAddress();
            value = mem->read(address);
            value++;
            mem->write(address,value);
            break;
        }
        case 0xEE:
        {
            uint16_t address = absAddress();
            value = mem->read(address);
            value++;
            mem->write(address,value);
            break;
        }
        case 0xF6:
        {
            uint8_t address = zpXAddress();
            value = mem->read(address);
            value++;
            mem->write(address,value);
            break;
        }
        case 0xFE:
        {
            uint16_t address = absXAddress();
            value = mem->read(address);
            value++;
            mem->write(address,value);
            break;
        }
    }

    SetFlag(Z, value == 0);
    SetFlag(N, value & 0x80);
}

void CPU::INX()
{
    X = (X + 1) & 0xFF;
    SetFlag(Z, X == 0);
    SetFlag(N, X & 0x80);
}

void CPU::INY()
{
    Y = (Y + 1) & 0xFF;
    SetFlag(Z, Y == 0);
    SetFlag(N, Y & 0x80);
}

void CPU::ISC(uint8_t opcode)
{
    uint16_t address = 0;
    uint8_t value = 0;

    switch (opcode)
    {
        case 0xE3: address = indirectXAddress(); break;
        case 0xE7: address = zpAddress(); break;
        case 0xEF: address = absAddress(); break;
        case 0xF3: address = indirectYAddress(); break;
        case 0xF7: address = zpXAddress(); break;
        case 0xFB: address = absYAddress(); break;
        case 0xFF: address = absXAddress(); break;
    }
    // Increment memory value
    value = mem->read(address);
    value = (value + 1) & 0xFF; // Ensure 8-bit wrapping
    mem->write(address, value);

    // Perform SBC (Subtract with Carry)
    uint16_t tempResult = uint16_t(A) - uint16_t(value) - (1 - getFlag(C));

    // Update accumulator
    A = tempResult & 0xFF;

    // Update flags
    SetFlag(C, tempResult < 0x100);
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);
    SetFlag(V, ((A ^ value) & (A ^ tempResult) & 0x80) != 0);
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
    uint8_t value = readABS();
    uint8_t result = value & SP;  // Perform AND with the stack pointer

    // Update A, X, and SP with the result
    A = result;
    X = result;
    SP = result;

    // Set flags
    SetFlag(Z, result == 0);     // Zero flag
    SetFlag(N, result & 0x80);   // Negative flag
}

void CPU::LAX(uint8_t opcode)
{
    uint16_t address = 0;
    uint8_t value = 0;

    switch (opcode)
    {
        case 0xA3: address = indirectXAddress(); break;
        case 0xA7: address = zpAddress(); break;
        case 0xAB: address = immediateAddress(); break;
        case 0xAF: address = absAddress(); break;
        case 0xB3: address = indirectYAddress(); break;
        case 0xB7: address = zpYAddress(); break;
        case 0xBF: address = absYAddress(); break;
    }
    // Load value from memory into A and X
    value = mem->read(address);
    A = value;
    X = value;

    // Update flags
    SetFlag(Z, value == 0);
    SetFlag(N, value & 0x80);
}

void CPU::LDA(uint8_t opcode)
{
    uint8_t value = 0;

    switch (opcode)
    {
        case 0xA1: // LDA ($nn,X) - Indirect Indexed X
        {
            value = readIndirectX();
            break;
        }

        case 0xA5: // LDA $nn - Zero Page
            value = readZP();
            break;

        case 0xA9: // LDA #$nn - Immediate
            value = readImmediate();
            break;

        case 0xAD: // LDA $nnnn - Absolute
        {
            value = readABS();
            break;
        }

        case 0xB1: // LDA ($nn),Y - Indirect Indexed Y
        {
            value = readIndirectY();
            break;
        }

        case 0xB5: // LDA $nn,X - Zero Page,X
            value = readZPX();
            break;

        case 0xB9: // LDA $nnnn,Y - Absolute,Y
        {
            value = readABSY();
            break;
        }

        case 0xBD: // LDA $nnnn,X - Absolute,X
        {
            value = readABSX();
            break;
        }
    }

    // Store the value in the accumulator
    A = value;

    // Set flags
    SetFlag(Z, A == 0);   // Zero flag
    SetFlag(N, A & 0x80); // Negative flag
}

void CPU::LDX(uint8_t opcode)
{
    switch(opcode)
    {
        case 0xA2: X = readImmediate(); break;
        case 0xA6: X = readZP(); break;
        case 0xAE: X = readABS(); break;
        case 0xB6: X = readZPY(); break;
        case 0xBE: X = readABSY(); break;
    }
    SetFlag(Z, X == 0);
    SetFlag(N, X & 0x80);
}

void CPU::LDY(uint8_t opcode)
{
    switch(opcode)
    {
        case 0xA0: Y = readImmediate(); break;
        case 0xA4: Y = readZP(); break;
        case 0xAC: Y = readABS(); break;
        case 0xB4: Y = readZPX(); break;
        case 0xBC: Y = readABSX(); break;
    }
    SetFlag(Z, Y == 0);
    SetFlag(N, Y & 0x80);
}

void CPU::LSR(uint8_t opcode)
{
    switch(opcode)
    {
        case 0x46:
        {
            uint8_t address = zpAddress();
            uint8_t value = mem->read(address);

            bool carry = value & 0x01;
            value >>= 1;

            mem->write(address,value);

            SetFlag(C, carry);
            SetFlag(Z, value == 0);
            SetFlag(N, false);

            break;
        }
        case 0x4A:
        {
            bool carry = A & 0x01;
            A >>= 1;

            SetFlag(C, carry);
            SetFlag(Z, A == 0);
            SetFlag(N, false);


            break;
        }
        case 0x4E:
        {
            uint16_t address = absAddress();
            uint8_t value = mem->read(address);

            bool carry = value & 0x01;
            value >>= 1;

            SetFlag(C, carry);
            SetFlag(Z, value == 0);
            SetFlag(N, false);

            mem->write(address,value);

            break;
        }
        case 0x56:
        {
            uint8_t address = zpXAddress();
            uint8_t value = mem->read(address);

            bool carry = value & 0x01;
            value >>= 1;

            SetFlag(C, carry);
            SetFlag(Z, value == 0);
            SetFlag(N, false);

            mem->write(address,value);

            break;
        }
        case 0x5E:
        {
            uint16_t address = absXAddress();
            uint8_t value = mem->read(address);

            bool carry = value & 0x01;
            value >>= 1;

            SetFlag(C, carry);
            SetFlag(Z, value == 0);
            SetFlag(N, false);

            mem->write(address,value);

            break;
        }
    }
}

void CPU::NOP(uint8_t opcode)
{
    switch (opcode)
    {
        case 0xEA:
        case 0x1A: case 0x3A: case 0x5A: case 0x7A:
        case 0xDA: case 0xFA:
            // 1 byte, implied
            break;

        case 0x04: case 0x44: case 0x64: // Zero-page
            fetch();
            break;

        case 0x14: case 0x34: case 0x54: case 0x74:
        case 0xD4: case 0xF4: // Zero-page,X
            fetch();
            break;

        case 0x0C: // Absolute
            fetch(); fetch();
            break;

        case 0x1C: case 0x3C: case 0x5C: case 0x7C:
        case 0xDC: case 0xFC: // Absolute,X
            readABSX();
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
        case 0x11: value = readIndirectY(); break;
        case 0x15: value = readZPX(); break;
        case 0x19: value = readABSY(); break;
        case 0x1D: value = readABSX(); break;
    }
    A |= value;
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);
}

void CPU::PHA()
{
    push(A);
}

void CPU::PHP()
{
    push(SR | 0x30);
}

void CPU::PLA()
{
  A = pop();
  SetFlag(Z, A == 0);
  SetFlag(N, A & 0x80);
}

void CPU::PLP()
{
    uint8_t status = pop();
    status |= 0x20;
    SR = status;
}

void CPU::RLA(uint8_t opcode)
{
    uint16_t address = 0;
    uint8_t value = 0;

    // Determine addressing mode
    switch (opcode) {
        case 0x23: address = readIndirectX(); break;
        case 0x27: address = readZP(); break;
        case 0x2F: address = readABS(); break;
        case 0x33: address = readIndirectY(); break;
        case 0x37: address = readZPX(); break;
        case 0x3B: address = readABSY(); break;
        case 0x3F: address = readABSX(); break;
    }
    // Perform Rotate Left (ROL) on memory value
    value = mem->read(address);
    bool carry = value & 0x80;
    value = (value << 1) | (getFlag(C) ? 1 : 0);
    mem->write(address, value);

    // Update Carry flag
    SetFlag(C, carry);

    // AND the result with the accumulator
    A &= value;

    // Update flags
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);
}

void CPU::ROL(uint8_t opcode)
{
    uint8_t value = 0;
    bool carry;

    switch(opcode)
    {
        case 0x26:
        {
            uint8_t address = fetch();
            value = mem->read(address);
            carry = value & 0x80;
            value  = (value << 1) | (getFlag(C) ? 1 : 0);
            mem->write(address,value);
            break;
        }
        case 0x2A:
        {
            value = A;
            carry = value & 0x80;
            value = (value << 1) | (getFlag(C) ? 1 : 0);
            A = value;
            break;
        }
        case 0x2E:
        {
            uint16_t address = fetch() | (fetch() << 8);
            value = mem->read(address);
            carry = value & 0x80;
            value  = (value << 1) | (getFlag(C) ? 1 :0);
            mem->write(address,value);
            break;
        }
        case 0x36:
        {
            uint8_t zpAddress = (fetch() + X) & 0xFF;
            uint8_t value = mem->read(zpAddress);
            carry = value & 0x80;
            value = (value << 1) | (getFlag(C) ? 1 : 0);
            mem->write(zpAddress, value);
            break;
        }
        case 0x3E:
        {
            uint16_t baseAddress = fetch() | (fetch() << 8);
            uint16_t effectiveAddress = baseAddress + X;
            value = mem->read(effectiveAddress);
            carry = value & 0x80;
            value  = (value << 1) | (getFlag(C) ? 1 :0);
            mem->write(effectiveAddress,value);
            break;
        }
    }
    SetFlag(N, value & 0x80);
    SetFlag(Z, value == 0);
    SetFlag(C, carry);
}

void CPU::ROR(uint8_t opcode)
{
    uint8_t value = 0;
    bool newCarry = false;
    bool oldCarry = getFlag(C);

    switch(opcode)
    {
        case 0x66: // Zero Page
        {
            uint8_t address = fetch();
            value = mem->read(address);
            newCarry = (value & 0x01) != 0;
            value = (value >> 1) | (oldCarry ? 0x80 : 0);
            mem->write(address, value);
            break;
        }
        case 0x6A: // Accumulator
        {
            value = A;
            newCarry = (value & 0x01) != 0;
            value = (value >> 1) | (oldCarry ? 0x80 : 0);
            A = value;
            break;
        }
        case 0x6E: // Absolute
        {
            uint16_t address = fetch() | (fetch() << 8);
            value = mem->read(address);
            newCarry = (value & 0x01) != 0;
            value = (value >> 1) | (oldCarry ? 0x80 : 0);
            mem->write(address, value);
            break;
        }
        case 0x76: // Zero Page, X-indexed
        {
            uint8_t zpAddress = (fetch() + X) & 0xFF;
            value = mem->read(zpAddress);
            newCarry = (value & 0x01) != 0;
            value = (value >> 1) | (oldCarry ? 0x80 : 0);
            mem->write(zpAddress, value);
            break;
        }
        case 0x7E: // Absolute, X-indexed
        {
            uint16_t baseAddress = fetch() | (fetch() << 8);
            uint16_t effectiveAddress = (baseAddress + X) & 0xFFFF;
            value = mem->read(effectiveAddress);
            newCarry = (value & 0x01) != 0;
            value = (value >> 1) | (oldCarry ? 0x80 : 0);
            mem->write(effectiveAddress, value);
            break;
        }
    }

    // Update processor flags
    SetFlag(N, value & 0x80);
    SetFlag(Z, value == 0);
    SetFlag(C, newCarry);
}

void CPU::RRA(uint8_t opcode)
{
    uint16_t address = 0;
    uint8_t value = 0;

    // Determine addressing mode
    switch (opcode) {
        case 0x63: address = indirectXAddress(); break;
        case 0x67: address = zpAddress(); break;
        case 0x6F: address = absAddress(); break;
        case 0x73: address = indirectYAddress(); break;
        case 0x77: address = zpXAddress(); break;
        case 0x7B: address = absYAddress(); break;
        case 0x7F: address = absXAddress(); break;
    }
    // Perform Rotate Right (ROR) on memory value
    value = mem->read(address);
    bool carry = value & 0x01; // Capture old LSB
    value = (value >> 1) | (getFlag(C) ? 0x80 : 0); // Shift right and insert carry into MSB
    mem->write(address, value); // Write updated value back to memory

    // Update Carry flag from old LSB
    SetFlag(C, carry);

    // Perform ADC (Add with Carry) with the accumulator
    uint16_t tempResult = uint16_t(A) + uint16_t(value) + getFlag(C);

    // Update accumulator
    A = tempResult & 0xFF;

    // Update flags
    SetFlag(C, tempResult > 0xFF); // Carry flag: Set if addition overflows
    SetFlag(Z, A == 0);           // Zero flag: Set if result is zero
    SetFlag(N, A & 0x80);         // Negative flag: Set if MSB is 1
    SetFlag(V, (~(A ^ value) & (A ^ tempResult) & 0x80) != 0); // Overflow flag
}

void CPU::RTI()
{
    // Restore the status register
    SR = pop();

    // Clear the break flag
    SR &= 0xEF;

    // Ensure unused bit 5 is always 1
    SR |= 0x20;

    // Restore the PC preinterrupt
    uint8_t lowByte = pop();
    uint8_t highByte = pop();

    PC = (highByte << 8) | lowByte;
}

void CPU::RTS()
{
        uint8_t lowByte = pop();  // Pop low byte first
        uint8_t highByte = pop();  // Pop high byte second

        // Set the PC to the return address
        PC = (highByte << 8) | lowByte;

        PC = (PC + 1) & 0xFFFF;
}

void CPU::SAX(uint8_t opcode)
{
    uint16_t address;
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
    uint8_t oldA = A; // Save the original accumulator for later use (overflow flag)
    uint16_t binaryResult = 0;

    // Determine addressing mode
    switch (opcode) {
        case 0xE1: value = readIndirectX(); break;
        case 0xE5: value = readZP(); break;
        case 0xE9: value = readImmediate(); break;
        case 0xED: value = readABS(); break;
        case 0xF1: value = readIndirectY(); break;
        case 0xF5: value = readZPX(); break;
        case 0xF9: value = readABSY(); break;
        case 0xFD: value = readABSX(); break;
    }
    binaryResult = uint16_t(A) - uint16_t(value) - (1 - getFlag(C));
    if (getFlag(D))
    {
         // BCD (Decimal) mode subtraction.
        // First work on the lower nibble.
        int al = (A & 0x0F) - (value & 0x0F) - (1 - getFlag(C));
        int ah = (A >> 4) - (value >> 4);
        // If the lower nibble underflows, add 10 (0xA) and borrow from the high nibble.
        if (al < 0) {
            al += 10;
            ah--;
        }
        // If the high nibble underflows, adjust it and clear the carry flag (borrow occurred).
        if (ah < 0) {
            ah += 10;
            SetFlag(C, 0);
        } else {
            SetFlag(C, 1);
        }
        // Recombine the adjusted nibbles.
        A = ((ah << 4) & 0xF0) | (al & 0x0F);
    }
    else
    {
        // Binary mode subtraction.
        A = binaryResult & 0xFF;
        // In SBC, the carry flag is set if no borrow occurred.
        SetFlag(C, binaryResult < 0x100);
    }

    // Update Zero and Negative flags.
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);

    // For the overflow flag, use the binary (unadjusted) result.
    // Overflow for SBC is set if the sign of the original accumulator differs
    // from the sign of the result in a way that indicates an out-of–range result.
    SetFlag(V, ((oldA ^ A) & (oldA ^ value) & 0x80) != 0);
}

void CPU::SHX()
{
    uint16_t baseAddress = fetch() | (fetch() << 8);  // Fetch the absolute address
    uint16_t effectiveAddress = (baseAddress + Y) & 0xFFFF;  // Add Y index with wrapping
    uint8_t highByte = (effectiveAddress >> 8) + 1;  // Increment the high byte
    uint8_t value = X & highByte;  // Logical AND of X and high byte + 1

    mem->write(effectiveAddress, value);  // Write the result to memory
}

void CPU::SHY()
{
    uint16_t baseAddress = fetch() | (fetch() << 8);  // Fetch the absolute address
    uint16_t effectiveAddress = (baseAddress + X) & 0xFFFF;  // Add X index with wrapping
    uint8_t highByte = (effectiveAddress >> 8) + 1;  // Increment the high byte
    uint8_t value = Y & highByte;  // Logical AND of Y and high byte + 1

    mem->write(effectiveAddress, value);  // Write the result to memory
}

void CPU::SLO(uint8_t opcode)
{
    uint16_t address = 0;

    // Select addressing mode
    switch (opcode)
    {
        case 0x03: address = indirectXAddress(); break;  // (Indirect,X)
        case 0x07: address = zpAddress();        break;  // Zero Page
        case 0x0F: address = absAddress();       break;  // Absolute
        case 0x13: address = indirectYAddress(); break;  // (Indirect),Y
        case 0x17: address = zpXAddress();       break;  // Zero Page,X
        case 0x1B: address = absYAddress();      break;  // Absolute,Y
        case 0x1F: address = absXAddress();      break;  // Absolute,X
    }

    // Read, shift left, and write back
    uint8_t value = mem->read(address);
    SetFlag(C, value & 0x80);  // old bit 7 → Carry
    value <<= 1;
    mem->write(address, value);

    // ORA with accumulator
    A |= value;
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);
}

void CPU::SRE(uint8_t opcode)
{
    uint16_t address = 0;

    // Select addressing mode
    switch (opcode)
    {
        case 0x43: address = indirectXAddress(); break;  // (Indirect,X)
        case 0x47: address = zpAddress();        break;  // Zero Page
        case 0x4F: address = absAddress();       break;  // Absolute
        case 0x53: address = indirectYAddress(); break;  // (Indirect),Y
        case 0x57: address = zpXAddress();       break;  // Zero Page,X
        case 0x5B: address = absYAddress();      break;  // Absolute,Y
        case 0x5F: address = absXAddress();      break;  // Absolute,X
    }

    // Perform LSR on memory value
    uint8_t value = mem->read(address);
    SetFlag(C, value & 0x01);  // old bit 0 → Carry
    value >>= 1;
    mem->write(address, value);

    // EOR with accumulator
    A ^= value;
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);
}

void CPU::STA(uint8_t opcode)
{
    uint16_t address = 0;

    switch (opcode)
    {
        case 0x81: // STA ($nn,X) - Indexed Indirect
        {
            address = indirectXAddress();
            break;
        }

        case 0x85: // STA $nn - Zero Page
        {
            address = zpAddress();
            break;
        }
        case 0x8D: // STA $nnnn - Absolute
        {
            address = absAddress();
            break;
        }

        case 0x91: // STA ($nn),Y - Indirect Indexed
        {
            address = indirectYAddress();
            break;
        }

        case 0x95: // STA $nn,X - Zero Page,X
        {
            address = zpXAddress();
            break;
        }

        case 0x99: // STA $nnnn,Y - Absolute,Y
        {
            address = absYAddress();
            break;
        }
        case 0x9D: // STA $nnnn,X - Absolute,X
        {
            address = absXAddress();
            break;
        }
    }
    mem->write(address, A);
}

void CPU::STX(uint8_t opcode)
{
    uint16_t address = 0;

    switch(opcode)
    {
        case 0x86:
        {
            address = zpAddress();
            break;
        }
        case 0x8E:
        {
            address = absAddress();
            break;
        }
      case 0x96: // STX Zero Page,Y
        {
            address = zpYAddress();
            break;
        }
    }
    mem->write(address, X);
}

void CPU::STY(uint8_t opcode)
{
    uint16_t address = 0;

    switch (opcode)
    {
        case 0x84: // Zero Page
            address = zpAddress();
            break;

        case 0x8C: // Absolute
            address = absAddress();
            break;

        case 0x94: // Zero Page,X
            address = zpXAddress();
            break;
    }

    mem->write(address, Y);
}


void CPU::TAS()
{
    uint16_t effectiveAddress = absYAddress();
    uint8_t highByte = (effectiveAddress >> 8) + 1;  // Increment the high byte

    uint8_t value = A & X;  // Compute A AND X
    SP = value;             // Transfer to stack pointer

    // Write SP & (High Byte + 1) to memory
    uint8_t memValue = SP & highByte;
    mem->write(effectiveAddress, memValue);
}

void CPU::TAX()
{
    X = A;
    SetFlag(Z, X == 0);
    SetFlag(N, X & 0x80);
}

void CPU::TAY()
{
    Y = A;
    SetFlag(Z, Y == 0);
    SetFlag(N, Y & 0x80);
}

void CPU::TSX()
{
    X = SP;
    SetFlag(Z, X == 0);
    SetFlag(N, X & 0x80);
}

void CPU::TXA()
{
    A = X;
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);
}

void CPU::TYA()
{
    A = Y;
    SetFlag(Z, A == 0);
    SetFlag(N, A & 0x80);
}

void CPU::XAA()
{
    uint8_t address = immediateAddress(); // Fetch the immediate value
    A = X & address;           // Compute X AND Immediate

    // Update flags
    SetFlag(Z, A == 0);          // Zero flag
    SetFlag(N, A & 0x80);        // Negative flag
}
