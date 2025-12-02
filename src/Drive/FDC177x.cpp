// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/FDC177x.h"

FDC177x::FDC177x() :
    host(nullptr),
    parentPeripheral(nullptr),
    currentSectorSize(256),
    dataIndex(0),
    readSectorInProgress(false),
    writeSectorInProgress(false),
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

    dataIndex = 0;
    drq = false;
    intrq = false;
    readSectorInProgress  = false;
    writeSectorInProgress = false;

    cyclesUntilEvent = 0;

    currentSectorSize = 256;
}

void FDC177x::tick(uint32_t cycles)
{
    while(cycles-- > 0)
    {
        if (cyclesUntilEvent > 0)
        {
            --cyclesUntilEvent;
            if (cyclesUntilEvent == 0)
            {
                switch (currentType)
                {
                    case CommandType::TypeI:
                    {
                        // Head move complete
                        setBusy(false);
                        setINTRQ(true);

                        // Track 0 / Not Track 0 flag (lostDataOrNotT0 bit)
                        if (registers.track == 0)
                            registers.status |= lostDataOrNotT0; // TRK0 = 1
                        else
                            registers.status &= static_cast<uint8_t>(~lostDataOrNotT0);  // TRK0 = 0

                        break;
                    }
                    case CommandType::TypeII:
                    {
                        if (readSectorInProgress)
                        {
                            // READ SECTOR: first byte is now ready
                            if (dataIndex < sizeof(sectorBuffer))
                            {
                                // Don't advance dataIndex yet; wait until CPU reads
                                registers.data = sectorBuffer[dataIndex];
                            }
                            setDRQ(true);   // tell CPU "you can read a byte now"
                        }
                        else if (writeSectorInProgress)
                        {
                            // WRITE SECTOR: ready for first data byte
                            setDRQ(true);   // tell CPU "write a byte now"
                        }
                        else
                        {
                            setBusy(false);
                            setDRQ(false);
                            setINTRQ(true);
                        }
                        break;
                    }
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
}

uint8_t FDC177x::readRegister(uint16_t address)
{
    switch(address & 0x03)
    {
        case 0: // Status reg
        {
            uint8_t status = registers.status;
            setINTRQ(false);
            return status;
        }
        case 1: // Track reg
            return registers.track;
        case 2: // Sector reg
            return registers.sector;
        case 3: // Data reg
        {
            CommandGroup group = static_cast<CommandGroup>(registers.command & 0xF0);

            if (currentType == CommandType::TypeII && group == CommandGroup::ReadSector && readSectorInProgress)
            {
                if (!drq)
                {
                    return registers.data;
                }

                uint8_t value = 0xFF;

                if (dataIndex < currentSectorSize)
                {
                    value = sectorBuffer[dataIndex++];
                }

                registers.data = value;
                setDRQ(false);

                if (dataIndex >= currentSectorSize)
                {
                    // Last byte of the sector
                    readSectorInProgress = false;
                    setBusy(false);
                    setINTRQ(true);
                }
                else
                {
                    setDRQ(true);
                }

                return value;
            }
            // Default: not in a READ SECTOR transfer, just return the latched data
            return registers.data;
        }
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
        {
            CommandGroup group = static_cast<CommandGroup>(registers.command & 0xF0);

            if (currentType == CommandType::TypeII && group == CommandGroup::WriteSector && writeSectorInProgress)
            {
                if (dataIndex < currentSectorSize)
                {
                    sectorBuffer[dataIndex++] = value;
                    registers.data = value;
                    setDRQ(false);  // we've consumed this byte
                }

                if (dataIndex >= currentSectorSize)
                {
                    // Complete sector, write to disk
                    bool ok = false;

                    if (!host)
                    {
                        // No host at all – treat as failure
                        ok = false;
                    }
                    else if (host->fdcIsWriteProtected())
                    {
                        // Disk is write-protected: do NOT call fdcWriteSector.
                        registers.status |= writeProtect;
                        ok = false;
                    }
                    else
                    {
                        // Disk is not write-protected; try to write.
                        ok = host->fdcWriteSector(registers.track,
                                                  registers.sector,
                                                  sectorBuffer,
                                                  sizeof(sectorBuffer));
                    }

                    writeSectorInProgress = false;
                    dataIndex             = 0;  // reset for next time

                    if (ok)
                    {
                        setBusy(false);
                        setDRQ(false);
                        setINTRQ(true);
                    }
                    else
                    {
                        // If it wasn't write-protect, treat it as "record not found"
                        if (!(registers.status & writeProtect))
                        {
                            registers.status |= recordNotFound;
                        }

                        setBusy(false);
                        setDRQ(false);
                        setINTRQ(true);
                    }
                    return;
                }
                else
                {
                    // Not done yet; request the next byte
                    setDRQ(true);
                }

                return;
            }

            registers.data = value; // Default behavior
            break;
        }
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
    readSectorInProgress  = false;
    writeSectorInProgress = false;

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
                    if (registers.track > 0)
                    --registers.track;
                    break;
                default: break;
            }
            cyclesUntilEvent = 500;
            break;
        }
        case CommandType::TypeII:
        {
            switch(group)
            {
                case CommandGroup::ReadSector:
                {
                    bool ok = false;
                    if (host)
                    {
                        ok = host->fdcReadSector(registers.track, registers.sector, sectorBuffer, currentSectorSize);
                    }
                    if (ok)
                    {
                        dataIndex            = 0;     // position in sectorBuffer
                        readSectorInProgress = true;
                        cyclesUntilEvent     = 2000;  // fake "read time"
                    }
                    else
                    {
                        // Sector not found or no disk
                        registers.status |= recordNotFound;
                        setBusy(false);
                        setINTRQ(true);
                        cyclesUntilEvent = 0;
                    }
                    break;
                }
                case CommandGroup::WriteSector:
                {
                    dataIndex = 0;
                    setDRQ(false);
                    setINTRQ(false);
                    cyclesUntilEvent      = 2000;
                    writeSectorInProgress = true;
                    break;
                }
                default:
                    cyclesUntilEvent = 2000;
                    break;
            }
            break;
        }
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
