// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Vic.h"

uint8_t Vic::readRegister(uint16_t address)
{
    // Handle all SpriteX and SpriteY registers with helper
    if (address >= 0xD000 && address <= 0xD00F)
    {
        int index = getSpriteIndex(address);
        return latchOpenBus(isSpriteX(address) ? registers.spriteX[index] : registers.spriteY[index]);
    }
    // Handle multicolor registers with helper
    else if (address >= 0xD022 && address <= 0xD024)
    {
        return latchOpenBus(0xF0 | (getBackgroundColor(address - 0xD022) & 0x0F));
    }
    // Handle SpriteColor registers with helper
    else if (address >= 0xD027 && address <= 0xD02E)
    {
        int index = getSpriteColorIndex(address);
        return latchOpenBus(0xF0 | (registers.spriteColors[index] & 0x0F));
    }

    switch(address)
    {
        case 0xD010:
            return latchOpenBusMasked(registers.spriteX_MSB, 0xFF);

        case 0xD011:
        {
            const uint16_t visibleRaster = visibleRasterForRead();
            const uint8_t highBit = (visibleRaster >> 8) & 0x01;

            const uint8_t value = (registers.control & 0x7F) | (highBit << 7);
            return latchOpenBusMasked(value, 0xFF);
        }

        case 0xD012:
        {
            const uint16_t visibleRaster = visibleRasterForRead();
            return latchOpenBus(visibleRaster & 0xFF);
        }

        case 0xD013:
            return latchOpenBus(registers.light_pen_X);

        case 0xD014:
            return latchOpenBus(registers.light_pen_Y);

        case 0xD015:
            return latchOpenBusMasked(registers.spriteEnabled, 0xFF);

        case 0xD016:
        {
            // Bits:
            // 4 = MCM
            // 3 = CSEL
            // 2-0 = X scroll
            const uint8_t value = registers.control2 & 0x1F;

            return latchOpenBusMasked(value, 0x1F);
        }

        case 0xD017:
            return latchOpenBusMasked(registers.spriteYExpansion, 0xFF);

        case 0xD018:
            return latchOpenBus(static_cast<uint8_t>(registers.memory_pointer | 0x01));

        case 0xD019:
            return latchOpenBus(static_cast<uint8_t>(d019Read() | 0x70));

        case 0xD01A:
            return latchOpenBus(static_cast<uint8_t>(0xF0 | (registers.interruptEnable & 0x0F)));

        case 0xD01B:
            return latchOpenBus(registers.spritePriority);

        case 0xD01C:
            return latchOpenBusMasked(registers.spriteMultiColor, 0xFF);

        case 0xD01D:
            return latchOpenBusMasked(registers.spriteXExpansion, 0xFF);

        case 0xD01E:
        {
            uint8_t value = registers.spriteCollision;
            registers.spriteCollision = 0;
            return latchOpenBus(value);
        }

        case 0xD01F:
        {
            uint8_t value = registers.spriteDataCollision;
            registers.spriteDataCollision = 0;
            return latchOpenBus(value);
        }

        case 0xD020:
            return latchOpenBus(0xF0 | (registers.borderColor & 0x0F));

        case 0xD021:
            return latchOpenBus(0xF0 | (registers.backgroundColor0 & 0x0F));

        case 0xD025:
            return latchOpenBus(0xF0 | (registers.spriteMultiColor1 & 0x0F));

        case 0xD026:
            return latchOpenBus(0xF0 | (registers.spriteMultiColor2 & 0x0F));

        case 0xD02F:
        case 0xD030:
        default:
            return getOpenBus();
    }
}

uint8_t Vic::peekRegister(uint16_t address) const
{
    address = static_cast<uint16_t>(0xD000 | (address & 0x003F));

    if (address >= 0xD000 && address <= 0xD00F)
    {
        const int index = getSpriteIndex(address);

        return isSpriteX(address) ? registers.spriteX[index] : registers.spriteY[index];
    }

    if (address >= 0xD022 && address <= 0xD024)
        return static_cast<uint8_t>(0xF0 | (getBackgroundColor(address - 0xD022) & 0x0F));

    if (address >= 0xD027 && address <= 0xD02E)
    {
        const int sprite = static_cast<int>(address - 0xD027);
        return static_cast<uint8_t>(0xF0 | (registers.spriteColors[sprite] & 0x0F));
    }

    switch(address)
    {
        case 0xD010:
            return registers.spriteX_MSB;

        case 0xD011:
        {
            const uint16_t visibleRaster = visibleRasterForRead();
            return static_cast<uint8_t>((registers.control & 0x7F) |(((visibleRaster >> 8) & 0x01) << 7));
        }

        case 0xD012:
        {
            const uint16_t visibleRaster = visibleRasterForRead();
            return static_cast<uint8_t>(visibleRaster & 0xFF);
        }

        case 0xD013:
            return registers.light_pen_X;

        case 0xD014:
            return registers.light_pen_Y;

        case 0xD015:
            return registers.spriteEnabled;

        case 0xD016:
            return registers.control2;

        case 0xD017:
            return registers.spriteYExpansion;

        case 0xD018:
            return registers.memory_pointer;

        case 0xD019:
            return static_cast<uint8_t>(d019Read() | 0x70);

        case 0xD01A:
            return static_cast<uint8_t>(0xF0 | (registers.interruptEnable & 0x0F));

        case 0xD01B:
            return registers.spritePriority;

        case 0xD01C:
            return registers.spriteMultiColor;

        case 0xD01D:
            return registers.spriteXExpansion;

        case 0xD01E:
            return registers.spriteCollision;

        case 0xD01F:
            return registers.spriteDataCollision;

        case 0xD020:
            return registers.borderColor;

        case 0xD021:
            return registers.backgroundColor0;

        case 0xD025:
            return registers.spriteMultiColor1;

        case 0xD026:
            return registers.spriteMultiColor2;

        default:
            return 0xFF;
    }

}

void Vic::writeRegister(uint16_t address, uint8_t value)
{
    // Handle SpriteX and SpriteY registers with helper
    if (address >= 0xD000 && address <= 0xD00F)
    {
        int index = getSpriteIndex(address);

        if (isSpriteX(address))
        {
            const uint8_t oldValue = registers.spriteX[index];
            registers.spriteX[index] = value;

            recordRasterSpriteXWrite(address, oldValue, registers.spriteX[index]);
            traceVicRegWrite(address, oldValue, registers.spriteX[index]);
        }
        else
        {
            const uint8_t oldValue = registers.spriteY[index];
            registers.spriteY[index] = value;
            traceVicRegWrite(address, oldValue, registers.spriteY[index]);
        }
        return;
    }

    // Handle multicolor registers with helper
    else if (address >= 0xD022 && address <= 0xD024)
    {
        const int index = address - 0xD022;
        const uint8_t oldValue = registers.backgroundColor[index];

        registers.backgroundColor[index] = value & 0x0F;

        traceVicRegWrite(address, oldValue, registers.backgroundColor[index]);
        return;
    }

    // Handle Sprite Color registers with helper
    else if (address >= 0xD027 && address <= 0xD02E)
    {
        int index = getSpriteColorIndex(address);
        const uint8_t oldValue = registers.spriteColors[index];

        registers.spriteColors[index] = value & 0x0F;

        traceVicRegWrite(address, oldValue, registers.spriteColors[index]);
        return;
    }

    switch (address)
    {
        case 0xD010:
        {
            const uint8_t oldValue = registers.spriteX_MSB;
            registers.spriteX_MSB = value;

            recordRasterSpriteXWrite(address, oldValue, registers.spriteX_MSB);
            traceVicRegWrite(address, oldValue, registers.spriteX_MSB);
            break;
        }

        case 0xD011:
        {
            const uint8_t oldValue = registers.control;
            const uint8_t oldRasterHigh = static_cast<uint8_t>((registers.rasterInterruptLine >> 8) & 0x01);

            registers.control = value & 0x7F;

            recordRasterEventLog(RasterEventKind::Control, 0xD011, oldValue, registers.control);

            const uint8_t newRasterHigh = static_cast<uint8_t>((value >> 7) & 0x01);

            if (oldRasterHigh != newRasterHigh)
                recordRasterEventLog(RasterEventKind::RasterIRQTarget, 0xD011, oldRasterHigh, newRasterHigh);

            const uint16_t newLine = static_cast<uint16_t>((registers.rasterInterruptLine & 0x00FF) |
                (static_cast<uint16_t>(value & 0x80) << 1));

            setRasterIRQTarget(newLine, "D011", value, true);

            const int raster = registers.raster;

            updateGraphicsMode(raster);
            updateMonitorCaches(raster);

            traceVicRegWrite(address, oldValue, registers.control);
            break;
        }

        case 0xD012:
        {
            const uint8_t oldLow = static_cast<uint8_t>(registers.rasterInterruptLine & 0x00FF);
            const uint16_t newLine = static_cast<uint16_t>((registers.rasterInterruptLine & 0x0100) |static_cast<uint16_t>(value));

            setRasterIRQTarget(newLine, "D012", value, false);

            recordRasterEventLog(RasterEventKind::RasterIRQTarget, 0xD012, oldLow, value);

            traceVicRegWrite(address, oldLow, value);
            break;
        }

        case 0xD013:
        case 0xD014:
            // Light-pen X/Y registers are read-only.
            // CPU writes have no effect.
            break;

        case 0xD015:
        {
            const uint8_t oldValue = registers.spriteEnabled;
            registers.spriteEnabled = value;

            traceVicRegWrite(address, oldValue, registers.spriteEnabled);
            break;
        }

        case 0xD016:
        {
            const uint8_t oldValue = registers.control2;
            registers.control2 = value;

            recordRasterEventLog(RasterEventKind::Control2, 0xD016, oldValue, registers.control2);

            const int raster = registers.raster;
            updateGraphicsMode(raster);

            traceVicRegWrite(address, oldValue, registers.control2);
            break;
        }

        case 0xD017:
        {
            const uint8_t oldValue = registers.spriteYExpansion;
            registers.spriteYExpansion = value;

            recordRasterEventLog(RasterEventKind::SpriteYExpansion, 0xD017, oldValue, registers.spriteYExpansion);

            const uint8_t falling = static_cast<uint8_t>(oldValue & ~registers.spriteYExpansion);

            for (int sprite = 0; sprite < 8; ++sprite)
            {
                const uint8_t bit = static_cast<uint8_t>(1u << sprite);

                if ((falling & bit) == 0)
                    continue;

                SpriteUnit& unit = spriteUnits[sprite];

                // Clearing MxYE sets the vertical expansion flip-flop.
                unit.yExpandFlipFlop = true;

                // A clear during the first MCBASE update stage
                // causes the VIC-II sprite-crunch behavior.
                if (unit.dmaActive && currentCycle == cfg_->spriteMcBaseAdvanceCycle1)
                    unit.yCrunchPending = true;
            }

            traceVicRegWrite(address, oldValue, registers.spriteYExpansion);

            break;
        }

        case 0xD018:
        {
            const uint8_t oldValue = registers.memory_pointer;
            registers.memory_pointer = value & 0xFE;

            recordRasterEventLog(RasterEventKind::MemoryPointer, 0xD018, oldValue, registers.memory_pointer);

            traceVicRegWrite(address, oldValue, registers.memory_pointer);
            break;
        }

        case 0xD019:
        {
            const uint8_t oldPending = static_cast<uint8_t>(registers.interruptStatus & 0x0F);

            const uint8_t clearMask = static_cast<uint8_t>(value & 0x0F);

            const bool reassertRasterIRQ = ((clearMask & 0x01) != 0) && rasterIrqDeferredReassert && rasterCompareMatchesNow();

            // $D019 is a write-1-to-clear interrupt-source latch.
            registers.interruptStatus = static_cast<uint8_t>(registers.interruptStatus & static_cast<uint8_t>(~clearMask));

            const uint8_t newPending = static_cast<uint8_t>(registers.interruptStatus & 0x0F);

            updateIRQLine();

            traceVicRegWrite(address, oldPending, newPending);

            if (reassertRasterIRQ)
            {
                registers.interruptStatus |= 0x01;
                updateIRQLine();
            }

            if ((clearMask & 0x01) != 0)
                rasterIrqDeferredReassert = false;

            break;
        }

        case 0xD01A:
        {
            const uint8_t oldValue = registers.interruptEnable & 0x0F;

            registers.interruptEnable = value & 0x0F;

            traceVicRegWrite(address, oldValue, static_cast<uint8_t>(registers.interruptEnable & 0x0F));
            updateIRQLine();
            break;
        }

        case 0xD01B:
        {
            const uint8_t oldValue = registers.spritePriority;
            registers.spritePriority = value;

            recordRasterPriorityWrite(oldValue, registers.spritePriority);
            traceVicRegWrite(address, oldValue, registers.spritePriority);
            break;
        }

        case 0xD01C:
        {
            const uint8_t oldValue = registers.spriteMultiColor;
            registers.spriteMultiColor = value;

            recordRasterSpriteModeWrite(oldValue, registers.spriteMultiColor);
            traceVicRegWrite(address, oldValue, registers.spriteMultiColor);
            break;
        }

        case 0xD01D:
        {
            const uint8_t oldValue = registers.spriteXExpansion;
            registers.spriteXExpansion = value;

            recordRasterSpriteXExpansionWrite(oldValue, registers.spriteXExpansion);
            traceVicRegWrite(address, oldValue, registers.spriteXExpansion);
            break;
        }

        case 0xD01E:
        case 0xD01F:
            break;

        case 0xD020:
        {
            const uint8_t oldValue = registers.borderColor;
            registers.borderColor = value & 0x0F;
            traceVicRegWrite(address, oldValue, registers.borderColor);
            break;
        }

        case 0xD021:
        {
            const uint8_t oldValue = registers.backgroundColor0;
            registers.backgroundColor0 = value & 0x0F;
            traceVicRegWrite(address, oldValue, registers.backgroundColor0);
            break;
        }

        case 0xD025:
        {
            const uint8_t oldValue = registers.spriteMultiColor1;
            registers.spriteMultiColor1 = value & 0x0F;
            traceVicRegWrite(address, oldValue, registers.spriteMultiColor1);
            break;
        }

        case 0xD026:
        {
            const uint8_t oldValue = registers.spriteMultiColor2;
            registers.spriteMultiColor2 = value & 0x0F;
            traceVicRegWrite(address, oldValue, registers.spriteMultiColor2);
            break;
        }

        case 0xD02F:
        case 0xD030:
            break;

        default:
            break;
    }
}

void Vic::triggerLightPenLatch()
{
    if (vicState.lightPenLatchedThisFrame)
        return;

    const uint16_t dotX = getRasterDot();

    registers.light_pen_X = static_cast<uint8_t>((dotX >> 1) & 0xFF);

    registers.light_pen_Y = static_cast<uint8_t>(registers.raster & 0xFF);

    vicState.lightPenLatchedThisFrame = true;

    raiseVicIRQSource(0x08);
}
