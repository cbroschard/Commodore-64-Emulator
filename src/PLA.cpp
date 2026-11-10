// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CPU.h"
#include "PLA.h"
#include "Vic.h"

PLA::PLA() :
    cart(nullptr),
    cpu(nullptr),
    traceMgr(nullptr),
    vic(nullptr),
    lastModeIndex(0xFF),
    lastloram(false),
    lasthiram(false),
    lastcharen(false),
    lastexROMLine(false),
    lastgameLine(false),
    cachedModeIndex(0xFF),
    cachedGameKillerOverride(false),
    bankCacheValid(false)
{
    reset();
}

PLA::~PLA() = default;

void PLA::reset()
{
    // No cartridge
    exROMLine = true;
    gameLine = true;
    cartridgeAttached = false;

    // Default memory control register state on power on
    updateMemoryControlRegister(0x37);

    // Force cache reconstruction after reset.
    bankCacheValid = false;
}

void PLA::updateMemoryControlRegister(uint8_t value)
{
    const uint8_t oldValue = memoryControlRegister;
    memoryControlRegister = value;

    loram  = (value & 0x01) != 0;
    hiram  = (value & 0x02) != 0;
    charen = (value & 0x04) != 0;

    if (traceMgr && traceMgr->plaDetailOn(TraceManager::TraceDetail::PLA_PORT))
    {
        TraceManager::Stamp stamp = traceMgr->makeStamp(
            cpu ? cpu->getTotalCycles() : 0,
            vic ? vic->getCurrentRaster() : 0xFFFF,
            vic ? vic->getRasterDot() : 0xFFFF);

        traceMgr->recordPlaPortWrite(oldValue, memoryControlRegister,
                                     gameLine, exROMLine, charen, hiram, loram, stamp);
    }
}

bool PLA::is8K() const
{
    if (cart && cartridgeAttached)
        return cart->getGameLine() && !cart->getExROMLine();

    return false;
}

bool PLA::is16K() const
{
    if (cart && cartridgeAttached)
        return !cart->getGameLine() && !cart->getExROMLine();

    return false;
}

bool PLA::isUltimax() const
{
    if (cart && cartridgeAttached)
        return !cart->getGameLine() && cart->getExROMLine();

    return false;
}

PLA::memoryAccessInfo PLA::getMemoryAccess(uint16_t address)
{
    // Read the current cartridge control lines.
    // These may change dynamically during cartridge operation.
    if (cart && cartridgeAttached)
    {
        exROMLine = cart->getExROMLine();
        gameLine = cart->getGameLine();
    }
    else
    {
        exROMLine = true;
        gameLine = true;
    }

    // Compute current PLA mode.
    const uint8_t modeIndex =
        ((exROMLine ? 1 : 0) << 4) |
        ((gameLine  ? 1 : 0) << 3) |
        ((charen    ? 1 : 0) << 2) |
        ((hiram     ? 1 : 0) << 1) |
        (loram      ? 1 : 0);

    // Game Killer has special memory mapping when configured
    // as Ultimax. Detect it separately from the PLA mode.
    const bool gameKillerOverride = cart && cartridgeAttached && cart->getType() == Cartridge::CartridgeType::GAME_KILLER &&
        !gameLine && exROMLine;

    // Preserve the existing mode-change tracing behavior.
    const bool changed = modeIndex != lastModeIndex || loram != lastloram || hiram != lasthiram || charen != lastcharen ||
        exROMLine != lastexROMLine || gameLine != lastgameLine;

    if (changed && traceMgr && traceMgr->plaDetailOn(TraceManager::TraceDetail::PLA_MODE))
    {
        const TraceManager::Stamp stamp = traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0,
                vic ? vic->getCurrentRaster() : 0, vic ? vic->getRasterDot() : 0);

        traceMgr->recordPlaMode(modeIndex, gameLine, exROMLine, charen, hiram, loram, stamp);
    }

    lastModeIndex = modeIndex;
    lastloram = loram;
    lasthiram = hiram;
    lastcharen = charen;
    lastexROMLine = exROMLine;
    lastgameLine = gameLine;

    // Rebuild the cache only when the effective PLA
    // configuration changes.
    if (!bankCacheValid || modeIndex != cachedModeIndex || gameKillerOverride != cachedGameKillerOverride)
        rebuildBankCache(modeIndex, gameKillerOverride);

    memoryAccessInfo info{};

    // Fast cached bank lookup.
    info.bank = bankCache[address >> 12];

    // Calculate the offset within the selected bank.
    switch (info.bank)
    {
        case BASIC_ROM:
            info.offset = address - 0xA000;
            break;

        case KERNAL_ROM:
            info.offset = address - 0xE000;
            break;

        case CHARACTER_ROM:
            info.offset = address - 0xD000;
            break;

        case CARTRIDGE_LO:
            info.offset = address - 0x8000;
            break;

        case CARTRIDGE_HI:
            info.offset = address - 0xA000;
            break;

        case CARTRIDGE_HI_E000:
            info.offset = address - 0xE000;
            break;

        default:
            info.offset = address;
            break;
    }

    // Preserve address-resolution tracing.
    if (traceMgr && traceMgr->plaDetailOn(TraceManager::TraceDetail::PLA_RESOLVE))
    {
        const bool interesting = address <= 0x0007 || address >= 0x8000;

        if (interesting)
        {
            const TraceManager::Stamp stamp = traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0,
                    vic ? vic->getCurrentRaster() : 0xFFFF, vic ? vic->getRasterDot() : 0xFFFF);

            traceMgr->recordPlaResolve(address, bankToString(info.bank), info.offset, memoryControlRegister,
                modeIndex, gameLine, exROMLine, charen, hiram, loram, stamp);
        }
    }

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

    // Game Killer uses normal C64 mapping below $E000,
    // but its cartridge ROM replaces the KERNAL at $E000-$FFFF.
    if (cart && cartridgeAttached && cart->getType() == Cartridge::CartridgeType::GAME_KILLER && cfg == CartCfg::Ultimax)
    {
        if (addr >= 0xE000)
            return CARTRIDGE_HI_E000;

        cfg = CartCfg::None;
    }

    // ---------- Ultimax ----------
    // Ultimax completely bypasses normal memory mapping rules
    if (cfg == CartCfg::Ultimax)
    {
        if (addr <= 0x0FFF)
            return RAM;

        if (addr >= 0x8000 && addr <= 0x9FFF)
            return CARTRIDGE_LO;

        if (addr >= 0xD000 && addr <= 0xDFFF)
            return IO;

        if (addr >= 0xE000)
            return CARTRIDGE_HI_E000;

        return UNMAPPED;
    }

    // ---------- $8000-$9FFF (ROML) ----------
    if (addr >= 0x8000 && addr <= 0x9FFF)
    {
        // In normal 8K/16K cartridge modes, ROML is visible only when HIRAM is high.
        // If HIRAM is low, RAM underneath $8000-$9FFF is visible.
        if ((cfg == CartCfg::Cart8K || cfg == CartCfg::Cart16K) && hiram)
            return CARTRIDGE_LO;

        return RAM;
    }

    // ---------- $A000-$BFFF (ROMH / BASIC) ----------
    if (addr >= 0xA000 && addr <= 0xBFFF)
    {
        // 16K cart maps ROMH here regardless of LORAM/HIRAM
        if (cfg == CartCfg::Cart16K && hiram)
            return CARTRIDGE_HI;

        // No cart: BASIC depends on LORAM+HIRAM
        if ((cfg == CartCfg::None || cfg == CartCfg::Cart8K) && loram && hiram)
            return BASIC_ROM;

        return RAM;
    }

    // ---------- $D000-$DFFF (I/O / CHAR_ROM) ----------
    if (addr >= 0xD000 && addr <= 0xDFFF)
    {
        // 64K RAM mode completely exposes the RAM under I/O
        if (!hiram && !loram)
            return RAM;

        return charen ? IO : CHARACTER_ROM;
    }

    // ---------- $E000-$FFFF (KERNAL) ----------
    if (addr >= 0xE000)
    {
        // KERNAL ROM relies solely on HIRAM for None, 8K, and 16K modes
        if (hiram)
            return KERNAL_ROM;

        return RAM;
    }

    // ---------- Everything Else ----------
    // $0000-$7FFF and $C000-$CFFF are always RAM in normal modes
    return RAM;
}

const char* PLA::bankToString(PLA::memoryBank bank)
{
    switch (bank)
    {
        case PLA::RAM:                  return "RAM";
        case PLA::BASIC_ROM:            return "BASIC ROM";
        case PLA::KERNAL_ROM:           return "KERNAL ROM";
        case PLA::CHARACTER_ROM:        return "Char ROM";
        case PLA::IO:                   return "I/O";
        case PLA::CARTRIDGE_LO:         return "Cartridge LO";
        case PLA::CARTRIDGE_HI:         return "Cartridge HI";
        case PLA::CARTRIDGE_HI_E000:    return "Cartridge HI ($E000)";
        case PLA::UNMAPPED:             return "Unmapped";
        default:                        return "Unmapped";
    }
}

void PLA::rebuildBankCache(uint8_t modeIndex, bool gameKillerOverride)
{
    // The existing resolveBank() is the single source of truth.
    // Every 4 KB region has a uniform PLA bank selection.
    for (size_t region = 0; region < bankCache.size(); ++region)
    {
        const uint16_t address = static_cast<uint16_t>(region << 12);
        bankCache[region] = resolveBank(address);
    }

    cachedModeIndex = modeIndex;
    cachedGameKillerOverride = gameKillerOverride;
    bankCacheValid = true;
}
