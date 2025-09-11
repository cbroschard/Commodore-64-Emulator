// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "6502/Opcode6502.h"

// Opcode table
const InstructionInfo OPCODES[256] =
{
    /* 0x00 */ {"BRK", AddressingMode::Implied,     1, 0x00}, // Force Interrupt
    /* 0x01 */ {"ORA", AddressingMode::IndirectX,   2, 0x01}, // ORA (zp,X)
    /* 0x02 */ {"KIL", AddressingMode::Implied,     1, 0x02}, // (undoc) JAM/KIL - locks CPU
    /* 0x03 */ {"SLO", AddressingMode::IndirectX,   2, 0x03}, // (undoc) ASL + ORA (zp,X)
    /* 0x04 */ {"NOP", AddressingMode::ZeroPage,    2, 0x04}, // (undoc) NOP zp
    /* 0x05 */ {"ORA", AddressingMode::ZeroPage,    2, 0x05}, // ORA zp
    /* 0x06 */ {"ASL", AddressingMode::ZeroPage,    2, 0x06}, // ASL zp
    /* 0x07 */ {"SLO", AddressingMode::ZeroPage,    2, 0x07}, // (undoc) ASL + ORA zp
    /* 0x08 */ {"PHP", AddressingMode::Implied,     1, 0x08}, // Push Processor Status
    /* 0x09 */ {"ORA", AddressingMode::Immediate,   2, 0x09}, // ORA #$nn
    /* 0x0A */ {"ASL", AddressingMode::Accumulator, 1, 0x0A}, // ASL A
    /* 0x0B */ {"ANC", AddressingMode::Immediate,   2, 0x0B}, // (undoc) AND + move bit7->Carry
    /* 0x0C */ {"NOP", AddressingMode::Absolute,    3, 0x0C}, // (undoc) NOP abs
    /* 0x0D */ {"ORA", AddressingMode::Absolute,    3, 0x0D}, // ORA abs
    /* 0x0E */ {"ASL", AddressingMode::Absolute,    3, 0x0E}, // ASL abs
    /* 0x0F */ {"SLO", AddressingMode::Absolute,    3, 0x0F}, // (undoc) ASL + ORA abs

    /* 0x10 */ {"BPL", AddressingMode::Relative,    2, 0x10}, // Branch on Plus
    /* 0x11 */ {"ORA", AddressingMode::IndirectY,   2, 0x11}, // ORA (zp),Y
    /* 0x12 */ {"KIL", AddressingMode::Implied,     1, 0x12}, // (undoc) JAM/KIL - locks CPU
    /* 0x13 */ {"SLO", AddressingMode::IndirectY,   2, 0x13}, // (undoc) ASL + ORA (zp),Y
    /* 0x14 */ {"NOP", AddressingMode::ZeroPageX,   2, 0x14}, // (undoc) NOP zp,X
    /* 0x15 */ {"ORA", AddressingMode::ZeroPageX,   2, 0x15}, // ORA zp,X
    /* 0x16 */ {"ASL", AddressingMode::ZeroPageX,   2, 0x16}, // ASL zp,X
    /* 0x17 */ {"SLO", AddressingMode::ZeroPageX,   2, 0x17}, // (undoc) ASL + ORA zp,X
    /* 0x18 */ {"CLC", AddressingMode::Implied,     1, 0x18}, // Clear Carry
    /* 0x19 */ {"ORA", AddressingMode::AbsoluteY,   3, 0x19}, // ORA abs,Y
    /* 0x1A */ {"NOP", AddressingMode::Implied,     1, 0x1A}, // (undoc) NOP (1-byte)
    /* 0x1B */ {"SLO", AddressingMode::AbsoluteY,   3, 0x1B}, // (undoc) ASL + ORA abs,Y
    /* 0x1C */ {"NOP", AddressingMode::AbsoluteX,   3, 0x1C}, // (undoc) NOP abs,X
    /* 0x1D */ {"ORA", AddressingMode::AbsoluteX,   3, 0x1D}, // ORA abs,X
    /* 0x1E */ {"ASL", AddressingMode::AbsoluteX,   3, 0x1E}, // ASL abs,X
    /* 0x1F */ {"SLO", AddressingMode::AbsoluteX,   3, 0x1F}, // (undoc) ASL + ORA abs,X

    /* 0x20 */ {"JSR", AddressingMode::Absolute,    3, 0x20}, // Jump to Subroutine
    /* 0x21 */ {"AND", AddressingMode::IndirectX,   2, 0x21}, // AND (zp,X)
    /* 0x22 */ {"KIL", AddressingMode::Implied,     1, 0x22}, // (undoc) JAM/KIL - locks CPU
    /* 0x23 */ {"RLA", AddressingMode::IndirectX,   2, 0x23}, // (undoc) ROL + AND (zp,X)
    /* 0x24 */ {"BIT", AddressingMode::ZeroPage,    2, 0x24}, // BIT zp
    /* 0x25 */ {"AND", AddressingMode::ZeroPage,    2, 0x25}, // AND zp
    /* 0x26 */ {"ROL", AddressingMode::ZeroPage,    2, 0x26}, // ROL zp
    /* 0x27 */ {"RLA", AddressingMode::ZeroPage,    2, 0x27}, // (undoc) ROL + AND zp
    /* 0x28 */ {"PLP", AddressingMode::Implied,     1, 0x28}, // Pull Processor Status
    /* 0x29 */ {"AND", AddressingMode::Immediate,   2, 0x29}, // AND #$nn
    /* 0x2A */ {"ROL", AddressingMode::Accumulator, 1, 0x2A}, // ROL A
    /* 0x2B */ {"ANC", AddressingMode::Immediate,   2, 0x2B}, // (undoc) AND + move bit7->Carry
    /* 0x2C */ {"BIT", AddressingMode::Absolute,    3, 0x2C}, // BIT abs
    /* 0x2D */ {"AND", AddressingMode::Absolute,    3, 0x2D}, // AND abs
    /* 0x2E */ {"ROL", AddressingMode::Absolute,    3, 0x2E}, // ROL abs
    /* 0x2F */ {"RLA", AddressingMode::Absolute,    3, 0x2F}, // (undoc) ROL + AND abs

    /* 0x30 */ {"BMI", AddressingMode::Relative,    2, 0x30}, // Branch on Minus
    /* 0x31 */ {"EOR", AddressingMode::IndirectY,   2, 0x31}, // EOR (zp),Y
    /* 0x32 */ {"KIL", AddressingMode::Implied,     1, 0x32}, // (undoc) JAM/KIL - locks CPU
    /* 0x33 */ {"SRE", AddressingMode::IndirectY,   2, 0x33}, // (undoc) LSR + EOR (zp),Y
    /* 0x34 */ {"NOP", AddressingMode::ZeroPageX,   2, 0x34}, // (undoc) NOP zp,X
    /* 0x35 */ {"EOR", AddressingMode::ZeroPageX,   2, 0x35}, // EOR zp,X
    /* 0x36 */ {"LSR", AddressingMode::ZeroPageX,   2, 0x36}, // LSR zp,X
    /* 0x37 */ {"SRE", AddressingMode::ZeroPageX,   2, 0x37}, // (undoc) LSR + EOR zp,X
    /* 0x38 */ {"SEC", AddressingMode::Implied,     1, 0x38}, // Set Carry
    /* 0x39 */ {"EOR", AddressingMode::AbsoluteY,   3, 0x39}, // EOR abs,Y
    /* 0x3A */ {"NOP", AddressingMode::Implied,     1, 0x3A}, // (undoc) NOP (1-byte)
    /* 0x3B */ {"SRE", AddressingMode::AbsoluteY,   3, 0x3B}, // (undoc) LSR + EOR abs,Y
    /* 0x3C */ {"NOP", AddressingMode::AbsoluteX,   3, 0x3C}, // (undoc) NOP abs,X
    /* 0x3D */ {"EOR", AddressingMode::AbsoluteX,   3, 0x3D}, // EOR abs,X
    /* 0x3E */ {"LSR", AddressingMode::AbsoluteX,   3, 0x3E}, // LSR abs,X
    /* 0x3F */ {"SRE", AddressingMode::AbsoluteX,   3, 0x3F}, // (undoc) LSR + EOR abs,X

    /* 0x40 */ {"RTI", AddressingMode::Implied,     1, 0x40}, // Return from Interrupt
    /* 0x41 */ {"EOR", AddressingMode::IndirectX,   2, 0x41}, // EOR (zp,X)
    /* 0x42 */ {"KIL", AddressingMode::Implied,     1, 0x42}, // (undoc) JAM/KIL - locks CPU
    /* 0x43 */ {"SRE", AddressingMode::IndirectX,   2, 0x43}, // (undoc) LSR + EOR (zp,X)
    /* 0x44 */ {"NOP", AddressingMode::ZeroPage,    2, 0x44}, // (undoc) NOP zp
    /* 0x45 */ {"EOR", AddressingMode::ZeroPage,    2, 0x45}, // EOR zp
    /* 0x46 */ {"LSR", AddressingMode::ZeroPage,    2, 0x46}, // LSR zp
    /* 0x47 */ {"SRE", AddressingMode::ZeroPage,    2, 0x47}, // (undoc) LSR + EOR zp
    /* 0x48 */ {"PHA", AddressingMode::Implied,     1, 0x48}, // Push Accumulator
    /* 0x49 */ {"EOR", AddressingMode::Immediate,   2, 0x49}, // EOR #$nn
    /* 0x4A */ {"LSR", AddressingMode::Accumulator, 1, 0x4A}, // LSR A
    /* 0x4B */ {"ALR", AddressingMode::Immediate,   2, 0x4B}, // (undoc) AND + LSR
    /* 0x4C */ {"JMP", AddressingMode::Absolute,    3, 0x4C}, // JMP abs
    /* 0x4D */ {"EOR", AddressingMode::Absolute,    3, 0x4D}, // EOR abs
    /* 0x4E */ {"LSR", AddressingMode::Absolute,    3, 0x4E}, // LSR abs
    /* 0x4F */ {"SRE", AddressingMode::Absolute,    3, 0x4F}, // (undoc) LSR + EOR abs

    /* 0x50 */ {"BVC", AddressingMode::Relative,    2, 0x50}, // Branch if Overflow Clear
    /* 0x51 */ {"EOR", AddressingMode::IndirectY,   2, 0x51}, // EOR (zp),Y
    /* 0x52 */ {"KIL", AddressingMode::Implied,     1, 0x52}, // (undoc) JAM/KIL - locks CPU
    /* 0x53 */ {"SRE", AddressingMode::IndirectY,   2, 0x53}, // (undoc) LSR + EOR (zp),Y
    /* 0x54 */ {"NOP", AddressingMode::ZeroPageX,   2, 0x54}, // (undoc) NOP zp,X
    /* 0x55 */ {"EOR", AddressingMode::ZeroPageX,   2, 0x55}, // EOR zp,X
    /* 0x56 */ {"LSR", AddressingMode::ZeroPageX,   2, 0x56}, // LSR zp,X
    /* 0x57 */ {"SRE", AddressingMode::ZeroPageX,   2, 0x57}, // (undoc) LSR + EOR zp,X
    /* 0x58 */ {"CLI", AddressingMode::Implied,     1, 0x58}, // Clear Interrupt Disable
    /* 0x59 */ {"EOR", AddressingMode::AbsoluteY,   3, 0x59}, // EOR abs,Y
    /* 0x5A */ {"NOP", AddressingMode::Implied,     1, 0x5A}, // (undoc) NOP (1-byte)
    /* 0x5B */ {"SRE", AddressingMode::AbsoluteY,   3, 0x5B}, // (undoc) LSR + EOR abs,Y
    /* 0x5C */ {"NOP", AddressingMode::AbsoluteX,   3, 0x5C}, // (undoc) NOP abs,X
    /* 0x5D */ {"EOR", AddressingMode::AbsoluteX,   3, 0x5D}, // EOR abs,X
    /* 0x5E */ {"LSR", AddressingMode::AbsoluteX,   3, 0x5E}, // LSR abs,X
    /* 0x5F */ {"SRE", AddressingMode::AbsoluteX,   3, 0x5F}, // (undoc) LSR + EOR abs,X

        /* 0x60 */ {"RTS", AddressingMode::Implied,     1, 0x60}, // Return from Subroutine
    /* 0x61 */ {"ADC", AddressingMode::IndirectX,   2, 0x61}, // ADC (zp,X)
    /* 0x62 */ {"KIL", AddressingMode::Implied,     1, 0x62}, // (undoc) JAM/KIL - locks CPU
    /* 0x63 */ {"RRA", AddressingMode::IndirectX,   2, 0x63}, // (undoc) ROR + ADC (zp,X)
    /* 0x64 */ {"NOP", AddressingMode::ZeroPage,    2, 0x64}, // (undoc) NOP zp
    /* 0x65 */ {"ADC", AddressingMode::ZeroPage,    2, 0x65}, // ADC zp
    /* 0x66 */ {"ROR", AddressingMode::ZeroPage,    2, 0x66}, // ROR zp
    /* 0x67 */ {"RRA", AddressingMode::ZeroPage,    2, 0x67}, // (undoc) ROR + ADC zp
    /* 0x68 */ {"PLA", AddressingMode::Implied,     1, 0x68}, // Pull Accumulator
    /* 0x69 */ {"ADC", AddressingMode::Immediate,   2, 0x69}, // ADC #$nn
    /* 0x6A */ {"ROR", AddressingMode::Accumulator, 1, 0x6A}, // ROR A
    /* 0x6B */ {"ARR", AddressingMode::Immediate,   2, 0x6B}, // (undoc) AND + ROR, special flags
    /* 0x6C */ {"JMP", AddressingMode::Indirect,    3, 0x6C}, // JMP (addr)
    /* 0x6D */ {"ADC", AddressingMode::Absolute,    3, 0x6D}, // ADC abs
    /* 0x6E */ {"ROR", AddressingMode::Absolute,    3, 0x6E}, // ROR abs
    /* 0x6F */ {"RRA", AddressingMode::Absolute,    3, 0x6F}, // (undoc) ROR + ADC abs

    /* 0x70 */ {"BVS", AddressingMode::Relative,    2, 0x70}, // Branch if Overflow Set
    /* 0x71 */ {"ADC", AddressingMode::IndirectY,   2, 0x71}, // ADC (zp),Y
    /* 0x72 */ {"KIL", AddressingMode::Implied,     1, 0x72}, // (undoc) JAM/KIL - locks CPU
    /* 0x73 */ {"RRA", AddressingMode::IndirectY,   2, 0x73}, // (undoc) ROR + ADC (zp),Y
    /* 0x74 */ {"NOP", AddressingMode::ZeroPageX,   2, 0x74}, // (undoc) NOP zp,X
    /* 0x75 */ {"ADC", AddressingMode::ZeroPageX,   2, 0x75}, // ADC zp,X
    /* 0x76 */ {"ROR", AddressingMode::ZeroPageX,   2, 0x76}, // ROR zp,X
    /* 0x77 */ {"RRA", AddressingMode::ZeroPageX,   2, 0x77}, // (undoc) ROR + ADC zp,X
    /* 0x78 */ {"SEI", AddressingMode::Implied,     1, 0x78}, // Set Interrupt Disable
    /* 0x79 */ {"ADC", AddressingMode::AbsoluteY,   3, 0x79}, // ADC abs,Y
    /* 0x7A */ {"NOP", AddressingMode::Implied,     1, 0x7A}, // (undoc) NOP (1-byte)
    /* 0x7B */ {"RRA", AddressingMode::AbsoluteY,   3, 0x7B}, // (undoc) ROR + ADC abs,Y
    /* 0x7C */ {"NOP", AddressingMode::AbsoluteX,   3, 0x7C}, // (undoc) NOP abs,X
    /* 0x7D */ {"ADC", AddressingMode::AbsoluteX,   3, 0x7D}, // ADC abs,X
    /* 0x7E */ {"ROR", AddressingMode::AbsoluteX,   3, 0x7E}, // ROR abs,X
    /* 0x7F */ {"RRA", AddressingMode::AbsoluteX,   3, 0x7F}, // (undoc) ROR + ADC abs,X

    /* 0x80 */ {"NOP", AddressingMode::Immediate,   2, 0x80}, // (undoc) NOP #$nn
    /* 0x81 */ {"STA", AddressingMode::IndirectX,   2, 0x81}, // STA (zp,X)
    /* 0x82 */ {"NOP", AddressingMode::Immediate,   2, 0x82}, // (undoc) NOP #$nn
    /* 0x83 */ {"SAX", AddressingMode::IndirectX,   2, 0x83}, // (undoc) Store A & X at (zp,X)
    /* 0x84 */ {"STY", AddressingMode::ZeroPage,    2, 0x84}, // STY zp
    /* 0x85 */ {"STA", AddressingMode::ZeroPage,    2, 0x85}, // STA zp
    /* 0x86 */ {"STX", AddressingMode::ZeroPage,    2, 0x86}, // STX zp
    /* 0x87 */ {"SAX", AddressingMode::ZeroPage,    2, 0x87}, // (undoc) Store A & X at zp
    /* 0x88 */ {"DEY", AddressingMode::Implied,     1, 0x88}, // Decrement Y
    /* 0x89 */ {"NOP", AddressingMode::Immediate,   2, 0x89}, // (undoc) NOP #$nn
    /* 0x8A */ {"TXA", AddressingMode::Implied,     1, 0x8A}, // Transfer X to A
    /* 0x8B */ {"XAA", AddressingMode::Immediate,   2, 0x8B}, // (undoc) Highly unstable AND
    /* 0x8C */ {"STY", AddressingMode::Absolute,    3, 0x8C}, // STY abs
    /* 0x8D */ {"STA", AddressingMode::Absolute,    3, 0x8D}, // STA abs
    /* 0x8E */ {"STX", AddressingMode::Absolute,    3, 0x8E}, // STX abs
    /* 0x8F */ {"SAX", AddressingMode::Absolute,    3, 0x8F}, // (undoc) Store A & X at abs

    /* 0x90 */ {"BCC", AddressingMode::Relative,    2, 0x90}, // Branch if Carry Clear
    /* 0x91 */ {"STA", AddressingMode::IndirectY,   2, 0x91}, // STA (zp),Y
    /* 0x92 */ {"KIL", AddressingMode::Implied,     1, 0x92}, // (undoc) JAM/KIL - locks CPU
    /* 0x93 */ {"AHX", AddressingMode::IndirectY,   2, 0x93}, // (undoc) Store A & X & (high byte+1)
    /* 0x94 */ {"STY", AddressingMode::ZeroPageX,   2, 0x94}, // STY zp,X
    /* 0x95 */ {"STA", AddressingMode::ZeroPageX,   2, 0x95}, // STA zp,X
    /* 0x96 */ {"STX", AddressingMode::ZeroPageY,   2, 0x96}, // STX zp,Y
    /* 0x97 */ {"SAX", AddressingMode::ZeroPageY,   2, 0x97}, // (undoc) Store A & X at zp,Y
    /* 0x98 */ {"TYA", AddressingMode::Implied,     1, 0x98}, // Transfer Y to A
    /* 0x99 */ {"STA", AddressingMode::AbsoluteY,   3, 0x99}, // STA abs,Y
    /* 0x9A */ {"TXS", AddressingMode::Implied,     1, 0x9A}, // Transfer X to Stack Ptr
    /* 0x9B */ {"TAS", AddressingMode::AbsoluteY,   3, 0x9B}, // (undoc) Transfer A & X to SP, store
    /* 0x9C */ {"SHY", AddressingMode::AbsoluteX,   3, 0x9C}, // (undoc) Store Y & (high byte+1)
    /* 0x9D */ {"STA", AddressingMode::AbsoluteX,   3, 0x9D}, // STA abs,X
    /* 0x9E */ {"SHX", AddressingMode::AbsoluteY,   3, 0x9E}, // (undoc) Store X & (high byte+1)
    /* 0x9F */ {"AHX", AddressingMode::AbsoluteY,   3, 0x9F}, // (undoc) Store A & X & (high byte+1)

    /* 0xA0 */ {"LDY", AddressingMode::Immediate,   2, 0xA0}, // LDY #$nn
    /* 0xA1 */ {"LDA", AddressingMode::IndirectX,   2, 0xA1}, // LDA (zp,X)
    /* 0xA2 */ {"LDX", AddressingMode::Immediate,   2, 0xA2}, // LDX #$nn
    /* 0xA3 */ {"LAX", AddressingMode::IndirectX,   2, 0xA3}, // (undoc) Load A & X from (zp,X)
    /* 0xA4 */ {"LDY", AddressingMode::ZeroPage,    2, 0xA4}, // LDY zp
    /* 0xA5 */ {"LDA", AddressingMode::ZeroPage,    2, 0xA5}, // LDA zp
    /* 0xA6 */ {"LDX", AddressingMode::ZeroPage,    2, 0xA6}, // LDX zp
    /* 0xA7 */ {"LAX", AddressingMode::ZeroPage,    2, 0xA7}, // (undoc) Load A & X from zp
    /* 0xA8 */ {"TAY", AddressingMode::Implied,     1, 0xA8}, // Transfer A to Y
    /* 0xA9 */ {"LDA", AddressingMode::Immediate,   2, 0xA9}, // LDA #$nn
    /* 0xAA */ {"TAX", AddressingMode::Implied,     1, 0xAA}, // Transfer A to X
    /* 0xAB */ {"LAX", AddressingMode::Immediate,   2, 0xAB}, // (undoc) Load A & X (unstable)
    /* 0xAC */ {"LDY", AddressingMode::Absolute,    3, 0xAC}, // LDY abs
    /* 0xAD */ {"LDA", AddressingMode::Absolute,    3, 0xAD}, // LDA abs
    /* 0xAE */ {"LDX", AddressingMode::Absolute,    3, 0xAE}, // LDX abs
    /* 0xAF */ {"LAX", AddressingMode::Absolute,    3, 0xAF}, // (undoc) Load A & X from abs

    /* 0xB0 */ {"BCS", AddressingMode::Relative,    2, 0xB0}, // Branch if Carry Set
    /* 0xB1 */ {"LDA", AddressingMode::IndirectY,   2, 0xB1}, // LDA (zp),Y
    /* 0xB2 */ {"KIL", AddressingMode::Implied,     1, 0xB2}, // (undoc) JAM/KIL - locks CPU
    /* 0xB3 */ {"LAX", AddressingMode::IndirectY,   2, 0xB3}, // (undoc) Load A & X from (zp),Y
    /* 0xB4 */ {"LDY", AddressingMode::ZeroPageX,   2, 0xB4}, // LDY zp,X
    /* 0xB5 */ {"LDA", AddressingMode::ZeroPageX,   2, 0xB5}, // LDA zp,X
    /* 0xB6 */ {"LDX", AddressingMode::ZeroPageY,   2, 0xB6}, // LDX zp,Y
    /* 0xB7 */ {"LAX", AddressingMode::ZeroPageY,   2, 0xB7}, // (undoc) Load A & X from zp,Y
    /* 0xB8 */ {"CLV", AddressingMode::Implied,     1, 0xB8}, // Clear Overflow
    /* 0xB9 */ {"LDA", AddressingMode::AbsoluteY,   3, 0xB9}, // LDA abs,Y
    /* 0xBA */ {"TSX", AddressingMode::Implied,     1, 0xBA}, // Transfer SP to X
    /* 0xBB */ {"LAS", AddressingMode::AbsoluteY,   3, 0xBB}, // (undoc) Load A,X,SP = SP & M
    /* 0xBC */ {"LDY", AddressingMode::AbsoluteX,   3, 0xBC}, // LDY abs,X
    /* 0xBD */ {"LDA", AddressingMode::AbsoluteX,   3, 0xBD}, // LDA abs,X
    /* 0xBE */ {"LDX", AddressingMode::AbsoluteY,   3, 0xBE}, // LDX abs,Y
    /* 0xBF */ {"LAX", AddressingMode::AbsoluteY,   3, 0xBF}, // (undoc) Load A & X from abs,Y

    /* 0xC0 */ {"CPY", AddressingMode::Immediate,   2, 0xC0}, // CPY #$nn
    /* 0xC1 */ {"CMP", AddressingMode::IndirectX,   2, 0xC1}, // CMP (zp,X)
    /* 0xC2 */ {"NOP", AddressingMode::Immediate,   2, 0xC2}, // (undoc) NOP #$nn
    /* 0xC3 */ {"DCP", AddressingMode::IndirectX,   2, 0xC3}, // (undoc) DEC + CMP (zp,X)
    /* 0xC4 */ {"CPY", AddressingMode::ZeroPage,    2, 0xC4}, // CPY zp
    /* 0xC5 */ {"CMP", AddressingMode::ZeroPage,    2, 0xC5}, // CMP zp
    /* 0xC6 */ {"DEC", AddressingMode::ZeroPage,    2, 0xC6}, // DEC zp
    /* 0xC7 */ {"DCP", AddressingMode::ZeroPage,    2, 0xC7}, // (undoc) DEC + CMP zp
    /* 0xC8 */ {"INY", AddressingMode::Implied,     1, 0xC8}, // Increment Y
    /* 0xC9 */ {"CMP", AddressingMode::Immediate,   2, 0xC9}, // CMP #$nn
    /* 0xCA */ {"DEX", AddressingMode::Implied,     1, 0xCA}, // Decrement X
    /* 0xCB */ {"AXS", AddressingMode::Immediate,   2, 0xCB}, // (undoc) A & X -> X, compare with #$nn
    /* 0xCC */ {"CPY", AddressingMode::Absolute,    3, 0xCC}, // CPY abs
    /* 0xCD */ {"CMP", AddressingMode::Absolute,    3, 0xCD}, // CMP abs
    /* 0xCE */ {"DEC", AddressingMode::Absolute,    3, 0xCE}, // DEC abs
    /* 0xCF */ {"DCP", AddressingMode::Absolute,    3, 0xCF}, // (undoc) DEC + CMP abs

    /* 0xD0 */ {"BNE", AddressingMode::Relative,    2, 0xD0}, // Branch if Not Equal
    /* 0xD1 */ {"CMP", AddressingMode::IndirectY,   2, 0xD1}, // CMP (zp),Y
    /* 0xD2 */ {"KIL", AddressingMode::Implied,     1, 0xD2}, // (undoc) JAM/KIL - locks CPU
    /* 0xD3 */ {"DCP", AddressingMode::IndirectY,   2, 0xD3}, // (undoc) DEC + CMP (zp),Y
    /* 0xD4 */ {"NOP", AddressingMode::ZeroPageX,   2, 0xD4}, // (undoc) NOP zp,X
    /* 0xD5 */ {"CMP", AddressingMode::ZeroPageX,   2, 0xD5}, // CMP zp,X
    /* 0xD6 */ {"DEC", AddressingMode::ZeroPageX,   2, 0xD6}, // DEC zp,X
    /* 0xD7 */ {"DCP", AddressingMode::ZeroPageX,   2, 0xD7}, // (undoc) DEC + CMP zp,X
    /* 0xD8 */ {"CLD", AddressingMode::Implied,     1, 0xD8}, // Clear Decimal
    /* 0xD9 */ {"CMP", AddressingMode::AbsoluteY,   3, 0xD9}, // CMP abs,Y
    /* 0xDA */ {"NOP", AddressingMode::Implied,     1, 0xDA}, // (undoc) NOP (1-byte)
    /* 0xDB */ {"DCP", AddressingMode::AbsoluteY,   3, 0xDB}, // (undoc) DEC + CMP abs,Y
    /* 0xDC */ {"NOP", AddressingMode::AbsoluteX,   3, 0xDC}, // (undoc) NOP abs,X
    /* 0xDD */ {"CMP", AddressingMode::AbsoluteX,   3, 0xDD}, // CMP abs,X
    /* 0xDE */ {"DEC", AddressingMode::AbsoluteX,   3, 0xDE}, // DEC abs,X
    /* 0xDF */ {"DCP", AddressingMode::AbsoluteX,   3, 0xDF}, // (undoc) DEC + CMP abs,X

    /* 0xE0 */ {"CPX", AddressingMode::Immediate,   2, 0xE0}, // CPX #$nn
    /* 0xE1 */ {"SBC", AddressingMode::IndirectX,   2, 0xE1}, // SBC (zp,X)
    /* 0xE2 */ {"NOP", AddressingMode::Immediate,   2, 0xE2}, // (undoc) NOP #$nn
    /* 0xE3 */ {"ISC", AddressingMode::IndirectX,   2, 0xE3}, // (undoc) INC + SBC (zp,X)
    /* 0xE4 */ {"CPX", AddressingMode::ZeroPage,    2, 0xE4}, // CPX zp
    /* 0xE5 */ {"SBC", AddressingMode::ZeroPage,    2, 0xE5}, // SBC zp
    /* 0xE6 */ {"INC", AddressingMode::ZeroPage,    2, 0xE6}, // INC zp
    /* 0xE7 */ {"ISC", AddressingMode::ZeroPage,    2, 0xE7}, // (undoc) INC + SBC zp
    /* 0xE8 */ {"INX", AddressingMode::Implied,     1, 0xE8}, // Increment X
    /* 0xE9 */ {"SBC", AddressingMode::Immediate,   2, 0xE9}, // SBC #$nn
    /* 0xEA */ {"NOP", AddressingMode::Implied,     1, 0xEA}, // Official NOP
    /* 0xEB */ {"SBC", AddressingMode::Immediate,   2, 0xEB}, // (undoc) SBC #$nn (alias of E9)
    /* 0xEC */ {"CPX", AddressingMode::Absolute,    3, 0xEC}, // CPX abs
    /* 0xED */ {"SBC", AddressingMode::Absolute,    3, 0xED}, // SBC abs
    /* 0xEE */ {"INC", AddressingMode::Absolute,    3, 0xEE}, // INC abs
    /* 0xEF */ {"ISC", AddressingMode::Absolute,    3, 0xEF}, // (undoc) INC + SBC abs

    /* 0xF0 */ {"BEQ", AddressingMode::Relative,    2, 0xF0}, // Branch if Equal
    /* 0xF1 */ {"SBC", AddressingMode::IndirectY,   2, 0xF1}, // SBC (zp),Y
    /* 0xF2 */ {"KIL", AddressingMode::Implied,     1, 0xF2}, // (undoc) JAM/KIL - locks CPU
    /* 0xF3 */ {"ISC", AddressingMode::IndirectY,   2, 0xF3}, // (undoc) INC + SBC (zp),Y
    /* 0xF4 */ {"NOP", AddressingMode::ZeroPageX,   2, 0xF4}, // (undoc) NOP zp,X
    /* 0xF5 */ {"SBC", AddressingMode::ZeroPageX,   2, 0xF5}, // SBC zp,X
    /* 0xF6 */ {"INC", AddressingMode::ZeroPageX,   2, 0xF6}, // INC zp,X
    /* 0xF7 */ {"ISC", AddressingMode::ZeroPageX,   2, 0xF7}, // (undoc) INC + SBC zp,X
    /* 0xF8 */ {"SED", AddressingMode::Implied,     1, 0xF8}, // Set Decimal
    /* 0xF9 */ {"SBC", AddressingMode::AbsoluteY,   3, 0xF9}, // SBC abs,Y
    /* 0xFA */ {"NOP", AddressingMode::Implied,     1, 0xFA}, // (undoc) NOP (1-byte)
    /* 0xFB */ {"ISC", AddressingMode::AbsoluteY,   3, 0xFB}, // (undoc) INC + SBC abs,Y
    /* 0xFC */ {"NOP", AddressingMode::AbsoluteX,   3, 0xFC}, // (undoc) NOP abs,X
    /* 0xFD */ {"SBC", AddressingMode::AbsoluteX,   3, 0xFD}, // SBC abs,X
    /* 0xFE */ {"INC", AddressingMode::AbsoluteX,   3, 0xFE}, // INC abs,X
    /* 0xFF */ {"ISC", AddressingMode::AbsoluteX,   3, 0xFF}, // (undoc) INC + SBC abs,X
};
