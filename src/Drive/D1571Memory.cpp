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

namespace
{
    void sampleA_1571(DriveCIA&, Drive&, uint8_t& outPinsA)
    {
        outPinsA = 0xFF; // TODO: set DRVRDY/DSKCH/devswitch pins later
    }

    void sampleB_1571(DriveCIA&, Drive& drive, uint8_t& outPinsB)
    {
        outPinsB = 0xFF;

        const auto s = drive.snapshotIEC();
        if (s.dataLow) outPinsB &= ~DriveCIA::PRB_DATAIN;
        if (s.clkLow)  outPinsB &= ~DriveCIA::PRB_CLKIN;
        if (s.atnLow)  outPinsB &= ~DriveCIA::PRB_ATNIN;
    }

    void applyA_1571(DriveCIA&, Drive&, uint8_t, uint8_t) {}
    void applyB_1571(DriveCIA&, Drive&, uint8_t, uint8_t) {}

    const DriveCIAWiring kCIA1571Wiring {
        &sampleA_1571,
        &sampleB_1571,
        &applyA_1571,
        &applyB_1571
    };
}

void D1571Memory::attachPeripheralInstance(Peripheral* parentPeripheral)
{
    // Attach the drive first
    this->parentPeripheral = parentPeripheral;

    // Attach the chips to the drive
    via1.attachPeripheralInstance(parentPeripheral, D1571VIA::VIARole::VIA1_IECBus);
    via2.attachPeripheralInstance(parentPeripheral, D1571VIA::VIARole::VIA2_Mechanics);
    cia.attachPeripheralInstance(parentPeripheral);
    cia.setWiring(&kCIA1571Wiring);
    fdc.attachPeripheralInstance(parentPeripheral);

    auto* host = dynamic_cast<FloppyControllerHost*>(parentPeripheral);
    fdc.attachFloppyeControllerHostInstance(host);
}

void D1571Memory::saveState(StateWriter& wrtr) const
{
    wrtr.writeU32(1);

    wrtr.writeU32(static_cast<uint32_t>(D1571RAM.size()));
    wrtr.writeVectorU8(D1571RAM);

    wrtr.writeU8(lastBus);
}

bool D1571Memory::loadState(StateReader& rdr)
{
    uint32_t ver = 0;
    if (!rdr.readU32(ver)) return false;
    if (ver != 1) return false;

    uint32_t ramSize = 0;
    if (!rdr.readU32(ramSize)) return false;
    if (ramSize != D1571RAM.size()) return false;

    if (!rdr.readVectorU8(D1571RAM)) return false;

    if (!rdr.readU8(lastBus)) return false;

    return true;
}

void D1571Memory::reset()
{
    // Reset all RAM to 0's
    std::fill(D1571RAM.begin(), D1571RAM.end(), 0x00);

    lastBus = 0xFF;

    // Disable MLMonitor logging by default
    setLogging = false;

    // Reset all chips
    via1.reset();
    via2.reset();
    cia.reset();
    fdc.reset();
}

void D1571Memory::tick(uint32_t cycles)
{
    via1.tick(cycles);
    via2.tick(cycles);
    cia.tick(cycles);
    fdc.tick(cycles);

    if (parentPeripheral)
    {
        auto* drive = static_cast<D1571*>(parentPeripheral);
        if (!drive->isGCRMode()) drive->syncTrackFromFDC();
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
        value = via1.readRegister((address - VIA1_START) & 0x000F);
    }
    else if (address >= VIA2_START && address <= VIA2_END)
    {
        value = via2.readRegister((address - VIA2_START) & 0x000F);
    }
    else if (address >= CIA_START && address <= CIA_END)
    {
        value = cia.readRegister((address - CIA_START) & 0x000F);
    }
    else if (address >= FDC_START && address <= FDC_END)
    {
        value = fdc.readRegister((address - FDC_START) & 0x0003);
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
        via1.writeRegister((address - VIA1_START) & 0x000F, value);
    }
    else if (address >= VIA2_START && address <= VIA2_END)
    {
        via2.writeRegister((address - VIA2_START) & 0x000F, value);
    }
    else if (address >= CIA_START && address <= CIA_END)
    {
        cia.writeRegister((address - CIA_START) & 0x000F, value);
    }
    else if (address >= FDC_START && address <= FDC_END)
    {
        fdc.writeRegister((address - FDC_START) & 0x0003, value);
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
