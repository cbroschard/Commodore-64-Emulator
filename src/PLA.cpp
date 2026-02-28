// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "PLA.h"

PLA::PLA() :
    cart(nullptr),
    processor(nullptr),
    logger(nullptr),
    traceMgr(nullptr),
    vicII(nullptr),
    lastModeIndex(0xFF),
    lastloram(false),
    lasthiram(false),
    lastcharen(false),
    lastexROMLine(false),
    lastgameLine(false)
{

}

PLA::~PLA() = default;

void PLA::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("PLA0");
    wrtr.writeU32(1); // version
    wrtr.writeU8(memoryControlRegister);
    wrtr.endChunk();
}

bool PLA::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "PLA0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                  { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                           { rdr.exitChunkPayload(chunk); return false; }

        uint8_t mcr = 0;

        if (!rdr.readU8(mcr))                   { rdr.exitChunkPayload(chunk); return false; }

        updateMemoryControlRegister(mcr);

        // reset trace deltas so you don't get a burst of "mode changed" noise
        lastModeIndex = 0xFF;

        rdr.exitChunkPayload(chunk);

        return true;
    }

    // Not our chunk
    return false;
}

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
    if (cart && cartridgeAttached)
    {
        // Get current exROMLine and gameLine from the cartridge
        exROMLine = cart->getExROMLine();
        gameLine = cart->getGameLine();
    }
    else
    {
        exROMLine = true;
        gameLine = true;
    }

    // Compute the mode index by combining control bits:
    // Bit 4: exROMLine, Bit 3: gameLine, Bit 2: charen, Bit 1: hiram, Bit 0: loram.
    uint8_t modeIndex = ((exROMLine ? 1 : 0) << 4) |
                        ((gameLine   ? 1 : 0) << 3) |
                        ((charen     ? 1 : 0) << 2) |
                        ((hiram      ? 1 : 0) << 1) |
                        (loram      ? 1 : 0);

    if (traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::PLA))
    {
        const bool changed = (modeIndex != lastModeIndex || loram != lastloram || hiram != lasthiram || charen != lastcharen
            || exROMLine != lastexROMLine || gameLine != lastgameLine);

        if (changed)
        {
            TraceManager::Stamp stamp = traceMgr->makeStamp(processor ? processor->getTotalCycles() : 0, vicII ? vicII->getCurrentRaster() : 0,
                vicII ? vicII->getRasterDot() : 0);
            traceMgr->recordPlaMode(modeIndex, gameLine, exROMLine, charen, hiram, loram, stamp);
        }

        lastModeIndex = modeIndex;
        lastloram = loram;
        lasthiram = hiram;
        lastcharen = charen;
        lastexROMLine = exROMLine;
        lastgameLine = gameLine;
    }

    memoryAccessInfo info;

    info.bank = resolveBank(address);

    switch (info.bank)
    {
        case BASIC_ROM:     info.offset = address - 0xA000; break;
        case KERNAL_ROM:    info.offset = address - 0xE000; break;
        case CHARACTER_ROM: info.offset = address - 0xD000; break;
        case CARTRIDGE_LO:  info.offset = address - 0x8000; break;

        case CARTRIDGE_HI:
            info.offset = (address >= 0xE000)
                            ? address - 0xE000
                            : address - 0xA000;
            break;

        default:
            info.offset = address;
            break;
    }

    return info;
    info.bank = UNMAPPED;
    return info;
}

std::string PLA::describeAddress(uint16_t addr)
{
    memoryAccessInfo info = getMemoryAccess(addr);

    std::ostringstream out;
    out << "$" << std::hex << std::uppercase << addr
        << " -> " << bankToString(info.bank)
        << " (offset $" << std::hex << info.offset << ")";
    return out.str();
}

std::string PLA::describeMode()
{
    if (cart && cartridgeAttached)
    {
        exROMLine = cart->getExROMLine();
        gameLine  = cart->getGameLine();
    }
    else
    {
        exROMLine = true;
        gameLine  = true;
    }

    std::ostringstream out;
    out << "PLA State:\n";
    out << "  GAME="  << (gameLine ? "1 (inactive)" : "0 (asserted)") << "\n";
    out << "  EXROM=" << (exROMLine ? "1 (inactive)" : "0 (asserted)") << "\n";
    out << "  HIRAM=" << (hiram ? "1" : "0") << "\n";
    out << "  LORAM=" << (loram ? "1" : "0") << "\n";
    out << "  CHAREN="<< (charen ? "1" : "0") << "\n";

    // Show effective wiring mode if a cart is attached
    if (cart && cartridgeAttached)
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

PLA::memoryBank PLA::resolveBank(uint16_t addr) const
{
    // Cartridge configuration derived from GAME/EXROM
    enum class CartCfg { None, Cart8K, Cart16K, Ultimax };

    CartCfg cfg;

    if ( exROMLine &&  gameLine) cfg = CartCfg::None;
    else if (!exROMLine &&  gameLine) cfg = CartCfg::Cart8K;
    else if (!exROMLine && !gameLine) cfg = CartCfg::Cart16K;
    else                               cfg = CartCfg::Ultimax;

    // ---------- Ultimax ----------
    if (cfg == CartCfg::Ultimax)
    {
        if (addr <= 0x0FFF)
            return RAM;

        if (addr >= 0x8000 && addr <= 0x9FFF)
            return CARTRIDGE_LO;

        if (addr >= 0xD000 && addr <= 0xDFFF)
            return IO;

        if (addr >= 0xE000)
            return CARTRIDGE_HI;

        return UNMAPPED;
    }

    // ---------- ROML ($8000-$9FFF) ----------
    if (!(exROMLine && gameLine) &&
        addr >= 0x8000 && addr <= 0x9FFF)
    {
        return CARTRIDGE_LO;
    }

    // ---------- $A000-$BFFF ----------
    if (addr >= 0xA000 && addr <= 0xBFFF)
    {
        if (cfg == CartCfg::Cart16K)
            return CARTRIDGE_HI;

        if (loram && hiram)
            return BASIC_ROM;

        return RAM;
    }

    // ---------- $E000-$FFFF ----------
    if (addr >= 0xE000)
    {
        if (hiram)
            return KERNAL_ROM;

        return RAM;
    }

    // ---------- $D000-$DFFF ----------
    if (addr >= 0xD000 && addr <= 0xDFFF)
    {
        if (!hiram && !loram)
            return RAM;

        return charen ? IO : CHARACTER_ROM;
    }

    return RAM;
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
        default:                 return "Unmapped";
    }
}
