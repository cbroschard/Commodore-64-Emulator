// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/FDC177x.h"
#include "Drive/D1571.h"

FDC177x::FDC177x() :
    parentPeripheral(nullptr),
    dataIndex(0),
    drq(false),
    intrq(false),
    cyclesUntilEvent(0)
{
    currentType = CommandType::None;
}

FDC177x::~FDC177x() = default;

void FDC177x::reset()
{
    registers.status   = 0x00;
    registers.command  = 0x00;
    registers.track    = 0x00;
    registers.sector   = 0x00;
    registers.data     = 0x00;

    currentType = CommandType::None;

    drq = false;
    intrq = false;

    cyclesUntilEvent = 0;
}

void FDC177x::tick()
{
    if (cyclesUntilEvent > 0)
    {
        --cyclesUntilEvent;
        if (cyclesUntilEvent == 0)
        {
            switch (currentType)
            {
                case CommandType::TypeI:
                    // Head move complete
                    setBusy(false);
                    setINTRQ(true);
                    break;
                case CommandType::TypeII:
                case CommandType::TypeIII:
                    setBusy(false);
                    setDRQ(false);
                    setINTRQ(true);
                    break;
                default:
                    break;
            }
        }
    }
}

uint8_t FDC177x::readRegister(uint16_t address)
{
    switch(address & 0x03)
    {
        case 0: // Status reg
            return registers.status;
        case 1: // Track reg
            return registers.track;
        case 2: // Sector reg
            return registers.sector;
        case 3: // Data reg
            return registers.data;
        default:
            return 0xFF; // open bus should not hit
    }
}

void FDC177x::writeRegister(uint16_t address, uint8_t value)
{
    switch(address & 0x03)
    {
        case 0: // Command reg
        {
            registers.command = value;
            currentType = decodeCommandType(value);
            startCommand(value);
            break;
        }
        case 1: // Track reg
            registers.track = value;
            break;
        case 2: // Sector reg
            registers.sector = value;
            break;
        case 3: // Data reg
            registers.data = value;
            break;
        default:
            // Ignore this
            break;
    }
}

FDC177x::CommandType FDC177x::decodeCommandType(uint8_t cmd) const
{
    uint8_t hi = cmd & 0xF0;
    switch (hi)
    {
        // 0x00–0x70 : Type I
        case 0x00:
        case 0x10:
        case 0x20:
        case 0x30:
        case 0x40:
        case 0x50:
        case 0x60:
        case 0x70:
            return CommandType::TypeI;

        // 0x80–0xB0 : Type II (Read/Write Sector and variants)
        case 0x80:
        case 0x90:
        case 0xA0:
        case 0xB0:
            return CommandType::TypeII;

        // 0xC0, 0xE0, 0xF0 : Type III
        case 0xC0:
        case 0xE0:
        case 0xF0:
            return CommandType::TypeIII;

        // 0xD0 : Type IV (Force Interrupt)
        case 0xD0:
            return CommandType::TypeIV;

        default:
            return CommandType::None; // illegal / undefined command
    }
}

void FDC177x::startCommand(uint8_t cmd)
{
    // Clear some status bits that are command-specific
    registers.status &= static_cast<uint8_t>(~(lostDataOrNotT0 | crcError | recordNotFound | spinUpOrDelData | writeProtect));

    setBusy(true);
    setDRQ(false);
    setINTRQ(false);

    cyclesUntilEvent = 0;

    CommandGroup group = static_cast<CommandGroup>(cmd & 0xF0);

    switch(currentType)
    {
        case CommandType::TypeI:
        {
            switch(group)
            {
                case CommandGroup::Restore:
                    registers.track = 0;
                    break;
                case CommandGroup::Seek:
                    registers.track = registers.data;
                    break;
                case CommandGroup::Step:
                    break;
                case CommandGroup::StepIn:
                    ++registers.track;
                    break;
                case CommandGroup::StepOut:
                    --registers.track;
                    break;
                default: break;
            }
            cyclesUntilEvent = 500;
            break;
        }
        case CommandType::TypeII:
        case CommandType::TypeIII:
            cyclesUntilEvent = 2000;
            break;
        case CommandType::TypeIV:
            setBusy(false);
            setDRQ(false);
            setINTRQ(true);
            cyclesUntilEvent = 0;
            break;
        default:
        {
            // Illegal/unknown command: clear busy and raise INTRQ
            setBusy(false);
            setDRQ(false);
            setINTRQ(true);
            cyclesUntilEvent = 0;
            break;
        }
    }

    if ((currentType == CommandType::TypeI || currentType == CommandType::TypeII || currentType == CommandType::TypeIII) && cyclesUntilEvent == 0)
    {
        cyclesUntilEvent = 1; // complete on next tick()
    }
}

void FDC177x::setDRQ(bool on)
{
    drq = on;
    if (on) registers.status |= dataRequest;
    else    registers.status &= static_cast<uint8_t>(~dataRequest);
}

void FDC177x::setBusy(bool on)
{
    if (on) registers.status |= busy;
    else    registers.status &= static_cast<uint8_t>(~busy);
}

void FDC177x::setINTRQ(bool on)
{
    intrq = on;
}
