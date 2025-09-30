// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "PLA.h"
#include "PLAMapper.h"

PLA::PLA()
{

}

PLA::~PLA() = default;

void PLA::reset()
{
    // Default memory control register state on power on
    updateMemoryControlRegister(0x37);

    // No cartridge
    exROMLine = true;
    gameLine = true;
    cartridgeAttached = false;

    // ML Monitor logging default off
    setLogging = false;
}

void PLA::updateMemoryControlRegister(uint8_t value)
{
    memoryControlRegister = value;

    // Update all lines
    loram = (value & 0x01);
    hiram = (value & 0x02);
    charen = (value & 0x04);
}

PLA::memoryAccessInfo PLA::getMemoryAccess(uint16_t address)
{
    if (cartridgeAttached)
    {
        // Get current exROMLine and gameLine from the cartridge
        exROMLine = cart->getExROMLine();
        gameLine = cart->getGameLine();
    }

    // Compute the mode index by combining control bits:
    // Bit 4: exROMLine, Bit 3: gameLine, Bit 2: charen, Bit 1: hiram, Bit 0: loram.
    uint8_t modeIndex = ((exROMLine ? 1 : 0) << 4) |
                        ((gameLine   ? 1 : 0) << 3) |
                        ((charen     ? 1 : 0) << 2) |
                        ((hiram      ? 1 : 0) << 1) |
                        (loram      ? 1 : 0);

    // Special case: distinguish Ultimax vs 16K
    if (!exROMLine && !gameLine)
    {
        if (cartridgeAttached && cart->getWiringMode() == Cartridge::WiringMode::CART_ULTIMAX)
        {
            // Force to Ultimax range (16–23)
            modeIndex |= 0x10; // ensure bit4 = 1
        }
    }

    // Retrieve the appropriate mapping configuration.
    const PLAMapper::modeMapping* mappingTable = PLAMapper::getMappings();
    const PLAMapper::modeMapping& currentMode = mappingTable[modeIndex];

    memoryAccessInfo info;
    info.bank = RAM;   // default
    info.offset = address;

    // Find the region covering the requested address.
    for (const auto & region : currentMode.regions)
    {
        if (address >= region.start && address <= region.end)
        {
            info.bank = region.bank;
            info.offset = address - region.offsetBase;

            return info;
        }
    }
    info.bank = UNMAPPED;
    return info;
}

std::string PLA::describeAddress(uint16_t addr) {
    memoryAccessInfo info = getMemoryAccess(addr);

    std::ostringstream out;
    out << "$" << std::hex << std::uppercase << addr
        << " -> " << bankToString(info.bank)
        << " (offset $" << std::hex << info.offset << ")";
    return out.str();
}

std::string PLA::describeMode()
{
    std::ostringstream out;
    out << "PLA State:\n";
    out << "  GAME="  << (gameLine ? "1 (inactive)" : "0 (asserted)") << "\n";
    out << "  EXROM=" << (exROMLine ? "1 (inactive)" : "0 (asserted)") << "\n";
    out << "  HIRAM=" << (hiram ? "1" : "0") << "\n";
    out << "  LORAM=" << (loram ? "1" : "0") << "\n";
    out << "  CHAREN="<< (charen ? "1" : "0") << "\n";

    // Show effective wiring mode if a cart is attached
    if (cartridgeAttached && cart)
    {
        switch (cart->getWiringMode())
        {
            case Cartridge::WiringMode::CART_8K:
                out << "  Cartridge wiring mode: 8K\n";
                break;
            case Cartridge::WiringMode::CART_16K:
                out << "  Cartridge wiring mode: 16K\n";
                break;
            case Cartridge::WiringMode::CART_ULTIMAX:
                out << "  Cartridge wiring mode: Ultimax\n";
                break;
            default:
                out << "  Cartridge wiring mode: None\n";
                break;
        }
    }
    else
    {
        out << "  Cartridge wiring mode: None\n";
    }

    out << "\n";

    // Compute current PLA mode index
    uint8_t modeIndex = (exROMLine << 4) |
                        (gameLine  << 3) |
                        (charen    << 2) |
                        (hiram     << 1) |
                        (loram);

    out << "  PLA Mode Index: " << std::dec << (int)modeIndex << "\n\n";

    auto regionReport = [&](uint16_t start, uint16_t end)
    {
        memoryAccessInfo info = getMemoryAccess(start);
        out << "  $" << std::hex << std::uppercase << start
            << "-$" << end
            << " -> " << bankToString(info.bank) << "\n";
    };

    regionReport(0x0000, 0x0FFF);
    regionReport(0x8000, 0x9FFF);
    regionReport(0xA000, 0xBFFF);
    regionReport(0xC000, 0xCFFF);
    regionReport(0xD000, 0xDFFF);
    regionReport(0xE000, 0xFFFF);

    return out.str();
}

const char* PLA::bankToString(PLA::memoryBank bank)
{
    switch (bank)
    {
        case PLA::RAM:           return "RAM";
        case PLA::BASIC_ROM:     return "BASIC ROM";
        case PLA::KERNAL_ROM:    return "KERNAL ROM";
        case PLA::CHARACTER_ROM: return "Char ROM";
        case PLA::IO:            return "I/O";
        case PLA::CARTRIDGE_LO:  return "Cartridge LO";
        case PLA::CARTRIDGE_HI:  return "Cartridge HI";
        case PLA::UNMAPPED:      return "Unmapped";
        default:                 return "Unknown";
    }
}
