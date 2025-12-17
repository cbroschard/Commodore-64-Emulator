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

namespace
{
    void sampleA_1581(DriveCIA&, Drive& drive, uint8_t& outPinsA)
    {
        outPinsA = 0xFF;

        // Device number switches (typical mapping: both open=8, SW1 closed=9, SW2 closed=10, both=11)
        const int dn = drive.getDeviceNumber(); // if you don't have this, add a getter in Drive
        const bool sw1Closed = (dn == 9 || dn == 11);
        const bool sw2Closed = (dn == 10 || dn == 11);

        if (sw1Closed) outPinsA &= ~DriveCIA::PRA_DEVSW1;
        if (sw2Closed) outPinsA &= ~DriveCIA::PRA_DEVSW2;

        // Disk present / ready (start simple: "loaded" => ready)
        if (drive.isDiskLoaded()) outPinsA |=  DriveCIA::PRA_DRVRDY;
        else                      outPinsA &= ~DriveCIA::PRA_DRVRDY;

        if (drive.isDiskLoaded()) outPinsA |=  DriveCIA::PRA_DSKCH;
        else                      outPinsA &= ~DriveCIA::PRA_DSKCH;
    }

    void applyA_1581(DriveCIA&, Drive& drive, uint8_t pra, uint8_t ddra)
    {
        auto& d = static_cast<D1581&>(drive);

        // Motor control: PRA_MOTOR comment says 0=on, 1=off :contentReference[oaicite:7]{index=7}
        if (ddra & DriveCIA::PRA_MOTOR)
        {
            if (pra & DriveCIA::PRA_MOTOR) d.stopMotor();
            else                           d.startMotor();
        }

        // Side select (store it so fdcReadSector can use it)
        if (ddra & DriveCIA::PRA_SIDE)
        {
            uint8_t side = (pra & DriveCIA::PRA_SIDE) ? 1 : 0;
            d.setCurrentSide(side);
        }

        // LEDs can be latched/logged later; not required for LOAD to work.
    }

    void sampleB_1581(DriveCIA& cia, Drive& drive, uint8_t& outPinsB)
    {
        outPinsB = 0xFF;

        const auto s = drive.snapshotIEC();
        if (s.dataLow) outPinsB &= ~DriveCIA::PRB_DATAIN;
        if (s.clkLow)  outPinsB &= ~DriveCIA::PRB_CLKIN;
        if (s.atnLow)  outPinsB &= ~DriveCIA::PRB_ATNIN;

        if (auto* host = dynamic_cast<FloppyControllerHost*>(&drive))
        {
            if (host->fdcIsWriteProtected())
                outPinsB &= ~DriveCIA::PRB_WRTPRO;  // "protected" reads low
            else
                outPinsB |=  DriveCIA::PRB_WRTPRO;
        }
    }

    void applyB_1581(DriveCIA&, Drive&, uint8_t, uint8_t) {}

    const DriveCIAWiring kCIA1581Wiring {
        &sampleA_1581,
        &sampleB_1581,
        &applyA_1581,
        &applyB_1581
    };
}

void D1581Memory::attachPeripheralInstance(Peripheral* parentPeripheral)
{
    this->parentPeripheral = parentPeripheral;

    cia.attachPeripheralInstance(parentPeripheral);
    cia.setWiring(&kCIA1581Wiring);
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
    fdc.setSectorSize(512);
}

void D1581Memory::tick(uint32_t cycles)
{
    while(cycles-- > 0)
    {
        cia.tick(1);
        fdc.tick(1);
    }

    if (parentPeripheral)
    {
        auto* drive = static_cast<D1581*>(parentPeripheral);
        drive->syncTrackFromFDC();
        drive->updateIRQ();
    }
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
    uint8_t value = lastBus;

    if (address >= RAM_START && address <= RAM_END)
        value = D1581RAM[address - RAM_START];
    else if (address >= CIA_START && address <= CIA_END)
        value = cia.readRegister((address - CIA_START) & 0x000F);
    else if (address >= FDC_START && address <= FDC_END)
        value = fdc.readRegister((address - FDC_START) & 0x0003);
    else if (address >= ROM_START && address <= ROM_END)
        value = D1581ROM[address - ROM_START];

    lastBus = value;
    return value;
}

void D1581Memory::write(uint16_t address, uint8_t value)
{
    lastBus = value;

    if (address >= RAM_START && address <= RAM_END)
    {
        D1581RAM[address - RAM_START] = value;
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
