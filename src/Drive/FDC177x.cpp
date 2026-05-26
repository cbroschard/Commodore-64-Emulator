// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <vector>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <iomanip>
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
    cyclesUntilEvent(0),
    addressIndex(0),
    readAddressInProgress(false),
    readAddressByteDelay(false),
    addressScanSector(1)
{
    currentType = CommandType::None;
    std::memset(addressBuffer, 0x00, sizeof(addressBuffer));
}

FDC177x::~FDC177x() = default;

static uint16_t fdcCrcUpdate(uint16_t crc, uint8_t value)
{
    crc ^= static_cast<uint16_t>(value) << 8;

    for (int i = 0; i < 8; ++i)
    {
        if (crc & 0x8000)
            crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
        else
            crc = static_cast<uint16_t>(crc << 1);
    }

    return crc;
}

static uint16_t computeAddressFieldCRC(uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
    uint16_t crc = 0xFFFF;

    crc = fdcCrcUpdate(crc, 0xA1);
    crc = fdcCrcUpdate(crc, 0xA1);
    crc = fdcCrcUpdate(crc, 0xA1);
    crc = fdcCrcUpdate(crc, 0xFE);

    crc = fdcCrcUpdate(crc, c);
    crc = fdcCrcUpdate(crc, h);
    crc = fdcCrcUpdate(crc, r);
    crc = fdcCrcUpdate(crc, n);

    return crc;
}

#ifdef Debug
static const char* fdcCommandName(uint8_t cmd)
{
    switch (cmd & 0xF0)
    {
        case 0x00: return "RESTORE";
        case 0x10: return "SEEK";
        case 0x20: return "STEP";
        case 0x30: return "STEP";
        case 0x40: return "STEP IN";
        case 0x50: return "STEP IN";
        case 0x60: return "STEP OUT";
        case 0x70: return "STEP OUT";
        case 0x80: return "READ SECTOR";
        case 0x90: return "READ SECTOR MULTI";
        case 0xA0: return "WRITE SECTOR";
        case 0xB0: return "WRITE SECTOR MULTI";
        case 0xC0: return "READ ADDRESS";
        case 0xD0: return "FORCE INTERRUPT";
        case 0xE0: return "READ TRACK";
        case 0xF0: return "WRITE TRACK";
        default:   return "UNKNOWN";
    }
}
#endif

void FDC177x::saveState(StateWriter& wrtr) const
{
    // Version
    wrtr.writeU32(1);

    // Registers
    wrtr.writeU8(registers.status);
    wrtr.writeU8(registers.command);
    wrtr.writeU8(registers.track);
    wrtr.writeU8(registers.sector);
    wrtr.writeU8(registers.data);

    // Core runtime state
    wrtr.writeU8(static_cast<uint8_t>(currentType));
    wrtr.writeU16(currentSectorSize);
    wrtr.writeU16(dataIndex);

    wrtr.writeBool(readSectorInProgress);
    wrtr.writeBool(writeSectorInProgress);

    wrtr.writeBool(drq);
    wrtr.writeBool(intrq);

    wrtr.writeU32(static_cast<uint32_t>(cyclesUntilEvent));

    const uint32_t bufLen = std::min<uint32_t>(currentSectorSize, static_cast<uint16_t>(MaxSectorSize));
    wrtr.writeU32(bufLen);

    std::vector<uint8_t> tmp;
    tmp.assign(sectorBuffer, sectorBuffer + bufLen);
    wrtr.writeVectorU8(tmp);
}

bool FDC177x::loadState(StateReader& rdr)
{
    // Version
    uint32_t ver = 0;
    if (!rdr.readU32(ver)) return false;
    if (ver != 1) return false;

    // Registers
    if (!rdr.readU8(registers.status)) return false;
    if (!rdr.readU8(registers.command)) return false;
    if (!rdr.readU8(registers.track)) return false;
    if (!rdr.readU8(registers.sector)) return false;
    if (!rdr.readU8(registers.data)) return false;

    // Core runtime state
    uint8_t typeU8 = 0;
    if (!rdr.readU8(typeU8)) return false;
    currentType = static_cast<CommandType>(typeU8);

    if (!rdr.readU16(currentSectorSize)) return false;
    if (currentSectorSize == 0 || currentSectorSize > MaxSectorSize) return false;

    if (!rdr.readU16(dataIndex)) return false;

    if (!rdr.readBool(readSectorInProgress)) return false;
    if (!rdr.readBool(writeSectorInProgress)) return false;

    if (!rdr.readBool(drq)) return false;
    if (!rdr.readBool(intrq)) return false;

    uint32_t ce = 0;
    if (!rdr.readU32(ce)) return false;
    cyclesUntilEvent = static_cast<int32_t>(ce);

    // Sector buffer snapshot
    uint32_t bufLen = 0;
    if (!rdr.readU32(bufLen)) return false;
    if (bufLen > MaxSectorSize) return false;

    std::vector<uint8_t> tmp;
    if (!rdr.readVectorU8(tmp)) return false;
    if (tmp.size() != bufLen) return false;

    // Clear entire buffer for determinism, then copy what was saved
    std::memset(sectorBuffer, 0x00, MaxSectorSize);
    if (bufLen > 0)
        std::memcpy(sectorBuffer, tmp.data(), bufLen);

    // Post-restore fixups
    if (drq) registers.status |= dataRequest;
    else     registers.status &= static_cast<uint8_t>(~dataRequest);

    // Clamp dataIndex so a bad save doesn't explode later
    if (dataIndex > currentSectorSize)
        dataIndex = currentSectorSize;

    return true;
}

void FDC177x::reset()
{
    registers.status        = 0x00;
    registers.command       = 0x00;
    registers.track         = 0x00;
    registers.sector        = 0x00;
    registers.data          = 0x00;

    dataIndex = 0;
    drq = false;
    intrq = false;
    readSectorInProgress    = false;
    writeSectorInProgress   = false;

    cyclesUntilEvent        = 0;

    addressIndex            = 0;
    readAddressInProgress   = false;
    readAddressByteDelay    = false;
    addressScanSector       = 1;

    currentType = CommandType::None;

    std::memset(addressBuffer, 0x00, sizeof(addressBuffer));
}

void FDC177x::tick(uint32_t cycles)
{
    while (cycles-- > 0)
    {
        if (cyclesUntilEvent <= 0)
            continue;

        --cyclesUntilEvent;

        if (cyclesUntilEvent != 0)
            continue;

        switch (currentType)
        {
            case CommandType::TypeI:
            {
                updateTypeIStatusBits();
                finishCommand(true);
                break;
            }

            case CommandType::TypeII:
            {
                if (readSectorInProgress)
                {
                    if (dataIndex < currentSectorSize)
                    {
                        // Next read byte is now available.
                        registers.data = sectorBuffer[dataIndex];
                        setDRQ(true);
                    }
                    else
                    {
                        // Defensive completion path.
                        finishCommand(true);
                    }
                }
                else if (writeSectorInProgress)
                {
                    if (dataIndex < currentSectorSize)
                    {
                        // FDC is ready for the next byte from the CPU.
                        setDRQ(true);
                    }
                    else
                    {
                        // Defensive completion path.
                        finishCommand(true);
                    }
                }
                else
                {
                    finishCommand(true);
                }

                break;
            }

            case CommandType::TypeIII:
            {
                if (readAddressInProgress)
                {
                    if (readAddressByteDelay)
                        readAddressByteDelay = false;

                    if (addressIndex < sizeof(addressBuffer))
                    {
                        registers.data = addressBuffer[addressIndex];
                        setBusy(true);
                        setDRQ(true);
                    }
                    else
                    {
                        finishCommand(true);
                    }
                }
                else
                {
                    finishCommand(true);
                }

                break;
            }

            default:
                break;
        }
    }
}

uint8_t FDC177x::readRegister(uint16_t address)
{
    switch (address & 0x03)
    {
        case 0: // Status reg
        {
            uint8_t status = registers.status;
            setINTRQ(false);
            return status;
        }

        case 1: // Track reg
        {
            return registers.track;
        }

        case 2: // Sector reg
        {
            return registers.sector;
        }

        case 3: // Data reg
        {
            // Type III READ ADDRESS transfer:
            // returns 6 bytes: C,H,R,N,CRC1,CRC2.
            if (currentType == CommandType::TypeIII && readAddressInProgress)
            {
                if (!drq)
                    return registers.data;

                uint8_t value = 0xFF;
                const uint8_t outIndex = addressIndex;

                if (addressIndex < sizeof(addressBuffer))
                    value = addressBuffer[addressIndex++];

                if (outIndex == 2)
                    registers.sector = value;

                registers.data = value;
                setDRQ(false);

                if (addressIndex >= sizeof(addressBuffer))
                {
                    finishCommand(true);
                }
                else
                {
                    readAddressByteDelay = true;
                    cyclesUntilEvent = 64; // small delay before next address byte
                }

                return value;
            }

            // Type II READ SECTOR transfer.
            const uint8_t typeIIGroup = registers.command & 0xE0;

            if (currentType == CommandType::TypeII &&
                typeIIGroup == 0x80 &&
                readSectorInProgress)
            {
                // If DRQ is not active, the FDC data register still returns
                // the last latched byte.
                if (!drq)
                    return registers.data;

                uint8_t value = registers.data;

                if (dataIndex < currentSectorSize)
                {
                    value = sectorBuffer[dataIndex];
                    ++dataIndex;
                }

                registers.data = value;
                setDRQ(false);

                if (dataIndex >= currentSectorSize)
                {
                    finishCommand(true);
                }
                else
                {
                    // Do NOT immediately assert DRQ again.
                    // Schedule the next byte to become available shortly.
                    cyclesUntilEvent = FDC_BYTE_DELAY_CYCLES;
                }

                return value;
            }

            return registers.data;
        }

        default:
        {
            return 0xFF;
        }
    }
}

void FDC177x::writeRegister(uint16_t address, uint8_t value)
{
    switch(address & 0x03)
    {
        case 0: // Command reg
        {
            const CommandType newType = decodeCommandType(value);

            if ((registers.status & busy) && newType != CommandType::TypeIV)
            {
                return;
            }

            registers.command = value;
            currentType = newType;
            startCommand(value);
            break;
        }

        case 1: // Track reg
        {
            registers.track = value;
            break;
        }

        case 2: // Sector reg
        {
            registers.sector = value;
            break;
        }

        case 3: // Data reg
        {
            const uint8_t typeIIGroup = registers.command & 0xE0;

            if (currentType == CommandType::TypeII &&
                typeIIGroup == 0xA0 &&
                writeSectorInProgress)
            {
                if (!drq)
                {
                    // CPU wrote when the FDC was not asking for data.
                    // For now, just latch the value. Later you can set Lost Data
                    // here if you want stricter timing.
                    registers.data = value;
                    return;
                }

                if (dataIndex < currentSectorSize)
                {
                    sectorBuffer[dataIndex++] = value;
                    registers.data = value;
                    setDRQ(false);
                }

                if (dataIndex >= currentSectorSize)
                {
                    bool ok = false;

                    if (!host)
                    {
                        ok = false;
                    }
                    else if (host->fdcIsWriteProtected())
                    {
                        registers.status |= writeProtect;
                        ok = false;
                    }
                    else
                    {
                        ok = host->fdcWriteSector(registers.track,
                                                  registers.sector,
                                                  sectorBuffer,
                                                  currentSectorSize);
                    }

                    if (!ok)
                    {
                        if (!(registers.status & writeProtect))
                            registers.status |= recordNotFound;
                    }

                    finishCommand(true);
                    return;
                }

                // More bytes still needed.
                // Do NOT immediately reassert DRQ.
                // Schedule the next write request.
                cyclesUntilEvent = FDC_BYTE_DELAY_CYCLES;
                return;
            }

            // Normal idle/data register write.
            registers.data = value;
            break;
        }

        default:
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
    #ifdef Debug
    std::cout << "[FDC CMD] $" << std::hex << std::setw(2) << std::setfill('0')
              << int(cmd)
              << " " << fdcCommandName(cmd)
              << " TRK=" << std::dec << int(registers.track)
              << " SEC=" << int(registers.sector)
              << " DATA=$" << std::hex << int(registers.data)
              << " STATUS=$" << int(registers.status)
              << std::dec
              << "\n";
    #endif

    readSectorInProgress    = false;
    writeSectorInProgress   = false;
    readAddressInProgress   = false;

    setDRQ(false);
    setINTRQ(false);

    clearCommandStatusBitsForType(currentType);

    setBusy(true);
    cyclesUntilEvent = 0;

    const uint8_t rawGroup    = cmd & 0xF0;
    const uint8_t typeIIGroup = cmd & 0xE0;

    switch(currentType)
    {
        case CommandType::TypeI:
        {
            CommandGroup group = static_cast<CommandGroup>(rawGroup);

            switch(group)
            {
                case CommandGroup::Restore:
                    registers.track = 0;
                    addressScanSector = 1;
                    break;

                case CommandGroup::Seek:
                    registers.track = registers.data;
                    addressScanSector = 1;
                    break;

                case CommandGroup::Step:
                    addressScanSector = 1;
                    break;

                case CommandGroup::StepIn:
                    ++registers.track;
                    addressScanSector = 1;
                    break;

                case CommandGroup::StepOut:
                    if (registers.track > 0)
                        --registers.track;
                    addressScanSector = 1;
                    break;

                default:
                    break;
            }

            /*if (host)
                host->fdcSetCurrentTrackId(registers.track);*/

            updateTypeIStatusBits();
            cyclesUntilEvent = 500;
            break;
        }

        case CommandType::TypeII:
        {
            switch(typeIIGroup)
            {
                case 0x80: // READ SECTOR, covers $80/$90
                {
                    bool ok = false;

                    if (host)
                    {
                        ok = host->fdcReadSector(registers.track,
                                                 registers.sector,
                                                 sectorBuffer,
                                                 currentSectorSize);
                    }

                    if (ok)
                    {
                        dataIndex = 0;
                        readSectorInProgress = true;
                        cyclesUntilEvent = 2000;
                    }
                    else
                    {
                        registers.status |= recordNotFound;
                        setBusy(false);
                        setDRQ(false);
                        setINTRQ(true);
                        cyclesUntilEvent = 0;
                    }

                    break;
                }
                case 0xA0: // WRITE SECTOR, covers $A0/$B0
                {
                    dataIndex = 0;
                    setDRQ(false);
                    setINTRQ(false);
                    cyclesUntilEvent = 2000;
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
        {
            const uint8_t typeIIIGroup = cmd & 0xF0;

            if (typeIIIGroup == 0xC0) // READ ADDRESS, includes $C8
            {
                addressIndex = 0;
                readAddressInProgress = true;

                // WD177x READ ADDRESS returns 6 bytes:
                // C, H, R, N, CRC1, CRC2.
                //
                // Important: R should look like the next sector ID passing under the head.
                // Do not return the same sector forever.
                const uint8_t side = host ? (host->fdcGetCurrentSide() & 0x01) : 0;
                const uint8_t trackId = registers.track;
                const uint8_t currentId = addressScanSector;

                // For the 1581 ROM path, report rotating IDs as 0..9.
                // The ROM then requests the following FDC sector number.
                ++addressScanSector;

                if (addressScanSector > 10)
                    addressScanSector = 1;

                addressBuffer[0] = trackId;
                addressBuffer[1] = side;
                addressBuffer[2] = currentId;
                addressBuffer[3] = 0x02;

                // WD177x READ ADDRESS returns the ID field CRC.
                // CRC is over A1 A1 A1 FE C H R N, standard CCITT poly $1021.
                const uint16_t crc = computeAddressFieldCRC(
                    addressBuffer[0],
                    addressBuffer[1],
                    addressBuffer[2],
                    addressBuffer[3]);

                addressBuffer[4] = static_cast<uint8_t>((crc >> 8) & 0xFF);
                addressBuffer[5] = static_cast<uint8_t>(crc & 0xFF);

                #ifdef Debug
                std::cout << "[FDC READ ADDRESS RESULT] "
                          << "C=" << int(addressBuffer[0])
                          << " H=" << int(addressBuffer[1])
                          << " R=" << int(addressBuffer[2])
                          << " N=" << int(addressBuffer[3])
                          << " currentSide=" << int(host ? (host->fdcGetCurrentSide() & 1) : 0)
                          << " sectorReg=" << int(registers.sector)
                          << "\n";
                #endif

                cyclesUntilEvent = 2000;
            }
            else
            {
                cyclesUntilEvent = 2000;
            }

            break;
        }

        case CommandType::TypeIV:
        {
            setBusy(false);
            setDRQ(false);

            readSectorInProgress = false;
            writeSectorInProgress = false;
            readAddressInProgress = false;
            readAddressByteDelay = false;

            // $D0 with no condition bits is commonly used as a terminate/clear.
            // Only assert INTRQ if one of I0-I3 is requested.
            if (cmd & 0x0F)
                setINTRQ(true);
            else
                setINTRQ(false);

            cyclesUntilEvent = 0;
            break;
        }

        default:
        {
            setBusy(false);
            setDRQ(false);
            setINTRQ(true);
            cyclesUntilEvent = 0;
            break;
        }
    }

    if ((currentType == CommandType::TypeI ||
         currentType == CommandType::TypeII ||
         currentType == CommandType::TypeIII) &&
        cyclesUntilEvent == 0)
    {
        cyclesUntilEvent = 1;
    }
}

void FDC177x::finishCommand(bool interrupt)
{
    readSectorInProgress = false;
    writeSectorInProgress = false;
    readAddressInProgress = false;
    readAddressByteDelay = false;

    dataIndex = 0;
    addressIndex = 0;
    cyclesUntilEvent = 0;

    setDRQ(false);
    setBusy(false);

    if (interrupt)
        setINTRQ(true);

    currentType = CommandType::None;
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

void FDC177x::updateTypeIStatusBits()
{
    // Type I status bit 2 = Track 0 indicator.
    if (registers.track == 0)
        registers.status |= lostDataOrNotT0;
    else
        registers.status &= static_cast<uint8_t>(~lostDataOrNotT0);

    registers.status &= static_cast<uint8_t>(~motorOn);
}

void FDC177x::clearCommandStatusBitsForType(CommandType type)
{
    if (type == CommandType::TypeI)
    {
        // Keep TRK0 semantics for Type I.
        registers.status &= static_cast<uint8_t>(~(crcError | recordNotFound | spinUpOrDelData | writeProtect | motorOn));
        updateTypeIStatusBits();
    }
    else
    {
        // For Type II/III, bit 2 is Lost Data, not TRK0.
        // Also clear bit 7 so the ROM does not see Not Ready.
        registers.status &= static_cast<uint8_t>(~(lostDataOrNotT0 | crcError | recordNotFound | spinUpOrDelData | writeProtect | motorOn));
    }
}
