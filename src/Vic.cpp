// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Bus.h"
#include "CIA2.h"
#include "CPU.h"
#include "DataBusLatch.h"
#include "IVideoSink.h"
#include "IRQLine.h"
#include "Vic.h"

Vic::Vic(VideoMode mode) :
    currentBusPhase(VicBusPhase::Phi1),
    bus(nullptr),
    cia2(nullptr),
    cpu(nullptr),
    dataBus(nullptr),
    sink(nullptr),
    IRQ(nullptr),
    traceMgr(nullptr),
    mode_(mode),
    cfg_(mode == VideoMode::NTSC ? &NTSC_CONFIG : &PAL_CONFIG)
{
    d011_per_raster.resize(cfg_->maxRasterLines);
    d016_per_raster.resize(cfg_->maxRasterLines);
    d018_per_raster.resize(cfg_->maxRasterLines);

    rasterEventsByRaster.resize(cfg_->maxRasterLines);
    lastFrameRasterEventsByRaster.resize(cfg_->maxRasterLines);

    rasterRowStates.resize(cfg_->maxRasterLines);
    lastFrameRasterRowStates.resize(cfg_->maxRasterLines);

    rasterPixelStates.resize(cfg_->maxRasterLines);
    lastFrameRasterPixelStates.resize(cfg_->maxRasterLines);

    borderVertical_per_raster.resize(cfg_->maxRasterLines);
    borderVerticalStart_per_raster.resize(cfg_->maxRasterLines);
    borderLeftOpenX_per_raster.resize(cfg_->maxRasterLines);
    borderRightCloseX_per_raster.resize(cfg_->maxRasterLines);
}

Vic::~Vic() = default;

void Vic::reset()
{
    // Initialize all registers to default values
    std::fill(std::begin(registers.spriteX), std::end(registers.spriteX), 0x00);
    std::fill(std::begin(registers.spriteY), std::end(registers.spriteY), 0x00);
    std::fill(std::begin(registers.spriteColors), std::end(registers.spriteColors), 0x00);
    registers.spriteX_MSB = 0x00;
    registers.control = 0x1B;
    registers.raster = 0x00;
    registers.light_pen_X = 0x00;
    registers.light_pen_Y = 0x00;
    registers.spriteEnabled = 0x00;
    registers.control2 = 0x08;
    registers.spriteYExpansion = 0x00;
    registers.memory_pointer = 0x14;
    registers.interruptStatus = 0x00;
    registers.interruptEnable = 0x00;
    registers.spritePriority = 0x00;
    registers.spriteMultiColor = 0x00;
    registers.spriteXExpansion = 0x00;
    registers.spriteCollision = 0x00;
    registers.spriteDataCollision = 0x00;
    registers.backgroundColor0 = 0x00;
    registers.borderColor = 0x00;
    registers.backgroundColor[0] = 0x00;
    registers.backgroundColor[1] = 0x00;
    registers.backgroundColor[2] = 0x00;
    registers.spriteMultiColor1 = 0x00;
    registers.spriteMultiColor2 = 0x00;
    // Use an out-of-range 9-bit target as the default disabled/no-match target.
    registers.rasterInterruptLine = 0x01FF;
    registers.undefined = 0xFF; // Undefined always returns 0xFF

    // AEC
    currentCycle = 0;

    // Raster IRQ
    rasterIrqCompareMatched = false;
    rasterIrqDeferredReassert = false;
    rasterIrqTriggeredThisLine = false;
    lastRasterIRQSample = {};

    // Internal VIC state
    vicState.vc = 0;
    vicState.vcBase = 0;
    vicState.vmliBase = 0;
    vicState.vmliFetchIndex = 0;
    vicState.rc = 0;

    vicState.refreshCounter = 0xFF;

    vicState.displayEnabled = false;
    vicState.displayEnabledNext = false;
    vicState.displayStateHoldForCycle58 = false;

    vicState.badLineCondition = false;
    vicState.badLineLatchedAt14 = false;
    vicState.cAccessActive = false;
    vicState.badLineDmaStartCycle = -1;
    vicState.badLineFetchIndex = 0;
    vicState.matrixFetchInitializedThisRaster = false;

    vicState.verticalBorder = true;
    vicState.horizontalBorder = true;

    vicState.leftBorder = true;
    vicState.rightBorder = true;

    vicState.leftBorderOpenX = 0;
    vicState.rightBorderCloseX = VISIBLE_WIDTH;

    vicState.topBorderOpenRaster = -1;
    vicState.bottomBorderCloseRaster = -1;

    vicState.ba = true;
    vicState.aec = true;

    vicState.lightPenLatchedThisFrame = false;

    for (auto& s : spriteUnits)
    {
        s.dmaActive = false;
        s.yExpandFlipFlop = true;

        s.mc = 0;
        s.mcBase = 0;

        s.pointerByte = 0;
        s.dataBase = 0;

        s.shift0 = 0;
        s.shift1 = 0;
        s.shift2 = 0;

        s.currentRow = 0;

        s.startY = 0;

        s.outputXStart = 0;
        s.outputWidth = 0;

        s.outputBit = 0;

        s.outputRepeat = 0;
        s.rowPrepared = false;
        s.rowDataLatched = false;

        s.yCrunchPending = false;

        s.fetched0 = 0;
        s.fetched1 = 0;
        s.fetched2 = 0;

        s.lastFetchAddr0 = 0;
        s.lastFetchAddr1 = 0;
        s.lastFetchAddr2 = 0;
    }

    std::fill(std::begin(sprPtrBase), std::end(sprPtrBase), 0);
    for (auto& line : spriteOpaqueLine) line.fill(0);
    for (auto& line : spriteColorLine)  line.fill(0);

    for (auto& s : rasterRowStates)
        s = {};

    for (auto& s : lastFrameRasterRowStates)
        s = {};

    for (auto& s : rasterPixelStates)
        s = {};

    for (auto& s : lastFrameRasterPixelStates)
        s = {};

    // Default character mode
    currentMode = graphicsMode::standard;

    // Bad line vars reset
    firstBadlineY = -1;
    denSeenOn30 = false;

    // Frame completion flag
    frameDone = false;

    // Default per raster register latches
    std::fill(std::begin(d011_per_raster), std::end(d011_per_raster), 0x1B);
    std::fill(std::begin(d016_per_raster), std::end(d016_per_raster), 0x08);
    std::fill(std::begin(d018_per_raster), std::end(d018_per_raster), 0x14);

    std::fill(borderVertical_per_raster.begin(), borderVertical_per_raster.end(), 1);
    std::fill(borderVerticalStart_per_raster.begin(), borderVerticalStart_per_raster.end(), 1);
    std::fill(borderLeftOpenX_per_raster.begin(), borderLeftOpenX_per_raster.end(), 0);
    std::fill(borderRightCloseX_per_raster.begin(), borderRightCloseX_per_raster.end(), VISIBLE_WIDTH);

    bgColorLine.fill(0);
    bgOpaqueLine.fill(0);
    bgSourceLine.fill(BackgroundSource::Border);

    borderMaskLine.fill(1);
    finalColorLine.fill(0);

    resetActiveBackgroundPixelState();

    // Rebuild Border Latches
    rebuildBorderRasterLatches();

    // Initialize monitor caches
    updateMonitorCaches(registers.raster);

    // Clear the bad line fifo
    clearBadLineFifo();

    // Clear SPrite Raster Line arrays
    for (auto& line : spriteColorSourceLine)    line.fill(SpriteColorSource::None);

    resetActiveMatrixRow();
    resetCAccessLatch();

    // Sprite collision latches
    lastSpriteSpriteCollision = {};
    lastSpriteBackgroundCollision = {};

    resetActiveBackgroundPixelState();
    resetBackgroundGraphicsLatches();
}

void Vic::setMode(VideoMode mode)
{
    mode_ = mode;
    cfg_  = (mode == VideoMode::NTSC ? &NTSC_CONFIG : &PAL_CONFIG);

    // Update based on mode to the right size
    d011_per_raster.resize(cfg_->maxRasterLines);
    d016_per_raster.resize(cfg_->maxRasterLines);
    d018_per_raster.resize(cfg_->maxRasterLines);

    rasterEventsByRaster.resize(cfg_->maxRasterLines);
    lastFrameRasterEventsByRaster.resize(cfg_->maxRasterLines);

    borderVertical_per_raster.resize(cfg_->maxRasterLines);
    borderVerticalStart_per_raster.resize(cfg_->maxRasterLines);
    borderLeftOpenX_per_raster.resize(cfg_->maxRasterLines);
    borderRightCloseX_per_raster.resize(cfg_->maxRasterLines);

    rasterRowStates.resize(cfg_->maxRasterLines);
    lastFrameRasterRowStates.resize(cfg_->maxRasterLines);

    rasterPixelStates.resize(cfg_->maxRasterLines);
    lastFrameRasterPixelStates.resize(cfg_->maxRasterLines);

    rebuildBorderRasterLatches();

    // Make sure internal state stays consistent
    if (registers.raster >= cfg_->maxRasterLines) registers.raster = 0;

    if (currentCycle < 0)
        currentCycle = 0;

    if (currentCycle >= cfg_->cyclesPerLine)
        currentCycle %= cfg_->cyclesPerLine;

    clearBadLineFifo();
    resetActiveMatrixRow();
    resetCAccessLatch();
    resetActiveBackgroundPixelState();
    resetBackgroundGraphicsLatches();

    std::fill(finalColorLine.begin(), finalColorLine.end(), 0);
    std::fill(borderMaskLine.begin(), borderMaskLine.end(), 1);

    vicState.badLineCondition = false;
    vicState.badLineLatchedAt14 = false;
    vicState.cAccessActive = false;
    vicState.badLineDmaStartCycle = -1;
    vicState.badLineFetchIndex = 0;
    vicState.matrixFetchInitializedThisRaster = false;

    vicState.displayEnabled = false;
    vicState.displayEnabledNext = false;
    vicState.displayStateHoldForCycle58 = false;

    vicState.vc = vicState.vcBase;
    vicState.vmliFetchIndex = 0;

    updateBusArbitration();
    updateMonitorCaches(registers.raster);

    // Notify IO of mode
    if (sink)
        sink->setScreenDimensions(320, cfg_->visibleLines, HORIZONTAL_BORDER_SIZE, VERTICAL_BORDER_SIZE);
}

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

        recordRasterColorWrite(address, oldValue, registers.backgroundColor[index]);
        traceVicRegWrite(address, oldValue, registers.backgroundColor[index]);
        return;
    }

    // Handle Sprite Color registers with helper
    else if (address >= 0xD027 && address <= 0xD02E)
    {
        int index = getSpriteColorIndex(address);
        const uint8_t oldValue = registers.spriteColors[index];

        registers.spriteColors[index] = value & 0x0F;

        recordRasterColorWrite(address, oldValue, registers.spriteColors[index]);
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

            recordRasterSpriteEnableWrite(oldValue, registers.spriteEnabled);
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
            recordRasterColorWrite(address, oldValue, registers.borderColor);
            traceVicRegWrite(address, oldValue, registers.borderColor);
            break;
        }

        case 0xD021:
        {
            const uint8_t oldValue = registers.backgroundColor0;
            registers.backgroundColor0 = value & 0x0F;
            recordRasterColorWrite(address, oldValue, registers.backgroundColor0);
            traceVicRegWrite(address, oldValue, registers.backgroundColor0);
            break;
        }

        case 0xD025:
        {
            const uint8_t oldValue = registers.spriteMultiColor1;
            registers.spriteMultiColor1 = value & 0x0F;
            recordRasterColorWrite(address, oldValue, registers.spriteMultiColor1);
            traceVicRegWrite(address, oldValue, registers.spriteMultiColor1);
            break;
        }

        case 0xD026:
        {
            const uint8_t oldValue = registers.spriteMultiColor2;
            registers.spriteMultiColor2 = value & 0x0F;
            recordRasterColorWrite(address, oldValue, registers.spriteMultiColor2);
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

void Vic::beginCycle()
{
    currentBusPhase = VicBusPhase::Phi1;

    beginFrameIfNeeded();

    currentCycleSlot = cycleSlotFor(registers.raster, currentCycle);

    runCycleDecisionPhase();

    const uint8_t startedMask = currentCycleSlot.spriteDmaStartMask;

    currentCycleSlot = cycleSlotFor(registers.raster, currentCycle);
    currentCycleSlot.spriteDmaStartMask = startedMask;

    updateBusArbitration();
}

void Vic::endCycle()
{
    runPixelOutputPhase();
    advanceCycleAndFinalizeLineIfNeeded();
}

void Vic::beginFrameIfNeeded()
{
    // Clear frame-local badline/display qualifiers at the very start
    // of the frame only.
    if (currentCycle == 0 && registers.raster == 0)
    {
        if (!rasterEventLog.empty())
            lastFrameRasterEventLog = rasterEventLog;

        lastFrameRasterEventsByRaster = rasterEventsByRaster;

        // Preserve completed-frame diagnostics before clearing current-frame state.
        lastFrameRasterRowStates = rasterRowStates;
        lastFrameRasterPixelStates = rasterPixelStates;

        rasterEventLog.clear();

        for (auto& events : rasterEventsByRaster)
            events.clear();

        for (auto& s : rasterRowStates)
            s = {};

        for (auto& s : rasterPixelStates)
            s = {};

        rasterColorEvents.clear();
        rasterPriorityEvents.clear();
        rasterSpriteModeEvents.clear();
        rasterSpriteXExpansionEvents.clear();
        rasterSpriteEnableEvents.clear();
        rasterSpriteXEvents.clear();

        firstBadlineY = -1;
        denSeenOn30 = false;

        vicState.vc = 0;
        vicState.vcBase = 0;
        vicState.vmliBase = 0;
        vicState.vmliFetchIndex = 0;
        vicState.rc = 0;

        vicState.refreshCounter = 0xFF;

        vicState.badLineCondition = false;
        vicState.badLineLatchedAt14 = false;
        vicState.cAccessActive = false;
        vicState.badLineDmaStartCycle = -1;
        vicState.badLineFetchIndex = 0;
        vicState.matrixFetchInitializedThisRaster = false;

        vicState.displayEnabled = false;
        vicState.displayEnabledNext = false;
        vicState.displayStateHoldForCycle58 = false;

        vicState.topBorderOpenRaster = 0;
        vicState.bottomBorderCloseRaster = 0;

        clearBadLineFifo();
    }

    if (registers.raster == 0x30 && (registers.control & 0x10) != 0)
        denSeenOn30 = true;
}

void Vic::runCycleDecisionPhase()
{
    const VicCycleSlot& slot = currentCycleSlot;

    if (slot.rasterIrqSample)
        evaluateRasterIRQCompare("normal-compare");

    if (slot.latchRasterState)
        handleCycle0Decisions();

    updateLiveBadLineCondition();

    if (slot.sampleBadline)
        handleCycle14Decisions();

    if (currentCycle == cfg_->spriteYExpansionToggleCycle)
        updateSpriteYExpansionFlipFlops();

    if (currentCycle == cfg_->spriteMcBaseAdvanceCycle1)
        advanceSpriteMCBaseFirstStep();

    if (currentCycle == cfg_->DMAStartCycle)
         handleBadLineFetchStartDecisions();

    if (slot.startSpriteDmaCheck)
        currentCycleSlot.spriteDmaStartMask = updateSpriteDMAStartForCurrentLine(registers.raster);

    if (slot.transferDisplayState)
        handleCycle58Decisions();

    if (slot.startBadlineFetch)
        handleDmaStartCycleDecisions();
}

void Vic::handleCycle0Decisions()
{
    const int raster = registers.raster;

    if (raster == 0)
        vicState.refreshCounter = 0xFF;

    resetCAccessLatch();

    d011_per_raster[raster] = registers.control & 0x7F;
    d016_per_raster[raster] = registers.control2 & 0x1F;
    d018_per_raster[raster] = registers.memory_pointer & 0xFE;

    borderVerticalStart_per_raster[raster] = vicState.verticalBorder ? 1 : 0;

    updateHorizontalBorderState(raster);

    borderVertical_per_raster[raster] = vicState.verticalBorder ? 1 : 0;

    borderLeftOpenX_per_raster[raster] = static_cast<int16_t>(vicState.leftBorderOpenX);

    borderRightCloseX_per_raster[raster] = static_cast<int16_t>(vicState.rightBorderCloseX);

    const uint16_t nextRaster = (registers.raster + 1) % cfg_->maxRasterLines;

    updateMonitorCaches(nextRaster);

    traceVicCycleCheckpoint("cycle-0", raster, currentCycle);
}

void Vic::handleCycle14Decisions()
{
    const int raster = registers.raster;

    const bool badAtCycle14 = isBadLine(raster);

    reloadCharacterSequencerAtCycle14(badAtCycle14);

    vicState.badLineLatchedAt14 = badAtCycle14;

    traceVicCycleCheckpoint("cycle-14", raster, currentCycle);

    if (badAtCycle14)
    {
        const bool firstBadlineThisFrame = (firstBadlineY < 0);

        initializeFirstBadLineIfNeeded(raster);

        if (firstBadlineThisFrame)
            vicState.displayEnabledNext = true;
    }
}

void Vic::handleBadLineFetchStartDecisions()
{
    const int raster = registers.raster;

    if (!vicState.cAccessActive)
        return;

    traceVicBadLineStart(raster, currentCycle, vicState. vcBase, vicState.rc, true);

    beginBadLineFetch();
}

void Vic::handleDmaStartCycleDecisions()
{
    const int raster = registers.raster;

    const uint16_t nextRaster = (raster + 1) % cfg_->maxRasterLines;

    d011_per_raster[nextRaster] = registers.control & 0x7F;
    d016_per_raster[nextRaster] = registers.control2 & 0x1F;
    d018_per_raster[nextRaster] = registers.memory_pointer & 0xFE;
}

void Vic::handleCycle58Decisions()
{
    traceVicCycleCheckpoint("cycle-58", registers.raster, currentCycle);

    if (registers.raster >= 0 && registers.raster < static_cast<int>(rasterRowStates.size()))
        rasterRowStates[registers.raster].displayRc = vicState.rc;

    advanceCharacterSequencerAtCycle58();
}

void Vic::runPhi1Phase()
{
    currentBusPhase = VicBusPhase::Phi1;

    tracePhi1BusCollision();

    const bool gAccessOccurred = performGAccessForCurrentCycle();

    if (gAccessOccurred)
        advanceCharacterSequencerAfterGAccess();

    if (currentCycleSlot.refresh)
        performRefreshFetchForCurrentCycle();

    // Sprite pointer fetch scheduled for Phi1.
    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const auto& timing = cfg_->spriteFetchTiming[sprite];

        if (currentCycle == timing.pointerCycle && timing.pointerPhase == VicBusPhase::Phi1)
        {
            fetchSpritePointer(sprite, registers.raster);
            break;
        }
    }

    // Sprite data fetch scheduled for Phi1.
    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const int byteIndex = spriteDataByteForCyclePhase(sprite, currentCycle, VicBusPhase::Phi1);

        if (byteIndex >= 0)
        {
            performSpriteDataFetchForSprite(sprite, byteIndex);
            break;
        }
    }

    if (currentCycleSlot.phi1BusOwner == BusOwner::Idle)
        performIdleFetchForCurrentCycle();
}

void Vic::runPhi2Phase()
{
    currentBusPhase = VicBusPhase::Phi2;

    tracePhi2BusCollision();

    // Sprite pointer fetch scheduled for Phi2.
    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const auto& timing = cfg_->spriteFetchTiming[sprite];

        if (currentCycle == timing.pointerCycle && timing.pointerPhase == VicBusPhase::Phi2)
        {
            fetchSpritePointer(sprite, registers.raster);
            break;
        }
    }

    // Sprite data fetch scheduled for Phi2.
    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const int byteIndex = spriteDataByteForCyclePhase(sprite, currentCycle, VicBusPhase::Phi2);

        if (byteIndex >= 0 && spriteUnits[sprite].dmaActive)
        {
            performSpriteDataFetchForSprite(sprite, byteIndex);
            break;
        }
    }

    // Bad-line c-access is handled on Phi2.
    performBadLineFetchesForCurrentCycle();

    if (currentCycle == cfg_->spriteMcBaseAdvanceCycle2)
        advanceSpriteMCBaseSecondStep();
}

void Vic::runPixelOutputPhase()
{
    const int raster = registers.raster;

    if (currentCycle == 0)
    {
        clearBackgroundLineBuffers();

        const graphicsMode mode = graphicsModeForRaster(raster);

        if (mode == graphicsMode::standard || mode == graphicsMode::multicolor || mode == graphicsMode::extendedColorText)
        {
            bgColorLine.fill(registers.backgroundColor0 & 0x0F);
            bgOpaqueLine.fill(0);
            bgSourceLine.fill(BackgroundSource::BG0);
        }

        clearSpriteLineBuffers();

        resetActiveBackgroundPixelState();
        resetBackgroundGraphicsLatches();

        prepareSpriteOutputForRaster(raster);
        beginSpriteRasterOutput(raster);
    }

    const int baseX = cycleFramebufferX(currentCycle);

    for (int i = 0; i < 8; ++i)
    {
        const int x = baseX + i;

        if (x < 0 || x >= VISIBLE_WIDTH)
            continue;

        updateVerticalBorderStateAtLeftCompare(raster, x);
        updateHorizontalBorderStateAtPixel(raster, x);

        outputPixel(raster, x);
        outputSpritePixel(raster, x);
    }
}

void Vic::outputPixel(int raster, int x)
{
    if (raster < 0 || raster >= static_cast<int>(rasterPixelStates.size()))
        return;

    if (x < 0 || x >= VISIBLE_WIDTH)
        return;

    if (currentCycleSlot.graphicsFetch)
    {
        const int fetchColumn = currentCycleSlot.graphicsFetchIndex;

        if (fetchColumn >= 0 && fetchColumn < BACKGROUND_MATRIX_COLUMNS)
        {
            const BackgroundGraphicsLatch& latch = backgroundGraphicsLatches[fetchColumn];

            if (latch.valid)
            {
                const int xScroll = static_cast<int>(d016XScroll(latch.d016));
                const int reloadX = cycleFramebufferX(currentCycle) + xScroll;

                if (x == reloadX)
                {
                    if (latch.mode == graphicsMode::bitmap || latch.mode == graphicsMode::multicolorBitmap)
                        loadActiveStandardBitmapPixelStateFromLatch(raster, fetchColumn, x);
                    else
                        loadActiveStandardTextPixelStateFromLatch(raster, fetchColumn, x);
                }
            }
        }
    }

    if (!activeBgPixel.valid)
        return;

    const int expectedX = activeBgPixel.pxBase + activeBgPixel.phase;

    if (x != expectedX)
    {
        resetActiveBackgroundPixelState();
        return;
    }

    const graphicsMode outputMode = activeBgPixel.mode;

    BackgroundPixel pixel {};

    if (outputMode == graphicsMode::multicolorBitmap || outputMode == graphicsMode::illegalMulticolorBitmap)
        pixel = sampleAndAdvanceActiveMulticolorBitmapPixel();
    else if (outputMode == graphicsMode::bitmap || outputMode == graphicsMode::illegalBitmap)
        pixel = sampleAndAdvanceActiveStandardBitmapPixel();
    else if ((outputMode == graphicsMode::multicolor || outputMode == graphicsMode::illegalText) && activeBgPixel.multicolorText)
        pixel = sampleAndAdvanceActiveMulticolorTextPixel();
    else
        pixel = sampleAndAdvanceActiveStandardTextPixel();

    if (outputMode == graphicsMode::illegalText || outputMode == graphicsMode::illegalBitmap ||
        outputMode == graphicsMode::illegalMulticolorBitmap)
        pixel.color = 0x00;

    stampBackgroundPixelSource(x, activeBgPixel.py, pixel.color, pixel.opaque, pixel.source);

    if (activeBgPixel.phase >= 8)
        activeBgPixel.valid = false;
}

void Vic::advanceCycleAndFinalizeLineIfNeeded()
{
    ++currentCycle;

    // End of raster line
    if (currentCycle >= cfg_->cyclesPerLine)
    {
        currentCycle = 0;

        const int curRaster = registers.raster;
        finalizeCurrentRasterLine(curRaster);
    }
}

void Vic::finalizeCurrentRasterLine(int curRaster)
{
    renderLine(curRaster);

    snapshotRasterPixelComposition(curRaster);
    snapshotRasterRowState(curRaster);

    updateSpriteDMAEndOfLine(curRaster);

    finalizeFrameIfNeeded(curRaster);
    advanceToNextRaster();
    traceRasterEnd();
}

void Vic::finalizeFrameIfNeeded(int curRaster)
{
    // End-of-frame check must use the pre-increment raster (curRaster)
    if (curRaster == cfg_->maxRasterLines - 1)
    {
        frameDone = true;

        if (sink)
        {
            const int lastFBY = fbY(curRaster);
            const int fbH = cfg_->visibleLines + 2 * VERTICAL_BORDER_SIZE;

            for (int y = lastFBY + 1; y < fbH; ++y)
            {
                sink->renderBorderLine(y, registers.borderColor, 0, 0);
            }
        }
    }
}

void Vic::advanceToNextRaster()
{
    registers.raster = (registers.raster + 1) % cfg_->maxRasterLines;

    rasterIrqTriggeredThisLine = false;

    // Bad-line/DMA state is local to one raster line.
    vicState.badLineCondition = false;
    vicState.badLineLatchedAt14 = false;
    vicState.cAccessActive = false;
    vicState.badLineDmaStartCycle = -1;
    vicState.badLineFetchIndex = 0;
    vicState.matrixFetchInitializedThisRaster = false;
    vicState.displayStateHoldForCycle58 = false;
}

void Vic::updateBusArbitration()
{
    const bool oldBA = vicState.ba;
    const bool oldAEC = vicState.aec;

    vicState.ba = !currentCycleSlot.baLow;
    vicState.aec = !currentCycleSlot.cpuBusStolen;

    if (cpu)
    {
        cpu->setRDY(vicState.ba);
        cpu->setAEC(vicState.aec);
    }

    if (oldBA != vicState.ba || oldAEC != vicState.aec)
        traceVicBusArb(oldBA, oldAEC, vicState.ba, vicState.aec, vicState.badLineLatchedAt14, currentCycleSlot.baLow,
                       currentCycleSlot.cpuBusStolen);
}

bool Vic::isBadLineCandidateForBusWarning(int raster) const
{
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return false;

    if (!denSeenOn30)
        return false;

    const uint8_t d011 = effectiveD011ForRaster(raster);
    const int yScroll = d011 & 0x07;

    if (raster < 0x30 || raster > 0xF7)
        return false;

    return (raster & 0x07) == yScroll;
}

bool Vic::isBadLineBusWarningCycle(int raster, int cycle) const
{
    if (!isBadLineCandidateForBusWarning(raster))
        return false;

    if (vicState.badLineDmaStartCycle < 0)
        return false;

    const int lineCycles = cfg_->cyclesPerLine;
    const int slot = vicState.badLineDmaStartCycle;

    const int warn0 = (slot - 3 + lineCycles) % lineCycles;
    const int warn1 = (slot - 2 + lineCycles) % lineCycles;
    const int warn2 = (slot - 1 + lineCycles) % lineCycles;

    return cycle == warn0 || cycle == warn1 || cycle == warn2;
}

bool Vic::isBadLineBusStealCycle(int raster, int cycle) const
{
    if (raster != registers.raster)
        return false;

    if (!vicState.cAccessActive)
        return false;

    if (vicState.badLineDmaStartCycle < 0)
        return false;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    return cycle >= vicState.badLineDmaStartCycle && cycle <= cfg_->DMAEndCycle;
}

bool Vic::isBadLineBAHoldCycle(int raster, int cycle) const
{
    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    return isBadLineBusWarningCycle(raster, cycle) || isBadLineBusStealCycle(raster, cycle);
}

bool Vic::isRefreshCycle(int cycle) const
{
    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    const int c0 = cfg_->refreshStartCycle;
    const int c1 = (c0 + 1) % cfg_->cyclesPerLine;
    const int c2 = (c0 + 2) % cfg_->cyclesPerLine;
    const int c3 = (c0 + 3) % cfg_->cyclesPerLine;
    const int c4 = (c0 + 4) % cfg_->cyclesPerLine;

    return cycle == c0 ||
           cycle == c1 ||
           cycle == c2 ||
           cycle == c3 ||
           cycle == c4;
}

void Vic::performRefreshFetchForCurrentCycle()
{
    if (!bus)
        return;

    // Refresh addresses are $3F00-$3FFF within the current
    // 16 KB VIC bank. The low byte comes from REF.
    const uint16_t address = static_cast<uint16_t>(0x3F00 | vicState.refreshCounter);

    const uint8_t value = bus->vicRead(address);

    updateOpenBus(value);

    // REF decrements after the refresh access.
    --vicState.refreshCounter;
}

bool Vic::isSpriteBusWarningCycle(int raster, int cycle) const
{
    (void)raster;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    const int lineCycles = cfg_->cyclesPerLine;

    for (int s = 0; s < 8; ++s)
    {
        if (!spriteUnits[s].dmaActive)
            continue;

        const int firstCpuStealCycle = firstSpriteCpuStealCycle(s);

        if (firstCpuStealCycle < 0)
            continue;

        const int warn0 = (firstCpuStealCycle - 3 + lineCycles) % lineCycles;
        const int warn1 = (firstCpuStealCycle - 2 + lineCycles) % lineCycles;
        const int warn2 = (firstCpuStealCycle - 1 + lineCycles) % lineCycles;

        if (cycle == warn0 || cycle == warn1 || cycle == warn2)
            return true;
    }

    return false;
}

bool Vic::isSpriteBusStealCycle(int raster, int cycle) const
{
    (void)raster;

    for (int s = 0; s < 8; ++s)
    {
        if (!spriteUnits[s].dmaActive)
            continue;

        // Pointer fetches are tracked as fetch events, but they should not
        // be modeled as full CPU-steal cycles.
        if (cycle == cfg_->spriteFetchTiming[s].pointerCycle)
            continue;

        if (isSpriteDataCpuStealCycle(s, cycle))
            return true;
    }

    return false;
}

bool Vic::isSpriteBusAECStealCycle(int raster, int cycle) const
{
    (void)raster;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    for (int sprite = 0; sprite < 8; ++sprite)
    {
        if (!spriteUnits[sprite].dmaActive)
            continue;

        if (isSpriteDataCpuStealCycle(sprite, cycle))
            return true;
    }

    return false;
}

bool Vic::isSpriteDataCpuStealCycle(int sprite, int cycle) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    if (!spriteUnits[sprite].dmaActive)
        return false;

    const SpriteFetchPhase phi2Phase = spriteFetchPhaseForCycle(sprite, cycle, VicBusPhase::Phi2);

    return spriteFetchPhaseStealsCpu(phi2Phase);
}

bool Vic::isSpriteBusBAHoldCycle(int raster, int cycle) const
{
    (void)raster;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    for (int sprite = 0; sprite < 8; ++sprite)
    {
        if (!spriteUnits[sprite].dmaActive)
            continue;

        if (isSpriteDataCpuStealCycle(sprite, cycle))
            return true;
    }

    return false;
}

Vic::VicCycleSlot Vic::cycleSlotFor(int raster, int cycle) const
{
    VicCycleSlot slot {};

    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return slot;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return slot;

    slot.fetchKind = getFetchKindForCycle(raster, cycle);

    const int graphicsFetchStartCycle = cfg_->bgFetchStartCycle + 1;
    const int graphicsFetchEndCycle = graphicsFetchStartCycle + BACKGROUND_MATRIX_COLUMNS - 1;

    if (cycle >= graphicsFetchStartCycle && cycle <= graphicsFetchEndCycle)
    {
        const int index = cycle - graphicsFetchStartCycle;

        if (index >= 0 && index < BACKGROUND_MATRIX_COLUMNS)
        {
            slot.graphicsFetch = true;
            slot.graphicsFetchIndex = index;
        }
    }

    switch (slot.fetchKind)
    {
        case FetchKind::SpritePtr0:
        case FetchKind::SpritePtr1:
        case FetchKind::SpritePtr2:
        case FetchKind::SpritePtr3:
        case FetchKind::SpritePtr4:
        case FetchKind::SpritePtr5:
        case FetchKind::SpritePtr6:
        case FetchKind::SpritePtr7:
        {
            slot.spriteIndex = spritePointerFetchSpriteForKind(slot.fetchKind);
            break;
        }

        case FetchKind::SpriteData0:
        case FetchKind::SpriteData1:
        case FetchKind::SpriteData2:
        case FetchKind::SpriteData3:
        case FetchKind::SpriteData4:
        case FetchKind::SpriteData5:
        case FetchKind::SpriteData6:
        case FetchKind::SpriteData7:
        {
            slot.spriteIndex = spriteDataFetchSpriteForKind(slot.fetchKind);

            if (slot.spriteIndex >= 0)
            {
                int byteIndex = spriteDataByteForCyclePhase(slot.spriteIndex, cycle, VicBusPhase::Phi1);

                if (byteIndex < 0)
                    byteIndex = spriteDataByteForCyclePhase(slot.spriteIndex, cycle, VicBusPhase::Phi2);

                slot.spriteByteIndex = byteIndex;
            }

            break;
        }

        case FetchKind::None:
        default:
            break;
    }

    if (slot.spriteIndex >= 0)
    {
        SpriteFetchPhase expectedPhase = SpriteFetchPhase::None;

        switch (slot.fetchKind)
        {
            case FetchKind::SpritePtr0:
            case FetchKind::SpritePtr1:
            case FetchKind::SpritePtr2:
            case FetchKind::SpritePtr3:
            case FetchKind::SpritePtr4:
            case FetchKind::SpritePtr5:
            case FetchKind::SpritePtr6:
            case FetchKind::SpritePtr7:
                expectedPhase = SpriteFetchPhase::Pointer;
                break;

            case FetchKind::SpriteData0:
            case FetchKind::SpriteData1:
            case FetchKind::SpriteData2:
            case FetchKind::SpriteData3:
            case FetchKind::SpriteData4:
            case FetchKind::SpriteData5:
            case FetchKind::SpriteData6:
            case FetchKind::SpriteData7:
            {
                switch (slot.spriteByteIndex)
                {
                    case 0:
                        expectedPhase = SpriteFetchPhase::Data0;
                        break;

                    case 1:
                        expectedPhase = SpriteFetchPhase::Data1;
                        break;

                    case 2:
                        expectedPhase = SpriteFetchPhase::Data2;
                        break;

                    default:
                        break;
                }

                break;
            }

            default:
                break;
        }

        if (expectedPhase != SpriteFetchPhase::None)
        {
            const VicBusPhase busPhase = spriteBusPhaseForFetch(slot.spriteIndex, expectedPhase);

            slot.spriteFetchPhase = spriteFetchPhaseForCycle(slot.spriteIndex, cycle, busPhase);

            if (slot.spriteFetchPhase != SpriteFetchPhase::None)
            {
                slot.spriteBusPhase = busPhase;
                slot.spriteBusPhaseValid = true;
            }
        }
    }

    slot.badlineWarning = isBadLineBusWarningCycle(raster, cycle);
    slot.badlineSteal = isBadLineBusStealCycle(raster, cycle);
    slot.badlineBAHold = isBadLineBAHoldCycle(raster, cycle);
    slot.spriteWarning = isSpriteBusWarningCycle(raster, cycle);
    slot.spriteBAHold = isSpriteBusBAHoldCycle(raster, cycle);
    slot.spriteAECSteal = isSpriteBusAECStealCycle(raster, cycle);
    slot.refresh = isRefreshCycle(cycle);

    //
    // Phi1 bus ownership
    //

    slot.phi1BusOwner = BusOwner::Idle;

    if (slot.graphicsFetch)
        slot.phi1BusOwner = BusOwner::Graphics;

    if (slot.refresh)
        slot.phi1BusOwner = BusOwner::Refresh;

    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const auto& timing = cfg_->spriteFetchTiming[sprite];

        if (cycle == timing.pointerCycle && timing.pointerPhase == VicBusPhase::Phi1)
        {
            slot.phi1BusOwner = BusOwner::SpritePointer;
            break;
        }

        if (spriteUnits[sprite].dmaActive)
        {
            const int byteIndex = spriteDataByteForCyclePhase(sprite, cycle, VicBusPhase::Phi1);

            if (byteIndex >= 0)
            {
                slot.phi1BusOwner = BusOwner::SpriteData;
                break;
            }
        }
    }

    //
    // BA / AEC state
    //

    slot.baLow = slot.badlineBAHold || slot.spriteWarning || slot.spriteBAHold;
    slot.cpuBusStolen = slot.badlineSteal || slot.spriteAECSteal;
    slot.aecLow = slot.cpuBusStolen;
    slot.rasterIrqSample = isRasterIRQCompareCycle(cycle);
    slot.latchRasterState = cycle == 0;
    slot.sampleBadline = cycle == 14;
    slot.startSpriteDmaCheck = cycle == cfg_->spriteDmaCheckCycle1 || cycle == cfg_->spriteDmaCheckCycle2;
    slot.transferDisplayState = cycle == 58;
    slot.startBadlineFetch = cycle == cfg_->DMAStartCycle;

    //
    // Bad-line c-access state
    //

    const bool cAccessForThisRaster = (raster == registers.raster) ? vicState.cAccessActive : isBadLine(raster);

    if (cAccessForThisRaster && cycle >= cfg_->bgFetchStartCycle && cycle <= cfg_->bgFetchEndCycle)
    {
        const int index = cycle - cfg_->bgFetchStartCycle;

        slot.matrixFetchIndex = index >= 0 && index < BACKGROUND_MATRIX_COLUMNS ? index : -1;
    }

    //
    // Phi2 bus ownership
    //

    if (cAccessForThisRaster && slot.matrixFetchIndex >= 0)
        slot.phi2BusOwner = BusOwner::BadLine;

    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const auto& timing = cfg_->spriteFetchTiming[sprite];

        if (cycle == timing.pointerCycle && timing.pointerPhase == VicBusPhase::Phi2)
        {
            slot.phi2BusOwner = BusOwner::SpritePointer;
            break;
        }

        if (spriteUnits[sprite].dmaActive)
        {
            const int byteIndex = spriteDataByteForCyclePhase(sprite, cycle, VicBusPhase::Phi2);

            if (byteIndex >= 0)
            {
                slot.phi2BusOwner = BusOwner::SpriteData;
                break;
            }
        }
    }

    return slot;
}

void Vic::renderLine(int raster)
{
    if (!sink || !bus)
        return;

    updateGraphicsMode(raster);

    applyBackgroundColorEventsToLine(raster);
    applyExtendedBackgroundColorEventsToLine(raster);
    applySpriteColorEventsToLine(raster);

    composeFinalRasterLine(raster);
    applyBorderColorEventsToFinalLine(raster);
    emitRasterLineInOrder(raster);
}

void Vic::recordRasterColorWrite(uint16_t address, uint8_t oldValue, uint8_t newValue)
{
    if (!(address >= 0xD020 && address <= 0xD02E))
        return;

    RasterColorEvent e;
    e.raster = registers.raster;
    e.cycle = currentCycle;
    e.phase = VicBusPhase::Phi2;

    e.address = address;
    e.oldValue = oldValue & 0x0F;
    e.newValue = newValue & 0x0F;

    rasterColorEvents.push_back(e);

    recordRasterEventLog(RasterEventKind::Color, address, e.oldValue, e.newValue);
}

void Vic::recordRasterEventLog(RasterEventKind kind, uint16_t address, uint8_t oldValue, uint8_t newValue)
{
    if (oldValue == newValue)
        return;

    RasterEventRecord e;
    e.kind = kind;
    e.raster = registers.raster;
    e.cycle = currentCycle;
    e.phase = currentBusPhase;
    e.address = address;
    e.oldValue = oldValue;
    e.newValue = newValue;

    rasterEventLog.push_back(e);

    if (e.raster >= 0 && e.raster < static_cast<int>(rasterEventsByRaster.size()))
        rasterEventsByRaster[e.raster].push_back(e);
}

void Vic::snapshotRasterPixelComposition(int raster)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    RasterPixelCompositionSnapshot& s = rasterPixelStates[raster];

    s.valid = true;
    s.raster = raster;

    for (int x = 0; x < VISIBLE_WIDTH; ++x)
    {
        s.bgColor[x] = bgColorLine[x] & 0x0F;
        s.bgOpaque[x] = bgOpaqueLine[x] ? 1 : 0;
        s.bgSource[x] = static_cast<uint8_t>(bgSourceLine[x]);
        s.borderMask[x] = borderMaskLine[x] ? 1 : 0;
        s.finalColor[x] = finalColorLine[x] & 0x0F;

        uint8_t mask = 0;
        for (int spr = 0; spr < 8; ++spr)
        {
            if (spriteOpaqueLine[spr][x])
                mask |= static_cast<uint8_t>(1u << spr);
        }

        s.spriteMask[x] = mask;
    }
}

void Vic::snapshotRasterRowState(int raster)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    RasterRowStateSnapshot& s = rasterRowStates[raster];

    s.valid = true;
    s.raster = raster;
    s.firstBadlineY = firstBadlineY;

    s.rc = vicState.rc;
    s.vcBase = vicState.vcBase;
    s.vmliBase = vicState.vmliBase;
    s.vmliFetchIndex = vicState.vmliFetchIndex;

    s.displayEnabled = vicState.displayEnabled;
    s.displayEnabledNext = vicState.displayEnabledNext;
    s.badLine = vicState.badLineCondition;
    s.badLineSampled = vicState.badLineLatchedAt14;

    s.d011 = latchedD011ForRaster(raster);
    s.d016 = latchedD016ForRaster(raster);
    s.d018 = latchedD018ForRaster(raster);
}

void Vic::emitRasterLineInOrder(int raster)
{
    if (!sink)
        return;

    const int screenY = fbY(raster);

    const int xStart = rasterVisibleStartX(raster);
    const int xEnd   = rasterVisibleEndX(raster);

    for (int px = xStart; px < xEnd; ++px)
    {
        sink->setPixel(px, screenY, finalColorLine[px] & 0x0F);
    }
}

int Vic::rasterVisibleStartX(int raster) const
{
    (void)raster;
    return 0;
}

int Vic::rasterVisibleEndX(int raster) const
{
    (void)raster;
    return VISIBLE_WIDTH;
}

bool Vic::isInnerDisplayPixel(int raster, int px) const
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return false;

    if (px < 0 || px >= VISIBLE_WIDTH)
        return false;

    return borderMaskLine[px] == 0;
}

void Vic::composeFinalRasterLine(int raster)
{
    const int xStart = rasterVisibleStartX(raster);
    const int xEnd   = rasterVisibleEndX(raster);

    for (int px = xStart; px < xEnd; ++px)
        finalColorLine[px] = compositePixelAtX(raster, px);
}

uint8_t Vic::compositePixelAtX(int raster, int px) const
{
    // VIC-II border is in front of both background graphics and sprites.
    if (borderActiveAtPixel(raster, px))
        return registers.borderColor & 0x0F;

    const BackgroundPixel bg = sampleBackgroundPixelAtX(raster, px);

    // Lower-numbered sprites have priority over higher-numbered sprites.
    for (int spr = 0; spr < 8; ++spr)
    {
        if (!spriteOpaqueLine[spr][px])
            continue;

        const bool behind = spriteBehindBackgroundAtPixel(spr, px);

        // This sprite wins sprite-to-sprite priority, but foreground
        // graphics may still cover it according to D01B.
        if (behind && bg.opaque)
            return bg.color & 0x0F;

        return spriteColorLine[spr][px] & 0x0F;
    }

    return bg.color & 0x0F;
}

int Vic::rasterPixelToCycle(int px) const
{
    if (px < 0)
        return -1;

    const int cycle = px / 8;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return -1;

    return cycle;
}

int Vic::rasterEventPixelX(int cycle) const
{
    int x = cfg_->hardware_X + (cycle * 8);

    if (x < 0)
        x = 0;

    if (x > VISIBLE_WIDTH)
        x = VISIBLE_WIDTH;

    return x;
}

int Vic::rasterRegisterEventPixelX(const RasterEventRecord& e) const
{
    int x = cfg_->hardware_X + (e.cycle * 8);

    if (e.phase == VicBusPhase::Phi2)
        x += 4;

    if (x < 0)
        x = 0;

    if (x > VISIBLE_WIDTH)
        x = VISIBLE_WIDTH;

    return x;
}

int Vic::rasterColorEventPixelX(const RasterColorEvent& e) const
{
    int x = cfg_->hardware_X + (e.cycle * 8);

    if (e.phase == VicBusPhase::Phi2)
        x += 4;

    if (x < 0)
        x = 0;

    if (x > VISIBLE_WIDTH)
        x = VISIBLE_WIDTH;

    return x;
}

bool Vic::firstRasterColorEventValue(int raster, uint16_t address, uint8_t& value) const
{
    for (const RasterColorEvent& e : rasterColorEvents)
    {
        if (e.raster != raster)
            continue;

        if (e.address != address)
            continue;

        value = e.oldValue & 0x0F;
        return true;
    }

    return false;
}

void Vic::applyBorderColorEventsToFinalLine(int raster)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    uint8_t activeBorderColor = 0;
    if (!firstRasterColorEventValue(raster, 0xD020, activeBorderColor))
        return; // no D020 event on this raster; normal composition is already correct

    int startX = rasterVisibleStartX(raster);
    const int endX = rasterVisibleEndX(raster);

    for (const RasterColorEvent& e : rasterColorEvents)
    {
        if (e.raster != raster)
            continue;

        if (e.address != 0xD020)
            continue;

        const int eventX = std::clamp(rasterColorEventPixelX(e), startX, endX);

        for (int px = startX; px < eventX; ++px)
        {
            if (borderMaskLine[px])
                finalColorLine[px] = activeBorderColor;
        }

        activeBorderColor = e.newValue & 0x0F;
        startX = eventX;
    }

    for (int px = startX; px < endX; ++px)
    {
        if (borderMaskLine[px])
            finalColorLine[px] = activeBorderColor;
    }
}

void Vic::applyExtendedBackgroundColorEventsToLine(int raster)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    const int endX = rasterVisibleEndX(raster);

    auto replayForRegister = [&](uint16_t address, BackgroundSource source)
    {
        uint8_t activeColor = 0;
        if (!firstRasterColorEventValue(raster, address, activeColor))
            return;

        int startX = rasterVisibleStartX(raster);

        for (const RasterColorEvent& e : rasterColorEvents)
        {
            if (e.raster != raster)
                continue;

            if (e.address != address)
                continue;

            const int eventX = std::clamp(rasterColorEventPixelX(e), startX, endX);

            for (int px = startX; px < eventX; ++px)
            {
                if (!isInnerDisplayPixel(raster, px))
                    continue;

                if (bgSourceLine[px] == source)
                    bgColorLine[px] = activeColor & 0x0F;
            }

            activeColor = e.newValue & 0x0F;
            startX = eventX;
        }

        for (int px = startX; px < endX; ++px)
        {
            if (!isInnerDisplayPixel(raster, px))
                continue;

            if (bgSourceLine[px] == source)
                bgColorLine[px] = activeColor & 0x0F;
        }
    };

    replayForRegister(0xD022, BackgroundSource::BG1);
    replayForRegister(0xD023, BackgroundSource::BG2);
    replayForRegister(0xD024, BackgroundSource::BG3);
}

void Vic::applyBackgroundColorEventsToLine(int raster)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    uint8_t activeBg0 = 0;
    if (!firstRasterColorEventValue(raster, 0xD021, activeBg0))
        return; // no D021 event on this raster; normal background generation is already correct

    int startX = rasterVisibleStartX(raster);
    const int endX = rasterVisibleEndX(raster);

    for (const RasterColorEvent& e : rasterColorEvents)
    {
        if (e.raster != raster)
            continue;

        if (e.address != 0xD021)
            continue;

        const int eventX = std::clamp(rasterColorEventPixelX(e), startX, endX);

        for (int px = startX; px < eventX; ++px)
        {
            if (!isInnerDisplayPixel(raster, px))
                continue;

            if (bgSourceLine[px] == BackgroundSource::BG0)
                bgColorLine[px] = activeBg0;
        }

        activeBg0 = e.newValue & 0x0F;
        startX = eventX;
    }

    for (int px = startX; px < endX; ++px)
    {
        if (!isInnerDisplayPixel(raster, px))
            continue;

        if (bgSourceLine[px] == BackgroundSource::BG0)
            bgColorLine[px] = activeBg0;
    }
}

uint8_t Vic::latchOpenBus(uint8_t value)
{
    const uint64_t cycle = cpu ? cpu->getTotalCycles() : 0;

    if (dataBus)
        dataBus->drive(value, DataBusLatch::Driver::VIC, cycle);

    return value;
}

uint8_t Vic::getOpenBus() const
{
    const uint64_t cycle = cpu ? cpu->getTotalCycles() : 0;
    return dataBus ? dataBus->sample(cycle) : 0xFF;
}

uint8_t Vic::latchOpenBusMasked(uint8_t definedBits, uint8_t definedMask)
{
     const uint64_t cycle = cpu ? cpu->getTotalCycles() : 0;

    if (!dataBus)
        return static_cast<uint8_t>(definedBits & definedMask);

    dataBus->drive(definedBits, definedMask, DataBusLatch::Driver::VIC, cycle);

    return dataBus->sample(cycle);
}

void Vic::updateOpenBus(uint8_t value)
{
    const uint64_t cycle = cpu ? cpu->getTotalCycles() : 0;

    if (dataBus)
        dataBus->drive(value, DataBusLatch::Driver::VIC, cycle);
}

void Vic::performIdleFetchForCurrentCycle()
{
    if (!bus)
        return;

    const uint16_t addr = idleFetchAddressForCurrentCycle();
    const uint8_t value = bus->vicRead(addr);

    updateOpenBus(value);
}

std::string Vic::decodeModeName() const
{
    const uint8_t d011 = effectiveD011ForRaster(registers.raster);
    const uint8_t d016 = effectiveD016ForRaster(registers.raster);

    const bool ecm = (d011 & 0x40) != 0;
    const bool bmm = (d011 & 0x20) != 0;
    const bool mcm = (d016 & 0x10) != 0;

    if (!ecm && !bmm && !mcm) return "Text";
    if (!ecm && !bmm &&  mcm) return "Multicolor Text";
    if (!ecm &&  bmm && !mcm) return "Bitmap";
    if (!ecm &&  bmm &&  mcm) return "Multicolor Bitmap";
    if ( ecm && !bmm && !mcm) return "ECM (Extended Color Mode)";
    if ( ecm && !bmm &&  mcm) return "Illegal Text";
    if ( ecm &&  bmm && !mcm) return "Illegal Bitmap";
    return "Illegal Multicolor Bitmap";
}

std::string Vic::getVICBanks() const
{
    std::stringstream out;
    out << std::hex << std::uppercase << std::setfill('0');

    const int raster = std::clamp<int>(
        static_cast<int>(registers.raster),
        0,
        static_cast<int>(cfg_->maxRasterLines - 1)
    );

    const uint16_t bankBase = cia2 ? cia2->getCurrentVICBank() : 0;

    // Representative display X for monitor reporting.
    // Actual rendering remains pixel-aware across the whole raster.
    const int samplePx = BACKGROUND_40COL_X0;

    const uint16_t charOffset =
        charBaseForRasterPixelX(raster, samplePx);

    const uint16_t screenOffset =
        screenBaseForRasterPixelX(raster, samplePx);

    const uint16_t bitmapOffset =
        bitmapBaseForRasterPixelX(raster, samplePx);

    out << "Active VIC Bank = " << (bankBase >> 14)
        << " ($" << std::setw(4) << bankBase
        << "-$" << std::setw(4) << static_cast<uint16_t>(bankBase + 0x3FFF)
        << ")\n\n";

    out << "CHAR Base   = offset $" << std::setw(4) << charOffset
        << "  ->  address $" << std::setw(4)
        << static_cast<uint16_t>(bankBase + charOffset) << "\n";

    out << "Screen Base = offset $" << std::setw(4) << screenOffset
        << "  ->  address $" << std::setw(4)
        << static_cast<uint16_t>(bankBase + screenOffset) << "\n";

    out << "Bitmap Base = offset $" << std::setw(4) << bitmapOffset
        << "  ->  address $" << std::setw(4)
        << static_cast<uint16_t>(bankBase + bitmapOffset) << "\n";

    return out.str();
}

void Vic::updateMonitorCaches(int raster)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        raster = registers.raster;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        raster = 0;

    const uint16_t currentVICBank = cia2 ? cia2->getCurrentVICBank() : 0;

    // Use a representative visible display X for monitor/debug cache reporting.
    // Rendering itself remains pixel-aware through charBaseForRasterPixelX(),
    // screenBaseForRasterPixelX(), and bitmapBaseForRasterPixelX().
    const int samplePx = BACKGROUND_40COL_X0;

    charBaseCache = static_cast<uint16_t>(charBaseForRasterPixelX(raster, samplePx) + currentVICBank);

    screenBaseCache = static_cast<uint16_t>(screenBaseForRasterPixelX(raster, samplePx) + currentVICBank);

    bitmapBaseCache = static_cast<uint16_t>(bitmapBaseForRasterPixelX(raster, samplePx) + currentVICBank);
}

Vic::FetchKind Vic::getFetchKindForCycle(int raster, int cycle) const
{
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return FetchKind::None;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return FetchKind::None;

    for (int s = 0; s < 8; ++s)
    {
        const auto& timing = cfg_->spriteFetchTiming[s];

        if (cycle == timing.pointerCycle)
        {
            switch (s)
            {
                case 0: return FetchKind::SpritePtr0;
                case 1: return FetchKind::SpritePtr1;
                case 2: return FetchKind::SpritePtr2;
                case 3: return FetchKind::SpritePtr3;
                case 4: return FetchKind::SpritePtr4;
                case 5: return FetchKind::SpritePtr5;
                case 6: return FetchKind::SpritePtr6;
                case 7: return FetchKind::SpritePtr7;
            }
        }

        if (spriteUnits[s].dmaActive && isSpriteDMAFetchCycle(s, cycle))
        {
            switch (s)
            {
                case 0: return FetchKind::SpriteData0;
                case 1: return FetchKind::SpriteData1;
                case 2: return FetchKind::SpriteData2;
                case 3: return FetchKind::SpriteData3;
                case 4: return FetchKind::SpriteData4;
                case 5: return FetchKind::SpriteData5;
                case 6: return FetchKind::SpriteData6;
                case 7: return FetchKind::SpriteData7;
            }
        }
    }

    return FetchKind::None;
}

std::string Vic::dumpRasterPixelCompositionDebug(int raster, int x0, int x1) const
{
    std::ostringstream out;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
    {
        out << "Raster " << raster << " is out of range\n";
        return out.str();
    }

    if (x0 > x1)
        std::swap(x0, x1);

    x0 = std::clamp(x0, 0, VISIBLE_WIDTH - 1);
    x1 = std::clamp(x1, 0, VISIBLE_WIDTH - 1);

    const RasterPixelCompositionSnapshot* snap = nullptr;
    const char* snapSource = "none";

    if (raster < static_cast<int>(rasterPixelStates.size()) &&  rasterPixelStates[raster].valid)
    {
        snap = &rasterPixelStates[raster];
        snapSource = "current frame";
    }
    else if (raster < static_cast<int>(lastFrameRasterPixelStates.size()) && lastFrameRasterPixelStates[raster].valid)
    {
        snap = &lastFrameRasterPixelStates[raster];
        snapSource = "previous frame";
    }

    if (!snap)
    {
        out << "No pixel composition snapshot available for raster "
            << raster << "\n";
        return out.str();
    }

    const int py = fbY(raster);

    out << "Raster Pixel Composition Debug\n";
    out << "------------------------------\n";
    out << "snapshot: " << snapSource << "\n";
    out << "raster: " << raster << "\n";
    out << "fbY: " << py << "\n";
    out << "x range: " << x0 << " - " << x1 << "\n";
    out << "\n";

    out << "  x    bgOpq bgCol bgSrc border final sprMask flags\n";
    out << "  --------------------------------------------------\n";

    for (int x = x0; x <= x1; ++x)
    {
        const uint8_t bgOpq    = snap->bgOpaque[x] ? 1 : 0;
        const uint8_t bgCol    = snap->bgColor[x] & 0x0F;
        const uint8_t bgSrc    = snap->bgSource[x];
        const uint8_t border   = snap->borderMask[x] ? 1 : 0;
        const uint8_t finalCol = snap->finalColor[x] & 0x0F;
        const uint8_t sprMask  = snap->spriteMask[x];

        out << "  "
            << std::dec << std::setw(3) << x
            << "     "
            << std::setw(1) << static_cast<int>(bgOpq)
            << "    $"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(bgCol)
            << std::dec << std::setfill(' ')
            << "   "
            << std::setw(5) << static_cast<int>(bgSrc)
            << "      "
            << std::setw(1) << static_cast<int>(border)
            << "    $"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(finalCol)
            << "   $"
            << std::setw(2) << static_cast<int>(sprMask)
            << std::dec << std::setfill(' ')
            << "   ";

        bool wroteFlag = false;

        if (border)
        {
            out << "BORDER";
            wroteFlag = true;
        }

        if (sprMask != 0)
        {
            if (wroteFlag)
                out << ",";
            out << "SPR";
            wroteFlag = true;
        }

        if (!bgOpq)
        {
            if (wroteFlag)
                out << ",";
            out << "BG-TRANSPARENT";
            wroteFlag = true;
        }

        if (sprMask == 0 && !border && finalCol != bgCol)
        {
            if (wroteFlag)
                out << ",";
            out << "FINAL!=BG";
            wroteFlag = true;
        }

        if (!wroteFlag)
            out << "-";

        out << "\n";
    }

    out << std::dec << std::nouppercase << std::setfill(' ');

    return out.str();
}

std::string Vic::dumpBusArbitrationLine(int raster) const
{
    std::ostringstream out;

    if (raster < 0 || raster >= cfg_->maxRasterLines)
    {
        out << "Invalid raster\n";
        return out.str();
    }

    out << "VIC bus arbitration - raster " << raster << "\n";
    out << "Cy BA AEC BW BS BH SW SH SS Fetch\n";
    out << "----------------------------------\n";

    for (int cycle = 0; cycle < cfg_->cyclesPerLine; ++cycle)
    {
        const VicCycleSlot slot = cycleSlotFor(raster, cycle);

        out << std::setw(2) << cycle << " "
            << (slot.baLow ? 'L' : 'H') << "  "
            << (slot.aecLow ? 'L' : 'H') << "   "
            << (slot.badlineWarning ? '1' : '0') << "  "
            << (slot.badlineSteal ? '1' : '0') << "  "
            << (slot.badlineBAHold ? '1' : '0') << "  "
            << (slot.spriteWarning ? '1' : '0') << "  "
            << (slot.spriteBAHold ? '1' : '0') << "  "
            << (slot.spriteAECSteal ? '1' : '0') << "  "
            << static_cast<int>(slot.fetchKind)
            << "\n";
    }

    return out.str();
}

int Vic::cyclePixelX(int cycle) const
{
    if (cycle < 0)
        cycle = 0;

    if (cycle >= cfg_->cyclesPerLine)
        cycle %= cfg_->cyclesPerLine;

    const int rasterWidth = cfg_->cyclesPerLine * 8;

    int hardwareX = cfg_->hardware_X + cycle * 8;
    hardwareX %= rasterWidth;

    int framebufferX = hardwareX - cfg_->hardware_X + HORIZONTAL_BORDER_SIZE;
    framebufferX %= rasterWidth;

    if (framebufferX < 0)
        framebufferX += rasterWidth;

    return framebufferX;
}

int Vic::cycleFramebufferX(int cycle) const
{
    const int graphicsFirstCycle = cfg_->bgFetchStartCycle + 1;

    return BACKGROUND_40COL_X0 + ((cycle - graphicsFirstCycle) * 8);
}

const char* Vic::busArbReason(int raster, int cycle) const
{
    if (isBadLineBusStealCycle(raster, cycle))
        return "badline-steal";

    if (isBadLineBusWarningCycle(raster, cycle))
        return "badline-warn";

    if (isSpriteBusStealCycle(raster, cycle))
        return "sprite-steal";

    if (isSpriteBusWarningCycle(raster, cycle))
        return "sprite-warn";

    if (isRefreshCycle(cycle))
        return "refresh";

    return "none";
}

const char* Vic::busPhaseName(VicBusPhase phase) const
{
    switch (phase)
    {
        case VicBusPhase::Phi1:
            return "Phi1";

        case VicBusPhase::Phi2:
            return "Phi2";

        default:
            return "?";
    }
}

uint16_t Vic::idleFetchAddressForCurrentCycle() const
{
    const int sampleX = rasterEventPixelX(currentCycle);

    const uint8_t d011 = d011ForRasterPixelX(registers.raster, sampleX, false);

    // VIC-II idle g-access:
    //   ECM=0 -> $3FFF
    //   ECM=1 -> A9/A10 forced low -> $39FF
    return (d011 & 0x40) ? 0x39FF : 0x3FFF;
}
