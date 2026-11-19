// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D4040Memory.h"

D4040Memory::D4040Memory() :
    parentPeripheral(nullptr)
{
    D4040RAM.resize(RAM_SIZE);
    D4040FDCROM.resize(FDC_ROM_SIZE, 0xFF);
    D4040ROM1.resize(ROM_SIZE, 0xFF);
    D4040ROM2.resize(ROM_SIZE, 0xFF);
    D4040ROM3.resize(ROM_SIZE, 0xFF);
}

D4040Memory::~D4040Memory() = default;


void D4040Memory::attachPeripheralInstance(Peripheral* parentPeripheral)
{
    this->parentPeripheral = parentPeripheral;
    via.attachPeripheralInstance(parentPeripheral, DriveVIA6522::VIARole::VIA2_Mechanics);
}

void D4040Memory::reset()
{
    riot1.reset();
    riot2.reset();
    rriot.reset();
    via.reset();
}

void D4040Memory::tick(uint32_t cycles)
{
    riot1.tick(cycles);
    riot2.tick(cycles);
    rriot.tick(cycles);
    via.tick(cycles);
}

uint8_t D4040Memory::read(uint16_t address)
{
    address = normalizeAddress(address);

     // RIOT RAM and stack-page mirrors
    if (address <= RIOT_RAM_MIRROR_END)
    {
        if ((address & 0x80) == 0)
            return riot1.readRAM(address & 0x7F);

        return riot2.readRAM(address & 0x7F);
    }

    // RIOT I/O registers and mirrors
    if (address >= RIOT1_IO_START && address <= RIOT_IO_MIRROR_END)
    {
        // Normalize the mirrored address
        uint16_t offset = address & 0xFF;

        if ((offset & 0x80) == 0)
            return riot1.read(offset & 0x1F);

        return riot2.read(offset & 0x1F);
    }

    if (address >= SHARED_RAM1_START && address <= SHARED_RAM4_MIRROR_END)
    {
        uint16_t bank = (address - SHARED_RAM1_START) >> 12;
        uint16_t offset = (bank << 10) | (address & 0x03FF);

        return D4040RAM[offset];
    }

    // DOS ROM 1
    if (address >= ROM1_START && address <= ROM1_END)
        return D4040ROM1[address - ROM1_START];

    // DOS ROM 2
    if (address >= ROM2_START && address <= ROM2_END)
        return D4040ROM2[address - ROM2_START];

    // DOS ROM 3
    if (address >= ROM3_START && address <= ROM3_END)
        return D4040ROM3[address - ROM3_START];

    return 0xFF;
}


void D4040Memory::write(uint16_t address, uint8_t value)
{
    address = normalizeAddress(address);

    // RIOT RAM and stack-page mirrors
    if (address <= RIOT_RAM_MIRROR_END)
    {
        if ((address & 0x80) == 0)
            riot1.writeRAM(address & 0x7F, value);
        else
            riot2.writeRAM(address & 0x7F, value);

        return;
    }

    // RIOT I/O registers and mirrors
    if (address >= RIOT1_IO_START && address <= RIOT_IO_MIRROR_END)
    {
        uint16_t offset = address & 0xFF;

        if ((offset & 0x80) == 0)
            riot1.write(offset & 0x1F, value);
        else
            riot2.write(offset & 0x1F, value);

        return;
    }

    // Shared RAM and mirrors
    if (address >= SHARED_RAM1_START && address <= SHARED_RAM4_MIRROR_END)
    {
        uint16_t bank = (address - SHARED_RAM1_START) >> 12;
        uint16_t offset = (bank << 10) | (address & 0x03FF);

        D4040RAM[offset] = value;
        return;
    }

    // ROM and unmapped addresses: ignore writes
}

uint8_t D4040Memory::peek(uint16_t address) const
{
    address = normalizeAddress(address);

    // RIOT RAM and stack-page mirrors
    if (address <= RIOT_RAM_MIRROR_END)
    {
        if ((address & 0x80) == 0)
            return riot1.readRAM(address & 0x7F);

        return riot2.readRAM(address & 0x7F);
    }

    // RIOT I/O registers and mirrors
    if (address >= RIOT1_IO_START && address <= RIOT_IO_MIRROR_END)
    {
        uint16_t offset = address & 0xFF;

        if ((offset & 0x80) == 0)
            return riot1.peek(offset & 0x1F);

        return riot2.peek(offset & 0x1F);
    }

    // Shared RAM and mirrors
    if (address >= SHARED_RAM1_START && address <= SHARED_RAM4_MIRROR_END)
    {
        uint16_t bank = (address - SHARED_RAM1_START) >> 12;
        uint16_t offset = (bank << 10) | (address & 0x03FF);

        return D4040RAM[offset];
    }

    // DOS ROM 1
    if (address >= ROM1_START && address <= ROM1_END)
        return D4040ROM1[address - ROM1_START];

    // DOS ROM 2
    if (address >= ROM2_START && address <= ROM2_END)
        return D4040ROM2[address - ROM2_START];

    // DOS ROM 3
    if (address >= ROM3_START && address <= ROM3_END)
        return D4040ROM3[address - ROM3_START];

    // Unmapped memory
    return 0xFF;
}


uint8_t D4040Memory::read6504(uint16_t address)
{
    // MOS6530 private RAM (64 bytes)
    if (address >= FDC_RAM_START && address <= FDC_RAM_END)
        return rriot.readRAM(address - FDC_RAM_START);

    // 6504 stack-page mirror of the MOS6530 RAM.
    // The controller firmware uses the upper part
    // of this 64-byte RAM for its hardware stack.
    if (address >= FDC_STACK_MIRROR_START && address <= FDC_STACK_MIRROR_END)
        return rriot.readRAM(address & FDC_STACK_MIRROR_MASK);

    // MOS6522 controller VIA
    if (address >= FDC_VIA_START && address <= FDC_VIA_END)
        return via.readRegister(address - FDC_VIA_START);

    // MOS6530 I/O registers
    if (address >= FDC_RRIOT_IO_START && address <= FDC_RRIOT_IO_END)
        return rriot.read(address - FDC_RRIOT_IO_START);

    // Shared 4 KB RAM
    if (address >= FDC_SHARED_RAM_START && address <= FDC_SHARED_RAM_END)
        return D4040RAM[address - FDC_SHARED_RAM_START];

    // MOS6530 controller ROM
    if (address >= FDC_ROM_START && address <= FDC_ROM_END)
        return D4040FDCROM[address - FDC_ROM_START];

    return 0xFF;
}


void D4040Memory::write6504(uint16_t address, uint8_t value)
{
    // MOS6530 private RAM
    if (address >= FDC_RAM_START && address <= FDC_RAM_END)
    {
        rriot.writeRAM(address - FDC_RAM_START, value);
        return;
    }

    // 6504 hardware stack writes.
    if (address >= FDC_STACK_MIRROR_START && address <= FDC_STACK_MIRROR_END)
    {
        rriot.writeRAM(address & FDC_STACK_MIRROR_MASK, value);
        return;
    }

    // MOS6522 controller VIA
    if (address >= FDC_VIA_START && address <= FDC_VIA_END)
    {
        via.writeRegister(address - FDC_VIA_START, value);
        return;
    }

    // MOS6530 I/O registers
    if (address >= FDC_RRIOT_IO_START && address <= FDC_RRIOT_IO_END)
    {
        rriot.write(address - FDC_RRIOT_IO_START, value);
        return;
    }

    // Shared 4 KB RAM
    if (address >= FDC_SHARED_RAM_START && address <= FDC_SHARED_RAM_END)
    {
        D4040RAM[address - FDC_SHARED_RAM_START] = value;
        return;
    }

    // ROM and unmapped addresses: ignore writes
}


uint8_t D4040Memory::peek6504(uint16_t address) const
{
    // MOS6530 private RAM
    if (address >= FDC_RAM_START && address <= FDC_RAM_END)
        return rriot.readRAM(address - FDC_RAM_START);

    // Stack-page mirror.
    if (address >= FDC_STACK_MIRROR_START && address <= FDC_STACK_MIRROR_END)
        return rriot.readRAM(address & FDC_STACK_MIRROR_MASK);

    // MOS6522 controller VIA
    if (address >= FDC_VIA_START && address <= FDC_VIA_END)
        return via.peek(address - FDC_VIA_START);

    // MOS6530 I/O registers
    if (address >= FDC_RRIOT_IO_START && address <= FDC_RRIOT_IO_END)
        return rriot.peek(address - FDC_RRIOT_IO_START);

    // Shared 4 KB RAM
    if (address >= FDC_SHARED_RAM_START && address <= FDC_SHARED_RAM_END)
        return D4040RAM[address - FDC_SHARED_RAM_START];

    // MOS6530 controller ROM
    if (address >= FDC_ROM_START && address <= FDC_ROM_END)
        return D4040FDCROM[address - FDC_ROM_START];

    return 0xFF;
}

bool D4040Memory::initialize(const std::string& fdcROM, const std::string& D4040LoROM, const std::string& D4040HiROM1,
                              const std::string& D4040HiROM2)
{
    // Initialize Drive RAM to 0's
    std::fill(D4040RAM.begin(), D4040RAM.end(), 0x00);

    // Initialize all ROMS before loading
    std::fill(D4040ROM1.begin(), D4040ROM1.end(), 0xFF);
    std::fill(D4040ROM2.begin(), D4040ROM2.end(), 0xFF);
    std::fill(D4040ROM3.begin(), D4040ROM3.end(), 0xFF);

    // Try to load in ROM files passed in via config
    if (!loadROM(fdcROM, D4040FDCROM, FDC_ROM_SIZE, "4040 Controller ROM") ||
        !loadROM(D4040LoROM, D4040ROM1, ROM_SIZE, "4040 ROM Lo") || !loadROM(D4040HiROM1, D4040ROM2, ROM_SIZE, "4040 ROM Hi1")
        || !loadROM(D4040HiROM2, D4040ROM3, ROM_SIZE, "4040 ROM Hi2"))
    {
        return false;
    }

    return true;
}


bool D4040Memory::loadROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    std::streamsize fileSize = file.tellg();
    if (static_cast<size_t>(fileSize) != expectedSize)
        return false;

    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(targetBuffer.data()), expectedSize))
        return false;

    file.close();
    return true;
}


uint16_t D4040Memory::normalizeAddress(uint16_t address)
{
    // Additional RIOT mirrors
    if ((address >= RIOT_MIRROR2_START && address <= RIOT_MIRROR2_END) || (address >= RIOT_MIRROR3_START && address <= RIOT_MIRROR3_END))
        return address & 0x03FF;

    // Additional shared RAM mirrors
    if (address >= RAM_MIRROR_START && address <= RAM_MIRROR_END)
        return address & 0x7FFF;

    // DOS ROM mirrors
    if (address >= ROM_MIRROR_START && address <= ROM_MIRROR_END)
        return address | 0x8000;

    return address;
}
