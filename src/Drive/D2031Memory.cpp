// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D2031.h"
#include "Drive/D2031Memory.h"

D2031Memory::D2031Memory()
{
    // Initialize Memory to all 0's
    D2031RAM.resize(RAM_SIZE,0);
    D2031ROM1.resize(ROM_SIZE,0);
    D2031ROM2.resize(ROM_SIZE,0);
}

D2031Memory::~D2031Memory() = default;

void D2031Memory::attachPeripheralInstance(Peripheral* parentPeripheral)
{
    // Attach the drive first
    this->parentPeripheral = parentPeripheral;

    // Attach the chips to the drive
    via1.attachPeripheralInstance(parentPeripheral, D2031VIA::VIARole::VIA1_IEEE488Bus);
    via2.attachPeripheralInstance(parentPeripheral, D2031VIA::VIARole::VIA2_Mechanics);
}

void D2031Memory::saveState(StateWriter& wrtr) const
{
    wrtr.writeU32(1); // Version
    wrtr.writeU32(static_cast<uint32_t>(D2031RAM.size()));
    wrtr.writeVectorU8(D2031RAM);
}

bool D2031Memory::loadState(StateReader& rdr)
{
    uint32_t ver = 0;
    if (!rdr.readU32(ver)) return false;
    if (ver != 1) return false;

    uint32_t ramSize = 0;
    if (!rdr.readU32(ramSize)) return false;
    if (ramSize != D2031RAM.size()) return false;

    if (!rdr.readVectorU8(D2031RAM)) return false;
    return true;
}

void D2031Memory::tick(uint32_t cycles)
{
    via1.tick(cycles);
    via2.tick(cycles);

    if (parentPeripheral)
    {
        auto* drive = static_cast<D2031*>(parentPeripheral);
        drive->updateIRQ();
    }
}

void D2031Memory::reset()
{
    // Reset all RAM to 0's
    std::fill(D2031RAM.begin(), D2031RAM.end(), 0x00);

    // Reset both VIA chips
    via1.reset();
    via2.reset();
}

uint8_t D2031Memory::read(uint16_t address)
{
    uint8_t value = 0xFF;

    if (address >= D2031_RAM_START && address <= D2031_RAM_END)
        value = D2031RAM[address - D2031_RAM_START];
    else if (address >= VIA1_START && address <= (VIA1_START + 0x03FF))
        value = via1.readRegister((address - VIA1_START) & 0x0F);
    else if (address >= VIA2_START && address <= (VIA2_START + 0x03FF))
        value = via2.readRegister((address - VIA2_START) & 0x0F);
    else if (address >= ROM1_START && address <= ROM1_END)
        value = D2031ROM1[address - ROM1_START];
    else if (address >= ROM2_START && address <= ROM2_END)
        value = D2031ROM2[address - ROM2_START];

    if (parentPeripheral)
    {
        auto* drive = static_cast<D2031*>(parentPeripheral);
        drive->checkWatchRead(address, value);
    }

    return value;
}

void D2031Memory::write(uint16_t address, uint8_t value)
{
    if (parentPeripheral)
    {
        auto* drive = static_cast<D2031*>(parentPeripheral);
        drive->checkWatchWrite(address, value);
    }

    if (address >= D2031_RAM_START && address <= D2031_RAM_END)
        D2031RAM[address - D2031_RAM_START] = value;
    else if (address >= VIA1_START && address <= (VIA1_START + 0x03FF))
        via1.writeRegister((address - VIA1_START) & 0x0F, value);
    else if (address >= VIA2_START && address <= (VIA2_START + 0x03FF))
        via2.writeRegister((address - VIA2_START) & 0x0F, value);
}

uint8_t D2031Memory::peek(uint16_t address) const
{
    if (address >= D2031_RAM_START && address <= D2031_RAM_END)
        return D2031RAM[address - D2031_RAM_START];
    else if (address >= ROM1_START && address <= ROM1_END)
        return D2031ROM1[address - ROM1_START];
    else if (address >= ROM2_START && address <= ROM2_END)
        return D2031ROM2[address - ROM2_START];

    // Avoid side effects from VIA register reads during monitor inspection.
    return 0xFF;
}

bool D2031Memory::initialize(const std::string& D2031LoROM, const std::string& D2031HiROM)
{
    // Initialize Drive RAM to 0's
    std::fill(D2031RAM.begin(), D2031RAM.end(), 0x00);

    // Initialize both ROMS to 0's before loading
    std::fill(D2031ROM1.begin(), D2031ROM1.end(), 0x00);
    std::fill(D2031ROM2.begin(), D2031ROM2.end(), 0x00);

    // Try to load in ROM files passed in via config
    if (!loadROM(D2031LoROM, D2031ROM1, ROM_SIZE, "2031 ROM Lo") || !loadROM(D2031HiROM, D2031ROM2, ROM_SIZE, "2031 ROM Hi"))
        return false;

    return true;
}

bool D2031Memory::loadROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName)
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
