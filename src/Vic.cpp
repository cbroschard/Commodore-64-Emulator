// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "IVideoSink.h"
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
