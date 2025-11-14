// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1581.h"
#include "Drive/D1581Memory.h"

D1581Memory::D1581Memory() :
    logger(nullptr),
    parentPeripheral(nullptr),
    setLogging(false),
    lastBus(0xFF)
{
    D1581RAM.resize(RAM_SIZE);
    D1581ROM.resize(ROM_SIZE);
}

D1581Memory::~D1581Memory() = default;

void D1581Memory::attachPeripheralInstance(Peripheral* parentPeripheral)
{
    this->parentPeripheral = parentPeripheral;

    cia.attachPeripheralInstance(parentPeripheral);
    fdc.attachPeripheralInstance(parentPeripheral);

    auto* host = dynamic_cast<FloppyControllerHost*>(parentPeripheral);
    fdc.attachFloppyeControllerHostInstance(host);
}

void D1581Memory::reset()
{
    // Clear RAM
    std::fill(D1581RAM.begin(), D1581RAM.end(), 0x00);

    // Disable ML Monitor logging
    setLogging = false;

    // Default return for open bus
    lastBus = 0xFF;

    // Reset chips
    cia.reset();
    fdc.reset();
}

void D1581Memory::tick()
{
    cia.tick();
    fdc.tick();
}

bool D1581Memory::initialize(const std::string& fileName)
{
    // Clear RAM/ROM
    std::fill(D1581RAM.begin(), D1581RAM.end(), 0x00);
    std::fill(D1581ROM.begin(), D1581ROM.end(), 0x00);

    // Attempt to load passed in ROM file
    if (!loadROM(fileName)) return false;

    // Reset all chips before we return
    reset();

    // All good
    return true;
}

uint8_t D1581Memory::read(uint16_t address)
{
    uint8_t value; // Hold for updating last bus

    if (address >= RAM_START && address <= RAM_END)
    {
        value = D1581RAM[address - RAM_START];
    }
    if (address >= CIA_START && address <= CIA_END)
    {
        uint8_t offset = (address - CIA_START) & 0x000F;
        value = cia.readRegister(offset);
    }
    if (address >= FDC_START && address <= FDC_END)
    {
        uint8_t offset = (address - FDC_START) & 0x0003;
        value = fdc.readRegister(offset);
    }

    lastBus = value;
    return value;
}

void D1581Memory::write(uint16_t address, uint8_t value)
{
    lastBus = value;

    if (address >= RAM_START && address <= RAM_END)
    {
        D1581RAM[address] = value;
    }

    if (address >= CIA_START && address <= CIA_END)
    {
        cia.writeRegister((address - CIA_START) & 0x000F, value);
    }

    if (address >= FDC_START && address <= FDC_END)
    {
        fdc.writeRegister((address - FDC_START) & 0x0003, value);
    }
}

bool D1581Memory::loadROM(const std::string& filename)
{
    std::ifstream f(filename, std::ios::binary | std::ios::ate);
    if (!f) return false;

    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);

    if (size != 0x4000 && size != 0x8000) return false; // accept only 16K or 32K

    std::vector<uint8_t> buf(size);
    if (!f.read(reinterpret_cast<char*>(buf.data()), buf.size())) return false;

    std::fill(D1581ROM.begin(), D1581ROM.end(), 0xFF);

    if (size == 0x8000)
    {
        // 32K -> $8000-$FFFF
        std::copy(buf.begin(), buf.end(), D1581ROM.begin());
    }
    else
    {
        // 16K -> map to $C000-$FFFF (upper half of ROM window)
        const size_t off = 0x4000; // offset inside our 32K vector
        std::copy(buf.begin(), buf.end(), D1581ROM.begin() + off);
    }
    return true;
}
