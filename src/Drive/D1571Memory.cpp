// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1571.h"
#include "Drive/D1571Memory.h"

D1571Memory::D1571Memory() :
    logger(nullptr),
    parentPeripheral(nullptr),
    setLogging(false),
    lastBus(0xFF)
{
    D1571RAM.resize(RAM_SIZE,0);
    D1571ROM.resize(ROM_SIZE,0);
}

D1571Memory::~D1571Memory() = default;

void D1571Memory::attachPeripheralInstance(Peripheral* parentPeripheral)
{
    // Attach the drive first
    this->parentPeripheral = parentPeripheral;

    // Attach the chips to the drive
    via1.attachPeripheralInstance(parentPeripheral, D1571VIA::VIARole::VIA1_IECBus);
    via2.attachPeripheralInstance(parentPeripheral, D1571VIA::VIARole::VIA2_Mechanics);
    cia.attachPeripheralInstance(parentPeripheral);
    fdc.attachPeripheralInstance(parentPeripheral);

    auto* host = dynamic_cast<FloppyControllerHost*>(parentPeripheral);
    fdc.attachFloppyeControllerHostInstance(host);
}

void D1571Memory::reset()
{
    // Reset all RAM to 0's
    std::fill(D1571RAM.begin(), D1571RAM.end(), 0x00);

    // Reset all chips
    via1.reset();
    via2.reset();
    cia.reset();
    fdc.reset();
}

void D1571Memory::tick()
{
    via1.tick();
    via2.tick();
    cia.tick();
    fdc.tick();

    if (parentPeripheral)
    {
        auto* drive = static_cast<D1571*>(parentPeripheral);
        drive->updateIRQ();
    }
}

bool D1571Memory::initialize(const std::string& fileName)
{
    // Clear RAM/ROM
    std::fill(D1571RAM.begin(), D1571RAM.end(), 0x00);
    std::fill(D1571ROM.begin(), D1571ROM.end(), 0x00);

    // Attempt to load passed in ROM file
    if (!loadROM(fileName)) return false;

    // Reset all chips before we return
    reset();

    // All good
    return true;
}

uint8_t D1571Memory::read(uint16_t address)
{
    uint8_t value; // Hold for lastBus update

    if (address >= RAM_START && address <= RAM_END)
    {
        value =  D1571RAM[address - RAM_START];
    }
    else if (address >= 0x0800 && address <= 0x0FFF)
    {
        value = D1571RAM[address & 0x07FF]; // RAM mirror
    }
    else if (address >= ROM_START && address <= ROM_END)
    {
        value = D1571ROM[address - ROM_START];
    }
    else if (address >= VIA1_START && address <= VIA1_END)
    {
        value = via1.readRegister((address - VIA1_START) & 0x0F);
    }
    else if (address >= VIA2_START && address <= VIA2_END)
    {
        value = via2.readRegister((address - VIA2_START) & 0x0F);
    }
    else if (address >= CIA_START && address <= CIA_END)
    {
        value = cia.readRegister((address - CIA_START) & 0x0F);
    }
    else if (address >= FDC_START && address <= FDC_END)
    {
        value = fdc.readRegister((address - FDC_START) & 0x03);
    }
    else
    {
        value = lastBus;
    }
    lastBus = value;
    return value;
}

void D1571Memory::write(uint16_t address, uint8_t value)
{
    // First update lastBus
    lastBus = value;

    if (address >= RAM_START && address <= RAM_END)
    {
        D1571RAM[address - RAM_START] = value;
    }
    else if (address >= 0x0800 && address <= 0x0FFF)
    {
        D1571RAM[address & 0x07FF] = value; // RAM mirror
    }
    else if (address >= VIA1_START && address <= VIA1_END)
    {
        via1.writeRegister((address - VIA1_START) & 0x0F, value);
    }
    else if (address >= VIA2_START && address <= VIA2_END)
    {
        via2.writeRegister((address - VIA2_START) & 0x0F, value);
    }
    else if (address >= CIA_START && address <= CIA_END)
    {
        cia.writeRegister((address - CIA_START) & 0x0F, value);
    }
    else if (address >= FDC_START && address <= FDC_END)
    {
        fdc.writeRegister((address - FDC_START) & 0x03, value);
    }
}

bool D1571Memory::loadROM(const std::string& filename)
{
    std::ifstream f(filename, std::ios::binary | std::ios::ate);
    if (!f) return false;

    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);

    if (size != 0x4000 && size != 0x8000) return false; // accept only 16K or 32K

    std::vector<uint8_t> buf(size);
    if (!f.read(reinterpret_cast<char*>(buf.data()), buf.size())) return false;

    std::fill(D1571ROM.begin(), D1571ROM.end(), 0xFF);

    if (size == 0x8000)
    {
        // 32K -> $8000-$FFFF
        std::copy(buf.begin(), buf.end(), D1571ROM.begin());
    }
    else
    {
        // 16K -> map to $C000-$FFFF (upper half of ROM window)
        const size_t off = 0x4000; // offset inside our 32K vector
        std::copy(buf.begin(), buf.end(), D1571ROM.begin() + off);
    }
    return true;
}
