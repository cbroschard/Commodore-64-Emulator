// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CIA2.h"
#include "CPU.h"
#include "DataBusLatch.h"
#include "IVideoSink.h"
#include "IRQLine.h"
#include "Memory.h"
#include "Vic.h"

Vic::Vic(VideoMode mode) :
    cia2(nullptr),
    cpu(nullptr),
    dataBus(nullptr),
    sink(nullptr),
    IRQ(nullptr),
    mem(nullptr),
    traceMgr(nullptr),
    mode_(mode),
    cfg_(mode == VideoMode::NTSC ? &NTSC_CONFIG : &PAL_CONFIG)
{
    d011_per_raster.resize(cfg_->maxRasterLines);
    d016_per_raster.resize(cfg_->maxRasterLines);
    d018_per_raster.resize(cfg_->maxRasterLines);
    dd00_per_raster.resize(cfg_->maxRasterLines);

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
    rasterIrqSampledThisLine = false;
    lastRasterIRQSample = {};

    // Internal VIC state
    vicState.vc = 0;
    vicState.vcBase = 0;
    vicState.vmliBase = 0;
    vicState.vmliFetchIndex = 0;
    vicState.rc = 0;

    vicState.matrixAdvancePending = false;

    vicState.displayEnabled = false;
    vicState.displayEnabledNext = false;
    vicState.badLine = false;
    vicState.badLineSampled = false;
    vicState.badLineDmaStartCycle = -1;
    vicState.badLineFetchIndex = 0;
    vicState.badLineInitializedThisRaster = false;

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

    for (auto& s : spriteUnits)
    {
        s.dmaActive = false;
        s.yExpandLatch = false;

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

        s.fetched0 = 0;
        s.fetched1 = 0;
        s.fetched2 = 0;
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

    // Background pipeline
    resetBackgroundPipeline();

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

    // Fill in DD00
    uint16_t currentVICBank = cia2 ? cia2->getCurrentVICBank() : 0;
    std::fill(std::begin(dd00_per_raster), std::end(dd00_per_raster), currentVICBank);

    // Initialize bgOpaque
    bgOpaque.resize(cfg_->visibleLines + 2*BORDER_SIZE);
    for (auto &row : bgOpaque) row.fill(0);

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
    dd00_per_raster.resize(cfg_->maxRasterLines);

    borderVertical_per_raster.resize(cfg_->maxRasterLines);
    borderVerticalStart_per_raster.resize(cfg_->maxRasterLines);
    borderLeftOpenX_per_raster.resize(cfg_->maxRasterLines);
    borderRightCloseX_per_raster.resize(cfg_->maxRasterLines);

    rasterRowStates.resize(cfg_->maxRasterLines);
    lastFrameRasterRowStates.resize(cfg_->maxRasterLines);

    rasterPixelStates.resize(cfg_->maxRasterLines);
    lastFrameRasterPixelStates.resize(cfg_->maxRasterLines);

    rebuildBorderRasterLatches();

    bgOpaque.resize(cfg_->visibleLines + 2 * BORDER_SIZE);
    for (auto &row : bgOpaque) row.fill(0);

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
    resetBackgroundPipeline();
    resetBackgroundGraphicsLatches();

    std::fill(finalColorLine.begin(), finalColorLine.end(), 0);
    std::fill(borderMaskLine.begin(), borderMaskLine.end(), 1);

    vicState.badLine = false;
    vicState.badLineSampled = false;
    vicState.badLineDmaStartCycle = -1;
    vicState.badLineFetchIndex = 0;
    vicState.badLineInitializedThisRaster = false;

    vicState.displayEnabled = false;
    vicState.displayEnabledNext = false;

    vicState.vc = vicState.vcBase;
    vicState.vmliFetchIndex = 0;

    updateBusArbitration();
    updateMonitorCaches(registers.raster);

    // Notify IO of mode
    if (sink)
        sink->setScreenDimensions(320, cfg_->visibleLines, BORDER_SIZE);
}

void Vic::saveState(StateWriter& wrtr) const
{
    // VIC0 = "Core" and Registers
    wrtr.beginChunk("VIC0");
    wrtr.writeU32(1); // version

    // Dump Sprite Registers
    for (int i = 0; i < 8; ++i)
    {
        wrtr.writeU8(registers.spriteX[i]);
        wrtr.writeU8(registers.spriteY[i]);
    }

    wrtr.writeU8(registers.spriteX_MSB);
    wrtr.writeU8(registers.spriteEnabled);
    wrtr.writeU8(registers.spriteYExpansion);
    wrtr.writeU8(registers.spritePriority);
    wrtr.writeU8(registers.spriteMultiColor);
    wrtr.writeU8(registers.spriteXExpansion);

    // Dump Control registers
    wrtr.writeU8(registers.control);
    wrtr.writeU8(registers.control2);

    // Dump Memory Pointer
    wrtr.writeU8(registers.memory_pointer);

    // Dump Background/Border color registers
    wrtr.writeU8(registers.borderColor);
    wrtr.writeU8(registers.backgroundColor0);
    for (int i = 0; i < 3; ++i)
        wrtr.writeU8(registers.backgroundColor[i]);

    // Dump Sprite Color Registers
    wrtr.writeU8(registers.spriteMultiColor1);
    wrtr.writeU8(registers.spriteMultiColor2);
    for (int i = 0; i < 8; ++i)
        wrtr.writeU8(registers.spriteColors[i]);

    // Dump Raster
    wrtr.writeU16(registers.raster);

    // Dump Interrupt Control
    wrtr.writeU8(registers.interruptStatus);
    wrtr.writeU8(registers.interruptEnable);
    wrtr.writeU16(registers.rasterInterruptLine);

    // Dump Lightpen
    wrtr.writeU8(registers.light_pen_X);
    wrtr.writeU8(registers.light_pen_Y);

    // Dump Collision Latches
    wrtr.writeU8(registers.spriteCollision);
    wrtr.writeU8(registers.spriteDataCollision);

    // End the chunk
    wrtr.endChunk();

    // VICX = Runtime
    wrtr.beginChunk("VICX");
    wrtr.writeU32(8); // version

    // Dump video mode
    wrtr.writeU8(static_cast<uint8_t>(mode_));

    // Dump current cycle
    wrtr.writeI32(currentCycle);

    // Dump Sprite/FIFO
    for (int i=0;i<8;++i)  wrtr.writeU16(sprPtrBase[i]);
    for (int i=0;i<40;++i) wrtr.writeU8(charPtrFIFO[i]);
    for (int i=0;i<40;++i) wrtr.writeU8(colorPtrFIFO[i]);

    // Dump Misc
    wrtr.writeBool(denSeenOn30);
    wrtr.writeI32(firstBadlineY);

    // Dump State
    wrtr.writeU16(vicState.vc);
    wrtr.writeU16(vicState.vcBase);
    wrtr.writeU16(vicState.vmliBase);
    wrtr.writeU8(vicState.vmliFetchIndex);
    wrtr.writeU8(vicState.badLineFetchIndex);
    wrtr.writeI32(vicState.badLineDmaStartCycle);

    wrtr.writeU8(vicState.rc);
    wrtr.writeBool(vicState.matrixAdvancePending);

    wrtr.writeBool(vicState.displayEnabled);
    wrtr.writeBool(vicState.displayEnabledNext);
    wrtr.writeBool(vicState.badLine);
    wrtr.writeBool(vicState.badLineSampled);
    wrtr.writeBool(vicState.badLineInitializedThisRaster);

    wrtr.writeBool(vicState.verticalBorder);
    wrtr.writeBool(vicState.horizontalBorder);

    wrtr.writeBool(vicState.leftBorder);
    wrtr.writeBool(vicState.rightBorder);

    wrtr.writeI32(vicState.leftBorderOpenX);
    wrtr.writeI32(vicState.rightBorderCloseX);

    wrtr.writeI32(vicState.topBorderOpenRaster);
    wrtr.writeI32(vicState.bottomBorderCloseRaster);

    wrtr.writeBool(vicState.ba);
    wrtr.writeBool(vicState.aec);

    wrtr.writeBool(rasterIrqSampledThisLine);

    wrtr.writeBool(activeMatrixRow.valid);
    wrtr.writeU16(activeMatrixRow.vcBase);
    wrtr.writeI32(activeMatrixRow.row);

    for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
        wrtr.writeU8(activeMatrixRow.screen[i]);

    for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
        wrtr.writeU8(activeMatrixRow.color[i]);

    for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
        wrtr.writeU8(activeMatrixRow.fetched[i]);

    for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
        wrtr.writeU8(activeMatrixRow.invalid[i]);

    for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
        wrtr.writeU8(activeMatrixRow.invalidScreen[i]);

    for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
        wrtr.writeU8(activeMatrixRow.invalidColor[i]);

    for (const auto& s : spriteUnits)
    {
        wrtr.writeBool(s.dmaActive);
        wrtr.writeBool(s.yExpandLatch);

        wrtr.writeU8(s.mc);
        wrtr.writeU8(s.mcBase);

        wrtr.writeU8(s.pointerByte);
        wrtr.writeU16(s.dataBase);

        wrtr.writeU8(s.shift0);
        wrtr.writeU8(s.shift1);
        wrtr.writeU8(s.shift2);

        wrtr.writeI32(s.currentRow);

        wrtr.writeI32(s.startY);

        wrtr.writeI32(s.outputBit);
        wrtr.writeI32(s.outputRepeat);
        wrtr.writeBool(s.rowPrepared);
        wrtr.writeBool(s.rowDataLatched);

        wrtr.writeI32(s.outputXStart);
        wrtr.writeI32(s.outputWidth);

        wrtr.writeU8(s.fetched0);
        wrtr.writeU8(s.fetched1);
        wrtr.writeU8(s.fetched2);
    }

    // Dump Latches
    wrtr.writeVectorU8(d011_per_raster);
    wrtr.writeVectorU8(d016_per_raster);
    wrtr.writeVectorU8(d018_per_raster);
    wrtr.writeVectorU16(dd00_per_raster);

    // Background graphics latches
    for (const auto& latch : backgroundGraphicsLatches)
    {
        wrtr.writeBool(latch.valid);
        wrtr.writeI32(latch.column);

        wrtr.writeU8(latch.screenByte);
        wrtr.writeU8(latch.colorByte);
        wrtr.writeU8(latch.graphicsByte);

        wrtr.writeU16(latch.graphicsAddress);

        wrtr.writeU8(latch.d011);
        wrtr.writeU8(latch.d016);
        wrtr.writeU8(latch.d018);
        wrtr.writeU8(static_cast<uint8_t>(latch.mode));
    }

    // Active background pixel shifter
    wrtr.writeBool(activeBgPixel.valid);
    wrtr.writeBool(activeBgPixel.multicolorText);
    wrtr.writeU8(static_cast<uint8_t>(activeBgPixel.mode));

    wrtr.writeU8(activeBgPixel.rowBits);

    wrtr.writeU8(activeBgPixel.fg);
    wrtr.writeU8(activeBgPixel.bg0);
    wrtr.writeU8(activeBgPixel.bg1);
    wrtr.writeU8(activeBgPixel.bg2);

    wrtr.writeU8(static_cast<uint8_t>(activeBgPixel.bg0Source));

    wrtr.writeI32(activeBgPixel.pxBase);
    wrtr.writeI32(activeBgPixel.py);
    wrtr.writeI32(activeBgPixel.phase);

    // Dump frameDone
    wrtr.writeBool(frameDone);

    // End the chunk
    wrtr.endChunk();
}

bool Vic::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "VIC0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                  { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                           { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 8; ++i)
        {
            if (!rdr.readU8(registers.spriteX[i]))              { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(registers.spriteY[i]))              { rdr.exitChunkPayload(chunk); return false; }
        }

        if (!rdr.readU8(registers.spriteX_MSB))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spriteEnabled))               { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spriteYExpansion))            { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spritePriority))              { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spriteMultiColor))            { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spriteXExpansion))            { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.control))                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.control2))                    { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.memory_pointer))              { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.borderColor))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.backgroundColor0))            { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 3; ++i)
            if (!rdr.readU8(registers.backgroundColor[i]))      { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.spriteMultiColor1))           { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spriteMultiColor2))           { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 8; ++i)
            if (!rdr.readU8(registers.spriteColors[i]))         { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU16(registers.raster))                     { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.interruptStatus))             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.interruptEnable))             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU16(registers.rasterInterruptLine))        { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.light_pen_X))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.light_pen_Y))                 { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.spriteCollision))             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spriteDataCollision))         { rdr.exitChunkPayload(chunk); return false; }

        postLoadState();

        rdr.exitChunkPayload(chunk);
        return true;
    }

    if (std::memcmp(chunk.tag, "VICX", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                          { rdr.exitChunkPayload(chunk); return false; }
        if (ver < 1 || ver > 8)                                         { rdr.exitChunkPayload(chunk); return false; }

        uint8_t m = 0;
        if (!rdr.readU8(m))                                             { rdr.exitChunkPayload(chunk); return false; }

        mode_ = static_cast<VideoMode>(m);
        cfg_ = (mode_ == VideoMode::NTSC ? &NTSC_CONFIG : &PAL_CONFIG);

        if (!rdr.readI32(currentCycle))                                 { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 8; ++i)
            if (!rdr.readU16(sprPtrBase[i]))                            { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 40; ++i)
            if (!rdr.readU8(charPtrFIFO[i]))                            { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 40; ++i)
            if (!rdr.readU8(colorPtrFIFO[i]))                           { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(denSeenOn30))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(firstBadlineY))                                { rdr.exitChunkPayload(chunk); return false; }

        if (ver >= 3)
            if (!rdr.readU16(vicState.vc))                              { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU16(vicState.vcBase))                              { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU16(vicState.vmliBase))                            { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(vicState.vmliFetchIndex))                       { rdr.exitChunkPayload(chunk); return false; }

        if (ver >= 7)
        {
            if (!rdr.readU8(vicState.badLineFetchIndex))                { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(vicState.badLineDmaStartCycle))            { rdr.exitChunkPayload(chunk); return false; }
        }
        else
        {
            vicState.badLineFetchIndex = 0;
            vicState.badLineDmaStartCycle = -1;
        }

        if (!rdr.readU8(vicState.rc))                                   { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.matrixAdvancePending))               { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(vicState.displayEnabled))                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.displayEnabledNext))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.badLine))                            { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.badLineSampled))                     { rdr.exitChunkPayload(chunk); return false; }

        if (ver >= 8)
        {
            if (!rdr.readBool(vicState.badLineInitializedThisRaster))   { rdr.exitChunkPayload(chunk); return false; }
        }
        else
        {
            vicState.badLineInitializedThisRaster = false;
        }

        if (!rdr.readBool(vicState.verticalBorder))                     { rdr.exitChunkPayload(chunk); return false; }

        if (ver >= 6)
        {
            if (!rdr.readBool(vicState.horizontalBorder))               { rdr.exitChunkPayload(chunk); return false; }
        }
        else
        {
            vicState.horizontalBorder = true;
        }

        if (!rdr.readBool(vicState.leftBorder))                         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.rightBorder))                        { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readI32(vicState.leftBorderOpenX))                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(vicState.rightBorderCloseX))                   { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readI32(vicState.topBorderOpenRaster))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(vicState.bottomBorderCloseRaster))             { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(vicState.ba))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.aec))                                { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(rasterIrqSampledThisLine))                    { rdr.exitChunkPayload(chunk); return false; }

        if (ver >= 8)
        {
            if (!rdr.readBool(activeMatrixRow.valid))                   { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU16(activeMatrixRow.vcBase))                   { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(activeMatrixRow.row))                      { rdr.exitChunkPayload(chunk); return false; }

            for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
            {
                if (!rdr.readU8(activeMatrixRow.screen[i]))             { rdr.exitChunkPayload(chunk); return false; }
            }

            for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
            {
                if (!rdr.readU8(activeMatrixRow.color[i]))              { rdr.exitChunkPayload(chunk); return false; }
            }

            for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
            {
                if (!rdr.readU8(activeMatrixRow.fetched[i]))            { rdr.exitChunkPayload(chunk); return false; }
            }

            for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
            {
                if (!rdr.readU8(activeMatrixRow.invalid[i]))            { rdr.exitChunkPayload(chunk); return false; }
            }

            for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
            {
                if (!rdr.readU8(activeMatrixRow.invalidScreen[i]))      { rdr.exitChunkPayload(chunk); return false; }
            }

            for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
            {
                if (!rdr.readU8(activeMatrixRow.invalidColor[i]))       { rdr.exitChunkPayload(chunk); return false; }
            }
        }
        else
        {
            resetActiveMatrixRow();
            resetCAccessLatch();
        }

        for (auto& s : spriteUnits)
        {
            if (!rdr.readBool(s.dmaActive))                             { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(s.yExpandLatch))                          { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readU8(s.mc))                                      { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.mcBase))                                  { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readU8(s.pointerByte))                             { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU16(s.dataBase))                               { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readU8(s.shift0))                                  { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.shift1))                                  { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.shift2))                                  { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readI32(s.currentRow))                             { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readI32(s.startY))                                 { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readI32(s.outputBit))                              { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(s.outputRepeat))                           { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(s.rowPrepared))                           { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(s.rowDataLatched))                        { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readI32(s.outputXStart))                           { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(s.outputWidth))                            { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readU8(s.fetched0))                                { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.fetched1))                                { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.fetched2))                                { rdr.exitChunkPayload(chunk); return false; }
        }

        if (!rdr.readVectorU8(d011_per_raster))                         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU8(d016_per_raster))                         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU8(d018_per_raster))                         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU16(dd00_per_raster))                        { rdr.exitChunkPayload(chunk); return false; }

        if (ver >= 2)
        {
            // Background graphics latches
            for (auto& latch : backgroundGraphicsLatches)
            {
                if (!rdr.readBool(latch.valid))             { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readI32(latch.column))             { rdr.exitChunkPayload(chunk); return false; }

                if (!rdr.readU8(latch.screenByte))          { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readU8(latch.colorByte))           { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readU8(latch.graphicsByte))        { rdr.exitChunkPayload(chunk); return false; }

                if (!rdr.readU16(latch.graphicsAddress))    { rdr.exitChunkPayload(chunk); return false; }

                if (ver >= 6)
                {
                    uint8_t mode = 0;

                    if (!rdr.readU8(latch.d011))            { rdr.exitChunkPayload(chunk); return false; }
                    if (!rdr.readU8(latch.d016))            { rdr.exitChunkPayload(chunk); return false; }
                    if (!rdr.readU8(latch.d018))            { rdr.exitChunkPayload(chunk); return false; }
                    if (!rdr.readU8(mode))                  { rdr.exitChunkPayload(chunk); return false; }

                    latch.mode = static_cast<graphicsMode>(mode);
                }
                else
                {
                    latch.d011 = 0;
                    latch.d016 = 0;
                    latch.d018 = 0;
                    latch.mode = graphicsMode::standard;
                }
            }

            // Active standard-text pixel shifter
            if (!rdr.readBool(activeBgPixel.valid))         { rdr.exitChunkPayload(chunk); return false; }

            if (ver >= 4)
            {
                if (!rdr.readBool(activeBgPixel.multicolorText)) {rdr.exitChunkPayload(chunk); return false; }
            }
            else
                activeBgPixel.multicolorText = false;

            if (ver >= 6)
            {
                uint8_t mode = 0;
                if (!rdr.readU8(mode))                     { rdr.exitChunkPayload(chunk); return false; }
                activeBgPixel.mode = static_cast<graphicsMode>(mode);
            }
            else
                activeBgPixel.mode = graphicsMode::standard;

            if (!rdr.readU8(activeBgPixel.rowBits))         { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(activeBgPixel.fg))              { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(activeBgPixel.bg0))             { rdr.exitChunkPayload(chunk); return false; }

            if (ver >= 4)
            {
                if (!rdr.readU8(activeBgPixel.bg1))         { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readU8(activeBgPixel.bg2))         { rdr.exitChunkPayload(chunk); return false; }
            }
            else
            {
                activeBgPixel.bg1 = 0;
                activeBgPixel.bg2 = 0;
            }

            if (ver >= 5)
            {
                uint8_t source = 0;
                if (!rdr.readU8(source))                    { rdr.exitChunkPayload(chunk); return false; }
                activeBgPixel.bg0Source = static_cast<BackgroundSource>(source);
            }
            else
                activeBgPixel.bg0Source = BackgroundSource::BG0;

            if (!rdr.readI32(activeBgPixel.pxBase))         { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(activeBgPixel.py))             { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(activeBgPixel.phase))          { rdr.exitChunkPayload(chunk); return false; }
        }
        else
        {
            // VICX v1 did not contain graphics-latch or live pixel-shifter state.
            resetBackgroundGraphicsLatches();
            resetActiveBackgroundPixelState();
        }

        if (!rdr.readBool(frameDone))                                   { rdr.exitChunkPayload(chunk); return false; }

        if (ver < 3)
            vicState.vc = vicState.vcBase;

        postLoadState();

        rdr.exitChunkPayload(chunk);
        return true;
    }

    return false;
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
             return latchOpenBusMasked(registers.memory_pointer, 0xFE);

        case 0xD019:
        {
            const uint8_t value = d019Read();
            return latchOpenBusMasked(value, 0x8F);
        }

        case 0xD01A:
        {
            const uint8_t value = (registers.interruptEnable & 0x0F);
            return latchOpenBusMasked(value, 0x0F);
        }

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

            registers.control = value & 0x7F;

            recordRasterEventLog(RasterEventKind::Control, 0xD011, oldValue, registers.control);

            const uint16_t newLine =
                static_cast<uint16_t>(
                    (registers.rasterInterruptLine & 0x00FF) |
                    (static_cast<uint16_t>(value & 0x80) << 1)
                );

            setRasterIRQTarget(newLine, "D011", value);

            const int raster = registers.raster;

            updateGraphicsMode(raster);
            updateMonitorCaches(raster);

            traceVicRegWrite(address, oldValue, registers.control);
            break;
        }

        case 0xD012:
        {
            const uint8_t oldLow =
                static_cast<uint8_t>(registers.rasterInterruptLine & 0x00FF);

            const uint16_t newLine =
                static_cast<uint16_t>(
                    (registers.rasterInterruptLine & 0x0100) |
                    static_cast<uint16_t>(value)
                );

            setRasterIRQTarget(newLine, "D012", value);

            traceVicRegWrite(address, oldLow, value);
            break;
        }

        case 0xD013:
        {
            const uint8_t oldValue = registers.light_pen_X;
            registers.light_pen_X = value;
            traceVicRegWrite(address, oldValue, registers.light_pen_X);
            break;
        }

        case 0xD014:
        {
            const uint8_t oldValue = registers.light_pen_Y;
            registers.light_pen_Y = value;
            traceVicRegWrite(address, oldValue, registers.light_pen_Y);
            break;
        }

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
            updateHorizontalBorderState(raster);
            updateGraphicsMode(raster);

            traceVicRegWrite(address, oldValue, registers.control2);
            break;
        }

        case 0xD017:
        {
            const uint8_t oldValue = registers.spriteYExpansion;
            registers.spriteYExpansion = value;
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
            const uint8_t oldPending = registers.interruptStatus & 0x0F;
            const uint8_t clearMask = value & 0x0F;

            registers.interruptStatus &= ~clearMask;

            const uint8_t newPending = registers.interruptStatus & 0x0F;

            traceVicRegWrite(address, oldPending, newPending);
            updateIRQLine();
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
    const uint16_t dotX = getRasterDot();

    registers.light_pen_X = static_cast<uint8_t>((dotX >> 1) & 0xFF);
    registers.light_pen_Y = static_cast<uint8_t>(registers.raster & 0xFF);

    // VIC IRQ bit 3 = light pen.
    raiseVicIRQSource(0x08);
}

void Vic::beginCycle()
{
    beginFrameIfNeeded();

    currentCycleSlot = cycleSlotFor(registers.raster, currentCycle);

    runCycleDecisionPhase();

    const uint8_t startedMask = currentCycleSlot.spriteDmaStartMask;

    // Decisions made this cycle can change bad-line and sprite DMA
    // state, so rebuild the slot before applying BA/AEC.
    currentCycleSlot = cycleSlotFor(registers.raster, currentCycle);

    currentCycleSlot.spriteDmaStartMask = startedMask;

    updateBusArbitration();
}

void Vic::endCycle()
{
    runFetchPhase();
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

        // Preserve completed-frame diagnostics before clearing current-frame state.
        lastFrameRasterRowStates = rasterRowStates;
        lastFrameRasterPixelStates = rasterPixelStates;

        rasterEventLog.clear();

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
        vicState.matrixAdvancePending = false;

        vicState.badLine = false;
        vicState.badLineSampled = false;
        vicState.badLineDmaStartCycle = -1;
        vicState.badLineFetchIndex = 0;
        vicState.badLineInitializedThisRaster = false;

        vicState.displayEnabled = false;
        vicState.displayEnabledNext = false;

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
        sampleRasterIRQCompare("normal-sample");

    if (slot.latchRasterState)
        handleCycle0Decisions();

    updateLiveBadLineCondition();

    if (slot.sampleBadline)
        handleCycle14Decisions();

    if (slot.startSpriteDmaCheck)
    {
        handleCycle15Decisions();
        currentCycleSlot.spriteDmaStartMask = updateSpriteDMAStartForCurrentLine(registers.raster);
    }

    if (slot.transferDisplayState)
        handleCycle58Decisions();

    if (slot.startBadlineFetch)
        handleDmaStartCycleDecisions();

    if (currentCycle == cfg_->cyclesPerLine - 1)
        updateVerticalBorderState(registers.raster);
}

void Vic::handleCycle0Decisions()
{
    const int raster = registers.raster;

    d011_per_raster[raster] = registers.control & 0x7F;
    d016_per_raster[raster] = registers.control2 & 0x1F;
    d018_per_raster[raster] = registers.memory_pointer & 0xFE;

    latchNextRasterDD00();

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

    vicState.vc = vicState.vcBase;
    vicState.vmliFetchIndex = 0;

    const bool badAtCycle14 = isBadLine(raster);

    vicState.badLineSampled = badAtCycle14;

    traceVicCycleCheckpoint("cycle-14", raster, currentCycle);

    if (badAtCycle14)
    {
        vicState.badLine = true;

        vicState.badLineDmaStartCycle = cfg_->DMAStartCycle;

        const bool firstBadlineThisFrame = (firstBadlineY < 0);

        vicState.rc = 0;

        initializeFirstBadLineIfNeeded(raster);

        if (firstBadlineThisFrame)
            vicState.displayEnabledNext = true;
    }
}

void Vic::handleCycle15Decisions()
{
    const int raster = registers.raster;

    if (vicState.badLineSampled)
    {
        traceVicBadLineStart(raster, currentCycle, vicState.vcBase, vicState.rc, true);
        beginBadLineFetch();
    }
}

void Vic::handleDmaStartCycleDecisions()
{
    const int raster = registers.raster;
    const uint16_t nextRaster = (raster + 1) % cfg_->maxRasterLines;

    d011_per_raster[nextRaster] = registers.control & 0x7F;
    d016_per_raster[nextRaster] = registers.control2;
    d018_per_raster[nextRaster] = registers.memory_pointer;
}

void Vic::handleCycle58Decisions()
{
    traceVicCycleCheckpoint("cycle-58", registers.raster, currentCycle);

    const bool badLineCanCarry = isBadLine(registers.raster) && rasterWithinVerticalDisplayWindow(registers.raster);
    vicState.displayEnabledNext = vicState.displayEnabled || badLineCanCarry;
}

void Vic::runFetchPhase()
{
    const int raster = registers.raster;

    performBackgroundGraphicsFetchForCurrentCycle();

    if (currentCycleSlot.graphicsFetch && vicState.displayEnabled && vicState.vmliFetchIndex < BACKGROUND_MATRIX_COLUMNS)
    {
        ++vicState.vmliFetchIndex;
        vicState.vc = static_cast<uint16_t>((vicState.vc + 1) & 0x03FF);
    }

    switch (currentCycleSlot.fetchKind)
    {

        case FetchKind::Graphics:
            break;

        case FetchKind::CharMatrix:
            performBadLineFetchesForCurrentCycle();
            break;

        case FetchKind::SpritePtr0:
        case FetchKind::SpritePtr1:
        case FetchKind::SpritePtr2:
        case FetchKind::SpritePtr3:
        case FetchKind::SpritePtr4:
        case FetchKind::SpritePtr5:
        case FetchKind::SpritePtr6:
        case FetchKind::SpritePtr7:
        {
            const int sprite = currentCycleSlot.spriteIndex;
            if (sprite >= 0)
                fetchSpritePointer(sprite, raster);

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
            const int sprite = currentCycleSlot.spriteIndex;
            if (sprite >= 0)
                performSpriteDataFetchForSprite(sprite);

            break;
        }

        case FetchKind::None:
        default:
            performIdleFetchForCurrentCycle();
            break;
    }
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
        resetBackgroundPipeline();
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

    const uint8_t d011 = d011ForRasterPixelX(raster, x, false);
    const uint8_t d016 = d016ForRasterPixelX(raster, x, false);

    const graphicsMode liveMode = graphicsModeFromRegisters(d011, d016);

    if (liveMode != graphicsMode::standard &&
        liveMode != graphicsMode::multicolor &&
        liveMode != graphicsMode::bitmap &&
        liveMode != graphicsMode::multicolorBitmap &&
        liveMode != graphicsMode::extendedColorText)
    {
        return;
    }

    const int xScroll = static_cast<int>(d016 & 0x07);

    if (currentCycleSlot.graphicsFetch)
    {
        const int fetchColumn = static_cast<int>(vicState.vmliFetchIndex) - 1;

        if (fetchColumn >= 0 && fetchColumn < BACKGROUND_MATRIX_COLUMNS)
        {
            const int reloadX = cycleFramebufferX(currentCycle) + xScroll;

            if (x == reloadX)
            {
                const BackgroundGraphicsLatch& latch = backgroundGraphicsLatches[fetchColumn];

                if (latch.valid)
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

    if (outputMode == graphicsMode::multicolorBitmap)
        pixel = sampleAndAdvanceActiveMulticolorBitmapPixel();
    else if (outputMode == graphicsMode::bitmap)
        pixel = sampleAndAdvanceActiveStandardBitmapPixel();
    else if (outputMode == graphicsMode::multicolor && activeBgPixel.multicolorText)
        pixel = sampleAndAdvanceActiveMulticolorTextPixel();
    else
        pixel = sampleAndAdvanceActiveStandardTextPixel();

    stampBackgroundPixelSource(x, activeBgPixel.py, pixel.color, pixel.opaque, pixel.source);

    if (activeBgPixel.phase >= 8)
        activeBgPixel.valid = false;
}

void Vic::performBackgroundGraphicsFetchForCurrentCycle()
{
    if (!currentCycleSlot.graphicsFetch)
        return;

    const int column = static_cast<int>(vicState.vmliFetchIndex);

    if (column < 0 || column >= BACKGROUND_MATRIX_COLUMNS)
        return;

    if (!vicState.displayEnabled)
        return;

    const int fetchPixelX = cyclePixelX(currentCycle);
    const int outputX     = cycleFramebufferX(currentCycle);
    const int fetchX      = cycleFramebufferX(currentCycle);

    const graphicsMode mode = graphicsModeForRasterPixel(registers.raster, fetchX, false);

    if (mode != graphicsMode::standard &&
        mode != graphicsMode::multicolor &&
        mode != graphicsMode::bitmap &&
        mode != graphicsMode::multicolorBitmap &&
        mode != graphicsMode::extendedColorText)
    {
        return;
    }

    traceBackgroundGraphicsFetch(registers.raster, currentCycle, column, fetchPixelX, outputX);

    switch (mode)
    {
        case graphicsMode::standard:
        case graphicsMode::multicolor:
        case graphicsMode::extendedColorText:
            fetchStandardTextGraphicsByte(registers.raster, column, fetchX);
            break;

        case graphicsMode::bitmap:
        case graphicsMode::multicolorBitmap:
            fetchStandardBitmapGraphicsByte(registers.raster, column, fetchX);
            break;

        default:
            break;
    }
}

int Vic::spriteDataByteIndexForCycle(int sprite, int cycle) const
{
    const int lineCycles = cfg_->cyclesPerLine;
    const int slotStart = spriteFetchSlotStart(sprite);
    const int firstDataCycle = (slotStart + 1) % lineCycles;

    int byteIndex = cycle - firstDataCycle;
    if (byteIndex < 0)
        byteIndex += lineCycles;

    return byteIndex;
}

uint16_t Vic::spritePointerAddressForRaster(int sprite, int raster, int cycle) const
{
    if (sprite < 0 || sprite >= 8)
        return 0;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return 0;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        cycle = currentCycle;

    const int px = rasterEventPixelX(cycle);

    const uint16_t screenBase = screenBaseForRasterPixelX(raster, px);

    return static_cast<uint16_t>(screenBase + 0x03F8 + sprite);
}

void Vic::performBadLineFetchesForCurrentCycle()
{
    if (!vicState.badLine)
        return;

    if (currentCycleSlot.fetchKind != FetchKind::CharMatrix)
        return;

    const int physicalIndex = currentCycleSlot.matrixFetchIndex;

    if (physicalIndex < 0 || physicalIndex >= BACKGROUND_MATRIX_COLUMNS)
        return;

    const int fetchIndex = static_cast<int>(vicState.badLineFetchIndex);

    if (fetchIndex < 0 || fetchIndex >= BACKGROUND_MATRIX_COLUMNS)
        return;

    if (!currentCycleSlot.cpuBusStolen)
    {
        const uint8_t cpuBusValue = getOpenBus();

        // Invalid late c-access:
        // character byte reads as $FF while the color nibble is
        // sourced from the shared/open data bus.
        cAccessScreenLatch = 0xFF;
        cAccessColorLatch = static_cast<uint8_t>(cpuBusValue & 0x0F);
        cAccessLatchValid = true;
        cAccessLatchIndex = fetchIndex;

        if (activeMatrixRow.valid)
        {
            activeMatrixRow.invalid[fetchIndex] = 1;
            activeMatrixRow.invalidScreen[fetchIndex] = cAccessScreenLatch;
            activeMatrixRow.invalidColor[fetchIndex] = cAccessColorLatch;
        }
    }
    else
        fetchBadLineMatrixByte(fetchIndex, registers.raster);

    if (vicState.badLineFetchIndex < BACKGROUND_MATRIX_COLUMNS)
        ++vicState.badLineFetchIndex;
}

void Vic::updateLiveBadLineCondition()
{
    const int raster = registers.raster;

    if (currentCycle < 12 || currentCycle > 54)
        return;

    const bool badNow = isBadLine(raster);

    if (!badNow)
    {
        if (vicState.badLine)
        {
            vicState.badLine = false;

            // Before cycle 14, nothing has committed yet, so the
            // pending DMA setup can be discarded completely.
            if (currentCycle < 14)
            {
                vicState.badLineDmaStartCycle = -1;
                vicState.badLineFetchIndex = 0;
            }
        }

        return;
    }

    if (!vicState.badLine)
    {
        vicState.badLine = true;

        // A newly asserted Bad Line starts a new BA/AEC takeover
        // sequence from this point.
        vicState.badLineDmaStartCycle = currentCycle + 4;

        vicState.displayEnabled = true;
        vicState.displayEnabledNext = true;

        // Only initialize matrix-fetch state the first time the
        // Bad Line Condition becomes true on this raster.
        if (!vicState.badLineInitializedThisRaster)
        {
            vicState.badLineInitializedThisRaster = true;

            vicState.vmliBase = vicState.vcBase;
            vicState.badLineFetchIndex = 0;

            activeMatrixRow.valid = true;
            activeMatrixRow.vcBase = vicState.vmliBase;
            activeMatrixRow.row = static_cast<int>(vicState.vmliBase / BACKGROUND_MATRIX_COLUMNS);

            activeMatrixRow.screen.fill(0);
            activeMatrixRow.color.fill(0);
            activeMatrixRow.fetched.fill(0);
            activeMatrixRow.invalid.fill(0);
            activeMatrixRow.invalidScreen.fill(0);
            activeMatrixRow.invalidColor.fill(0);
        }
    }
}

void Vic::initializeFirstBadLineIfNeeded(int raster)
{
    if (firstBadlineY >= 0)
        return;

    firstBadlineY = raster;

    // Seed the first visible character row only before display
    // progression has actually started.
    if (!vicState.displayEnabled)
    {
        vicState.vcBase = 0;
        vicState.vmliBase = 0;
        vicState.rc = 0;
    }
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
    advanceCharacterSequencerEndOfLine(curRaster);

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
            const int fbH = cfg_->visibleLines + 2 * BORDER_SIZE;

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

    vicState.badLineSampled = false;
    vicState.badLineInitializedThisRaster = false;

    rasterIrqSampledThisLine = false;
}

void Vic::traceRasterEnd()
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_RASTER))
        return;

    TraceManager::Stamp stamp =
        traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0,
                            registers.raster,
                            (currentCycle * 8));

    traceMgr->recordVicRaster(registers.raster, currentCycle,
                              (registers.interruptStatus & 0x01) != 0,
                              registers.control,
                              registers.rasterInterruptLine & 0xFF,
                              stamp);
}

int Vic::spriteFetchSlotStart(int sprite) const
{
    return cfg_->spriteFetchSlots[sprite];
}

bool Vic::isSpriteDMAFetchCycle(int sprite, int cycle) const
{
    const int slotStart = spriteFetchSlotStart(sprite);
    const int lineCycles = cfg_->cyclesPerLine;

    return cycle == ((slotStart + 1) % lineCycles) ||
           cycle == ((slotStart + 2) % lineCycles) ||
           cycle == ((slotStart + 3) % lineCycles);
}


Vic::HorizontalBorderWindow Vic::horizontalBorderWindowForCSEL(bool csel40) const
{
    HorizontalBorderWindow w {};

    if (csel40)
    {
        w.openX = 31;
        w.closeX = 351;
    }
    else
    {
        w.openX = 38;
        w.closeX = 344;
    }

    w.openX = std::clamp(w.openX, 0, VISIBLE_WIDTH);
    w.closeX = std::clamp(w.closeX, 0, VISIBLE_WIDTH);

    if (w.openX >= w.closeX)
    {
        w.openX = 0;
        w.closeX = 0;
    }

    return w;
}

Vic::VerticalBorderWindow Vic::verticalBorderWindowForRaster(int raster) const
{
    VerticalBorderWindow w {};

    const bool rsel25 = getLatchedRSEL(raster);

    w.topOpen = verticalBorderOpenCompareRaster(rsel25);
    w.bottomClose = verticalBorderCloseCompareRaster(rsel25) - 1;

    return w;
}

Vic::BorderWindow Vic::borderWindowForRaster(int raster) const
{
    BorderWindow w {};

    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return w;

    w.vertical = borderVertical_per_raster[raster] != 0;
    w.openX = std::clamp<int>(borderLeftOpenX_per_raster[raster], 0, VISIBLE_WIDTH);
    w.closeX = std::clamp<int>(borderRightCloseX_per_raster[raster], 0, VISIBLE_WIDTH);

    if (w.openX >= w.closeX)
    {
        w.vertical = true;
        w.openX = 0;
        w.closeX = VISIBLE_WIDTH;
    }

    return w;
}

int Vic::horizontalBorderOpenCompareX(bool csel40) const
{
    return horizontalBorderWindowForCSEL(csel40).openX;
}

int Vic::horizontalBorderCloseCompareX(bool csel40) const
{
    return horizontalBorderWindowForCSEL(csel40).closeX;
}

int Vic::verticalBorderOpenCompareRaster(bool rsel25) const
{
    return rsel25 ? 51 : 55;
}

int Vic::verticalBorderCloseCompareRaster(bool rsel25) const
{
    return rsel25 ? 251 : 247;
}

void Vic::updateVerticalBorderStateAtLeftCompare(int raster, int px)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    if (px < 0 || px >= VISIBLE_WIDTH)
        return;

    const uint8_t d016 = d016ForRasterPixelX(raster, px, false);

    const bool csel40 = (d016 & 0x08) != 0;

    const int compareX = horizontalBorderOpenCompareX(csel40);

    if (px != compareX)
        return;

    const uint8_t d011 = d011ForRasterPixelX(raster, px, false);

    applyVerticalBorderCompare(raster, d011);
}

bool Vic::spriteCanRenderThisRaster(int sprite) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    if (!spriteUnits[sprite].dmaActive)
        return false;

    if (!spriteUnits[sprite].rowDataLatched)
        return false;

    return true;
}

void Vic::resetSpriteLineOutputState(int sprite)
{
    spriteUnits[sprite].rowPrepared = false;
    spriteUnits[sprite].outputBit = 0;
    spriteUnits[sprite].outputRepeat = 0;
    spriteUnits[sprite].outputXStart = 0;
    spriteUnits[sprite].outputWidth = 0;
}

void Vic::clearSpriteFetchedRowState(int sprite)
{
    spriteUnits[sprite].rowDataLatched = false;

    spriteUnits[sprite].fetched0 = 0;
    spriteUnits[sprite].fetched1 = 0;
    spriteUnits[sprite].fetched2 = 0;

    spriteUnits[sprite].shift0 = 0;
    spriteUnits[sprite].shift1 = 0;
    spriteUnits[sprite].shift2 = 0;
}

uint32_t Vic::getLatchedSpriteBits(int sprite) const
{
    if (sprite < 0 || sprite >= 8)
        return 0;

    return  (uint32_t(spriteUnits[sprite].shift0) << 16)
          | (uint32_t(spriteUnits[sprite].shift1) << 8)
          |  uint32_t(spriteUnits[sprite].shift2);
}

bool Vic::initialSpriteMulticolorModeForRaster(int raster, uint8_t& value) const
{
    for (const RasterSpriteModeEvent& e : rasterSpriteModeEvents)
    {
        if (e.raster != raster)
            continue;

        value = e.oldValue;
        return true;
    }

    return false;
}

bool Vic::spriteMulticolorAtPixel(int sprite, int px) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    if (px < 0 || px >= VISIBLE_WIDTH)
        return false;

    uint8_t activeMode = registers.spriteMultiColor;

    if (initialSpriteMulticolorModeForRaster(registers.raster, activeMode))
    {
        for (const RasterSpriteModeEvent& e : rasterSpriteModeEvents)
        {
            if (e.raster != registers.raster)
                continue;

            const int eventX = rasterEventPixelX(e.cycle);

            if (eventX > px)
                continue;

            activeMode = e.newValue;
        }
    }

    return (activeMode & static_cast<uint8_t>(1u << sprite)) != 0;
}

bool Vic::initialSpriteXExpansionForRaster(int raster, uint8_t& value) const
{
    for (const RasterSpriteXExpansionEvent& e : rasterSpriteXExpansionEvents)
    {
        if (e.raster != raster)
            continue;

        value = e.oldValue;
        return true;
    }

    return false;
}

bool Vic::spriteXExpandedAtPixel(int sprite, int px) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    if (px < 0 || px >= VISIBLE_WIDTH)
        return false;

    uint8_t activeExpansion = registers.spriteXExpansion;

    if (initialSpriteXExpansionForRaster(registers.raster, activeExpansion))
    {
        for (const RasterSpriteXExpansionEvent& e : rasterSpriteXExpansionEvents)
        {
            if (e.raster != registers.raster)
                continue;

            const int eventX = rasterEventPixelX(e.cycle);

            if (eventX > px)
                continue;

            activeExpansion = e.newValue;
        }
    }

    return (activeExpansion & static_cast<uint8_t>(1u << sprite)) != 0;
}

bool Vic::firstRasterSpriteEnableEventValue(int raster, uint8_t& value) const
{
    for (const RasterSpriteEnableEvent& e : rasterSpriteEnableEvents)
    {
        if (e.raster != raster)
            continue;

        value = e.oldValue;
        return true;
    }

    return false;
}

bool Vic::spriteEnabledAtPixel(int sprite, int px) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    if (px < 0 || px >= VISIBLE_WIDTH)
        return false;

    uint8_t activeEnable = registers.spriteEnabled;

    if (firstRasterSpriteEnableEventValue(registers.raster, activeEnable))
    {
        for (const RasterSpriteEnableEvent& e : rasterSpriteEnableEvents)
        {
            if (e.raster != registers.raster)
                continue;

            const int eventX = rasterEventPixelX(e.cycle);

            if (eventX > px)
                continue;

            activeEnable = e.newValue;
        }
    }

    return (activeEnable & static_cast<uint8_t>(1u << sprite)) != 0;
}

void Vic::fetchSpritePointer(int sprite, int raster)
{
    if (!mem)
        return;

    const uint16_t ptrLoc = spritePointerAddressForRaster(sprite, raster, currentCycle);
    const uint8_t ptr = mem->vicRead(ptrLoc, raster);

    // Latch Open Bus
    updateOpenBus(ptr);

    traceVicSpritePtrFetch(sprite, raster, ptrLoc, ptr);

    spriteUnits[sprite].pointerByte = ptr;
    spriteUnits[sprite].dataBase = static_cast<uint16_t>(ptr) << 6;
    sprPtrBase[sprite] = spriteUnits[sprite].dataBase;

    traceVicSpriteSlotEvent(sprite, "ptr", raster, currentCycle);
}

void Vic::prepareSpriteOutputForRaster(int raster)
{
    for (int i = 0; i < 8; ++i)
    {
        resetSpriteLineOutputState(i);

        if (!spriteUnits[i].dmaActive)
        {
            traceVicSpriteSlotEvent(i, "prep-inactive", raster, currentCycle);
            clearSpriteFetchedRowState(i);
            continue;
        }

        if (!spriteUnits[i].rowDataLatched)
        {
            traceVicSpriteSlotEvent(i, "prep-no-row", raster, currentCycle);
            continue;
        }

        traceVicSpriteSlotEvent(i, "prep-output", raster, currentCycle);
        beginSpriteLineOutput(i, raster);
    }
}

void Vic::beginSpriteLineOutput(int spr, int raster)
{
    int rowInSprite = 0;
    int fbLine = 0;

    resetSpriteLineOutputState(spr);

    if (!spriteCanRenderThisRaster(spr))
        return;

    if (!spriteDisplayCoversRaster(spr, raster, rowInSprite, fbLine))
        return;

    spriteUnits[spr].rowPrepared = true;
    resetSpriteLineSequencer(spr, raster);
}

void Vic::resetSpriteLineSequencer(int sprIndex, int raster)
{
    if (sprIndex < 0 || sprIndex >= 8)
        return;

    SpriteUnit& u = spriteUnits[sprIndex];

    u.outputBit = 0;
    u.outputRepeat = 0;
    u.outputXStart = spriteScreenXFor(sprIndex, raster);

    const int sampleX = std::clamp(u.outputXStart, 0, VISIBLE_WIDTH - 1);
    const bool expanded = spriteXExpandedAtPixel(sprIndex, sampleX);

    u.outputWidth = expanded ? SPRITE_OUTPUT_WIDTH_EXPANDED_MAX : 24;
}

void Vic::advanceSpriteOutputState(int sprIndex, int px)
{
    if (sprIndex < 0 || sprIndex >= 8)
        return;

    const bool expandX = spriteXExpandedAtPixel(sprIndex, px);
    const bool multClr = spriteMulticolorAtPixel(sprIndex, px);

    const int repeatsPerSourceUnit = multClr ? (expandX ? 4 : 2) : (expandX ? 2 : 1);
    spriteUnits[sprIndex].outputRepeat++;

    if (spriteUnits[sprIndex].outputRepeat >= repeatsPerSourceUnit)
    {
        spriteUnits[sprIndex].outputRepeat = 0;
        spriteUnits[sprIndex].outputBit++;
    }
}

bool Vic::currentSpriteSequencerPixel(int sprIndex, int px, uint8_t& outColor, bool& opaque, SpriteColorSource& outSource) const
{
    outColor = 0;
    opaque = false;
    outSource = SpriteColorSource::None;

    if (sprIndex < 0 || sprIndex >= 8)
        return false;

    if (!spriteUnits[sprIndex].rowPrepared)
        return false;

    const bool multClr = spriteMulticolorAtPixel(sprIndex, px);
    const uint32_t rowBits = getLatchedSpriteBits(sprIndex);

    if (!multClr)
    {
        const int srcBit = spriteUnits[sprIndex].outputBit;
        if (srcBit < 0 || srcBit >= 24)
            return false;

        if (((rowBits >> (23 - srcBit)) & 0x01) == 0)
            return false;

        // Color is intentionally assigned later by applySpriteColorEventsToLine().
        // This function only identifies opacity and color source.
        outColor = 0;
        opaque = true;
        outSource = SpriteColorSource::SpriteOwnColor;
        return true;
    }

    const int srcPair = spriteUnits[sprIndex].outputBit;
    if (srcPair < 0 || srcPair >= 12)
        return false;

    const uint8_t bits = static_cast<uint8_t>((rowBits >> (22 - srcPair * 2)) & 0x03);

    if (bits == 0)
        return false;

    switch (bits)
    {
        case 0x01:
            outSource = SpriteColorSource::SpriteMultiColor1;
            break;

        case 0x02:
            outSource = SpriteColorSource::SpriteOwnColor;
            break;

        case 0x03:
            outSource = SpriteColorSource::SpriteMultiColor2;
            break;

        default:
            return false;
    }

    // Color is intentionally assigned later by applySpriteColorEventsToLine().
    outColor = 0;
    opaque = true;
    return true;
}

void Vic::clearSpriteLineBuffers()
{
    for (auto& line : spriteOpaqueLine)
        line.fill(0);

    for (auto& line : spriteColorLine)
        line.fill(0);

    for (auto& line : spriteColorSourceLine)
        line.fill(SpriteColorSource::None);
}

void Vic::beginSpriteRasterOutput(int raster)
{
    for (int spr = 0; spr < 8; ++spr)
    {
        if (!spriteUnits[spr].rowPrepared)
            continue;

        if (!spriteUnits[spr].rowDataLatched)
            continue;

        traceVicSpriteSlotEvent(spr, "display-begin", raster, currentCycle);
    }
}

void Vic::stepSpriteSequencersAtX(int raster, int px)
{
    if (px < 0 || px >= VISIBLE_WIDTH)
        return;

    for (int spr = 0; spr < 8; ++spr)
    {
        SpriteUnit& u = spriteUnits[spr];

        if (!u.rowPrepared)
            continue;

        if (px < u.outputXStart)
            continue;

        if (px >= u.outputXStart + u.outputWidth)
            continue;

        // Event-aware D015 gate:
        // the sprite may be prepared for the line, but individual pixels
        // should only be emitted while the sprite is enabled at that X.
        if (!spriteEnabledAtPixel(spr, px))
        {
            advanceSpriteOutputState(spr, px);
            continue;
        }

        uint8_t color = 0;
        bool opaque = false;
        SpriteColorSource source = SpriteColorSource::None;

        if (currentSpriteSequencerPixel(spr, px, color, opaque, source) && opaque)
        {
            spriteOpaqueLine[spr][px] = 1;
            spriteColorLine[spr][px] = static_cast<uint8_t>(color & 0x0F);
            spriteColorSourceLine[spr][px] = source;

            if (bgOpaqueLine[px])
            {
                const uint8_t bit = static_cast<uint8_t>(1u << spr);
                latchSpriteBackgroundCollision(bit, raster, px);
            }

            for (int other = 0; other < spr; ++other)
            {
                if (!spriteOpaqueLine[other][px])
                    continue;

                const uint8_t bits =
                    static_cast<uint8_t>((1 << spr) | (1 << other));

                latchSpriteSpriteCollision(bits, raster, px);
            }
        }

        advanceSpriteOutputState(spr, px);
    }
}

void Vic::outputSpritePixel(int raster, int px)
{
    stepSpriteSequencersAtX(raster, px);
}

void Vic::updateSpriteDMAEndOfLine(int raster)
{
    for (int s = 0; s < 8; ++s)
    {
        if (!spriteUnits[s].dmaActive)
            continue;

        traceVicSpriteSlotEvent(s, "eol-before", raster, currentCycle);

        const bool willAdvance = shouldAdvanceSpriteMCBaseThisLine(s);
        traceVicSpriteAdvanceDecision(s, raster, willAdvance);

        if (willAdvance)
            spriteUnits[s].mcBase = static_cast<uint8_t>(spriteUnits[s].mcBase + 3);

        spriteUnits[s].mc = spriteUnits[s].mcBase;

        if (spriteUnits[s].yExpandLatch)
            spriteUnits[s].currentRow += 1;
        else
            spriteUnits[s].currentRow = spriteRowFromMCBase(s);

        if (spriteUnits[s].mcBase >= 63)
        {
            traceVicSpriteSlotEvent(s, "dma-stop", raster, currentCycle);
            clearSpriteFetchedRowState(s);
            resetSpriteDMAState(s);
            continue;
        }

        traceVicSpriteSlotEvent(s, "eol-after", raster, currentCycle);
    }
}

int Vic::spriteRowFromMCBase(int spr) const
{
    return spriteUnits[spr].mcBase / 3;
}

bool Vic::shouldAdvanceSpriteMCBaseThisLine(int spr) const
{
    if (!spriteUnits[spr].yExpandLatch)
        return true;

    const int currentRow = spriteUnits[spr].currentRow;

    // In expanded Y mode, each sprite row is used for two raster lines.
    // currentRow starts at 0, so do not advance after the first line of a pair.
    // Advance after rows 1, 3, 5, etc.
    return (currentRow & 1) != 0;
}

void Vic::resetSpriteDMAState(int spr)
{
    spriteUnits[spr].dmaActive = false;
    spriteUnits[spr].yExpandLatch = false;

    spriteUnits[spr].currentRow = 0;
    spriteUnits[spr].mc = 0;
    spriteUnits[spr].mcBase = 0;
    spriteUnits[spr].startY = 0;

    resetSpriteLineOutputState(spr);
    clearSpriteFetchedRowState(spr);
}

void Vic::performSpriteDataFetchForSprite(int sprite)
{
    if (sprite < 0 || sprite >= 8)
        return;

    if (!spriteUnits[sprite].dmaActive)
        return;

    const int byteIndex = currentCycleSlot.spriteByteIndex;

    if (byteIndex < 0 || byteIndex >= 3)
        return;

    fetchSpriteDataByte(sprite, byteIndex, registers.raster);
}

int Vic::spritePointerFetchSpriteForKind(FetchKind kind) const
{
    switch (kind)
    {
        case FetchKind::SpritePtr0: return 0;
        case FetchKind::SpritePtr1: return 1;
        case FetchKind::SpritePtr2: return 2;
        case FetchKind::SpritePtr3: return 3;
        case FetchKind::SpritePtr4: return 4;
        case FetchKind::SpritePtr5: return 5;
        case FetchKind::SpritePtr6: return 6;
        case FetchKind::SpritePtr7: return 7;
        default: return -1;
    }
}

int Vic::spriteDataFetchSpriteForKind(FetchKind kind) const
{
    switch (kind)
    {
        case FetchKind::SpriteData0: return 0;
        case FetchKind::SpriteData1: return 1;
        case FetchKind::SpriteData2: return 2;
        case FetchKind::SpriteData3: return 3;
        case FetchKind::SpriteData4: return 4;
        case FetchKind::SpriteData5: return 5;
        case FetchKind::SpriteData6: return 6;
        case FetchKind::SpriteData7: return 7;
        default: return -1;
    }
}

void Vic::fetchSpriteDataByte(int sprite, int byteIndex, int raster)
{
    if (!mem)
        return;

    const int rowInSprite = spriteRowFromMCBase(sprite);
    if (rowInSprite < 0 || rowInSprite >= 21)
        return;

    const uint16_t addr = spriteUnits[sprite].dataBase + rowInSprite * 3 + byteIndex;
    const uint8_t value = mem->vicRead(addr, raster);

    // Latch Open Bus
    updateOpenBus(value);

    traceVicSpriteDataFetch(sprite, raster, byteIndex, addr, value);

    if (byteIndex == 0)
    {
        spriteUnits[sprite].fetched0 = value;
    }
    else if (byteIndex == 1)
    {
        spriteUnits[sprite].fetched1 = value;
    }
    else if (byteIndex == 2)
    {
        spriteUnits[sprite].fetched2 = value;

        // Sprite row data becomes live when the 3rd byte arrives.
        latchSpriteShiftersFromFetchedBytes(sprite);
    }

    traceVicSpriteSlotEvent(sprite, "data", raster, currentCycle, byteIndex);
}

void Vic::latchSpriteShiftersFromFetchedBytes(int sprite)
{
    spriteUnits[sprite].shift0 = spriteUnits[sprite].fetched0;
    spriteUnits[sprite].shift1 = spriteUnits[sprite].fetched1;
    spriteUnits[sprite].shift2 = spriteUnits[sprite].fetched2;
    spriteUnits[sprite].rowDataLatched = true;

    traceVicSpriteSlotEvent(sprite, "row-latched", registers.raster, currentCycle);
}

uint8_t Vic::updateSpriteDMAStartForCurrentLine(int raster)
{
    uint8_t startedMask = 0;

    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const uint8_t spriteBit     = static_cast<uint8_t>(1u << sprite);
        const bool enabled          = (registers.spriteEnabled & spriteBit) != 0;
        const bool yExpanded        = (registers.spriteYExpansion & spriteBit) != 0;
        const bool rasterMatches    = raster == registers.spriteY[sprite];
        const bool alreadyActive    = spriteUnits[sprite].dmaActive;
        const bool shouldStart      = enabled && rasterMatches && !alreadyActive;

        traceVicSpriteStartCheck(sprite, raster, registers.spriteY[sprite], enabled, yExpanded, rasterMatches, shouldStart);

        if (!shouldStart)
            continue;

        SpriteUnit& unit = spriteUnits[sprite];

        unit.dmaActive = true;
        unit.yExpandLatch = yExpanded;
        unit.currentRow = 0;
        unit.mc = 0;
        unit.mcBase = 0;
        unit.startY = registers.spriteY[sprite];

        resetSpriteLineOutputState(sprite);
        clearSpriteFetchedRowState(sprite);

        startedMask |= spriteBit;

        traceVicSpriteDmaStart(sprite);
        traceVicSpriteSlotEvent(sprite, "dma-start", raster, currentCycle);
    }

    return startedMask;
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
        traceVicBusArb(oldBA, oldAEC, vicState.ba, vicState.aec, vicState.badLineSampled, currentCycleSlot.baLow,
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

    const int lineCycles = cfg_->cyclesPerLine;
    const int slot = cfg_->DMAStartCycle;

    const int warn0 = (slot - 3 + lineCycles) % lineCycles;
    const int warn1 = (slot - 2 + lineCycles) % lineCycles;
    const int warn2 = (slot - 1 + lineCycles) % lineCycles;

    return cycle == warn0 || cycle == warn1 || cycle == warn2;
}

bool Vic::isBadLineBusStealCycle(int raster, int cycle) const
{
    if (raster != registers.raster)
        return false;

    if (!vicState.badLine)
        return false;

    if (vicState.badLineDmaStartCycle < 0)
        return false;

    if (cycle < vicState.badLineDmaStartCycle ||
        cycle > cfg_->DMAEndCycle)
    {
        return false;
    }

    return getFetchKindForCycle(raster, cycle) == FetchKind::CharMatrix;
}

bool Vic::isBadLineBAHoldCycle(int raster, int cycle) const
{
    if (raster != registers.raster)
        return false;

    if (!vicState.badLine)
        return false;

    if (vicState.badLineDmaStartCycle < 0)
        return false;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    const int baStart = std::max(0, vicState.badLineDmaStartCycle - 3);

    return cycle >= baStart && cycle <= cfg_->DMAEndCycle;
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
        if (cycle == cfg_->spriteFetchSlots[s])
            continue;

        if (isSpriteDataCpuStealCycle(s, cycle))
            return true;
    }

    return false;
}

bool Vic::isSpriteBusAECStealCycle(int raster, int cycle) const
{
    (void)raster;

    for (int s = 0; s < 8; ++s)
    {
        if (!spriteUnits[s].dmaActive)
            continue;

        if (isSpriteDataCpuStealCycle(s, cycle))
            return true;
    }

    return false;
}

bool Vic::isSpriteDataCpuStealCycle(int sprite, int cycle) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    if (!spriteUnits[sprite].dmaActive)
        return false;

    const SpriteFetchPhase phase =  spriteFetchPhaseForCycle(sprite, cycle);

    return spriteFetchPhaseStealsCpu(phase);
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

        const SpriteFetchPhase phase = spriteFetchPhaseForCycle(sprite, cycle);

        switch (phase)
        {
            case SpriteFetchPhase::Pointer:
            case SpriteFetchPhase::Data0:
            case SpriteFetchPhase::Data1:
            case SpriteFetchPhase::Data2:
                return true;

            case SpriteFetchPhase::None:
            default:
                break;
        }
    }

    return false;
}

bool Vic::isBadLine(int raster) const
{
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return false;

    if (!denSeenOn30)
        return false;

    const uint8_t d011 = effectiveD011ForRaster(raster);
    const int yScroll = d011 & 0x07;

    // VIC-II bad lines are only possible in the fixed display window.
    if (raster < 0x30 || raster > 0xF7)
        return false;

    return (raster & 0x07) == yScroll;
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
            slot.spriteIndex = spritePointerFetchSpriteForKind(slot.fetchKind);
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
            slot.spriteIndex = spriteDataFetchSpriteForKind(slot.fetchKind);

            if (slot.spriteIndex >= 0)
                slot.spriteByteIndex = spriteDataByteIndexForCycle(slot.spriteIndex, cycle);

            break;
        }

        case FetchKind::CharMatrix:
        case FetchKind::None:
        default:
            break;
    }

    if (slot.spriteIndex >= 0)
        slot.spriteFetchPhase = spriteFetchPhaseForCycle(slot.spriteIndex, cycle);

    slot.badlineWarning = isBadLineBusWarningCycle(raster, cycle);
    slot.badlineSteal = isBadLineBusStealCycle(raster, cycle);
    slot.badlineBAHold = isBadLineBAHoldCycle(raster, cycle);
    slot.spriteWarning = isSpriteBusWarningCycle(raster, cycle);
    slot.spriteBAHold = isSpriteBusBAHoldCycle(raster, cycle);
    slot.spriteAECSteal = isSpriteBusAECStealCycle(raster, cycle);
    slot.refresh = isRefreshCycle(cycle);

    slot.baLow = slot.badlineBAHold || slot.spriteWarning || slot.spriteBAHold;

    slot.cpuBusStolen = slot.badlineSteal || slot.spriteAECSteal;
    slot.aecLow = slot.cpuBusStolen;
    slot.rasterIrqSample = isRasterIRQCompareCycle(cycle);

    slot.latchRasterState = cycle == 0;
    slot.sampleBadline = cycle == 14;
    slot.startSpriteDmaCheck =  cycle == 15;
    slot.transferDisplayState = cycle == 58;
    slot.startBadlineFetch = cycle == cfg_->DMAStartCycle;
    slot.busOwner = BusOwner::CPU;

    auto fallbackOwner = [&]() -> BusOwner
    {
        if (slot.refresh)
            return BusOwner::Refresh;

        if (slot.cpuBusStolen)
            return BusOwner::Idle;

        return BusOwner::CPU;
    };

    switch (slot.fetchKind)
    {
        case FetchKind::CharMatrix:
        {
            slot.busOwner = BusOwner::BadLine;
            const int index = cycle - cfg_->bgFetchStartCycle;
            slot.matrixFetchIndex = index >= 0 && index < BACKGROUND_MATRIX_COLUMNS ? index : -1;
            break;
        }

        case FetchKind::SpritePtr0:
        case FetchKind::SpritePtr1:
        case FetchKind::SpritePtr2:
        case FetchKind::SpritePtr3:
        case FetchKind::SpritePtr4:
        case FetchKind::SpritePtr5:
        case FetchKind::SpritePtr6:
        case FetchKind::SpritePtr7:
        {
            if (slot.spriteIndex >= 0 && spriteUnits[slot.spriteIndex].dmaActive)
                slot.busOwner = BusOwner::SpritePointer;
            else
                slot.busOwner = fallbackOwner();

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
            if (slot.spriteIndex >= 0 && spriteUnits[slot.spriteIndex].dmaActive)
                slot.busOwner = BusOwner::SpriteData;
            else
                slot.busOwner = fallbackOwner();

            break;
        }

        case FetchKind::None:
        default:
            slot.busOwner = fallbackOwner();

            break;
    }

    return slot;
}

void Vic::beginBadLineFetch()
{
    // VCBASE is advanced when RC wraps from 7 -> 0.
    // Therefore a bad line uses the row already selected by VCBASE
    // instead of advancing VCBASE again here.
    vicState.matrixAdvancePending = false;

    // A bad line resets the row counter.
    vicState.rc = 0;

    // A valid bad line starts/continues display state.
    vicState.displayEnabled = true;
    vicState.displayEnabledNext = true;

    // Matrix state has now definitely been initialized for this raster.
    vicState.badLineInitializedThisRaster = true;

    // The matrix line index starts from the current VCBASE.
    vicState.vmliBase = vicState.vcBase;
    vicState.badLineFetchIndex = 0;

    activeMatrixRow.valid = true;
    activeMatrixRow.vcBase = vicState.vmliBase;
    activeMatrixRow.row = static_cast<int>(vicState.vmliBase / BACKGROUND_MATRIX_COLUMNS);

    activeMatrixRow.screen.fill(0);
    activeMatrixRow.color.fill(0);
    activeMatrixRow.fetched.fill(0);
    activeMatrixRow.invalid.fill(0);
    activeMatrixRow.invalidScreen.fill(0);
    activeMatrixRow.invalidColor.fill(0);
}

void Vic::fetchBadLineMatrixByte(int fetchIndex, int raster)
{
    if (fetchIndex < 0 || fetchIndex >= BACKGROUND_MATRIX_COLUMNS)
        return;

    if (!mem)
        return;

    // VC for this actual matrix fetch.
    const uint16_t vc = static_cast<uint16_t>((vicState.vmliBase + fetchIndex) & 0x03FF);

    // Use the register state that is active at this exact c-access.
    const int fetchX = rasterEventPixelX(currentCycle);

    const uint16_t screenBase = screenBaseForRasterPixelX(raster, fetchX);

    const uint16_t screenAddress = static_cast<uint16_t>(screenBase + vc);

    const uint8_t screenByte = mem->vicRead(screenAddress, raster);

    // Color RAM is selected independently of D018.
    const uint16_t colorAddress = static_cast<uint16_t>(COLOR_MEMORY_START + vc);

    const uint8_t colorByte = static_cast<uint8_t>(mem->vicReadColor(colorAddress) & 0x0F);

    // Successful c-access becomes the current VIC matrix latch.
    cAccessScreenLatch = screenByte;
    cAccessColorLatch = colorByte;
    cAccessLatchValid = true;
    cAccessLatchIndex = fetchIndex;

    charPtrFIFO[fetchIndex] = screenByte;
    colorPtrFIFO[fetchIndex] = colorByte;

    if (activeMatrixRow.valid &&
        activeMatrixRow.vcBase == vicState.vmliBase)
    {
        activeMatrixRow.screen[fetchIndex] = screenByte;
        activeMatrixRow.color[fetchIndex] = colorByte;
        activeMatrixRow.fetched[fetchIndex] = 1;
        activeMatrixRow.invalid[fetchIndex] = 0;
    }
}

void Vic::renderLine(int raster)
{
    if (!sink || !mem)
        return;

    updateGraphicsMode(raster);
    buildBorderMaskLine(raster);

    const graphicsMode mode = graphicsModeForRaster(raster);

    const bool liveBackgroundMode =
        mode == graphicsMode::standard ||
        mode == graphicsMode::multicolor ||
        mode == graphicsMode::bitmap ||
        mode == graphicsMode::multicolorBitmap ||
        mode == graphicsMode::extendedColorText;

    if (!liveBackgroundMode)
        generateBackgroundLine(raster);

    applyBackgroundColorEventsToLine(raster);
    applyExtendedBackgroundColorEventsToLine(raster);
    applySpriteColorEventsToLine(raster);

    composeFinalRasterLine(raster);
    applyBorderColorEventsToFinalLine(raster);
    emitRasterLineInOrder(raster);
}

void Vic::resetBackgroundGraphicsLatches()
{
    for (auto& latch : backgroundGraphicsLatches)
        latch = {};
}

void Vic::fetchStandardTextGraphicsByte(int raster, int column, int fetchX)
{
    if (column < 0 || column >= BACKGROUND_MATRIX_COLUMNS)
        return;

    BackgroundGraphicsLatch& latch = backgroundGraphicsLatches[column];
    latch = {};
    latch.column = column;

    uint8_t screenByte = 0;
    uint8_t colorByte = 0;

    const bool useCAccessLatch = cAccessLatchValid && cAccessLatchIndex == column && vicState.badLine;

    if (useCAccessLatch)
    {
        screenByte = cAccessScreenLatch;
        colorByte = static_cast<uint8_t>(cAccessColorLatch & 0x0F);
    }
    else
    {
        screenByte = fetchDisplayScreenByte(column, raster, fetchX);
        colorByte = fetchDisplayColorByte(column, raster);
    }

    const uint8_t d011 = d011ForRasterPixelX(raster, fetchX, false);
    const uint8_t d016 = d016ForRasterPixelX(raster, fetchX, false);
    const uint8_t d018 = d018ForRasterPixelX(raster, fetchX, false) & 0xFE;

    const graphicsMode mode = graphicsModeFromRegisters(d011, d016);

    uint8_t charIndex = screenByte;

    if (mode == graphicsMode::extendedColorText)
        charIndex &= 0x3F;

    const uint16_t charBase = static_cast<uint16_t>(((d018 >> 1) & 0x07) * 0x0800);
    const uint16_t charAddr = static_cast<uint16_t>(charBase + static_cast<uint16_t>(charIndex) * 8
                                + static_cast<uint16_t>(vicState.rc & 0x07));
    const uint8_t graphicsByte = mem ? mem->vicRead(charAddr, raster) : 0x00;

    updateOpenBus(graphicsByte);

    latch.valid           = true;
    latch.screenByte      = screenByte;
    latch.colorByte       = static_cast<uint8_t>(colorByte & 0x0F);
    latch.graphicsByte    = graphicsByte;
    latch.graphicsAddress = charAddr;

    latch.d011 = d011;
    latch.d016 = d016;
    latch.d018 = d018;
    latch.mode = mode;
}

void Vic::loadActiveStandardTextPixelStateFromLatch(int raster, int column, int px)
{
    resetActiveBackgroundPixelState();

    if (column < 0 || column >= BACKGROUND_MATRIX_COLUMNS)
        return;

    const BackgroundGraphicsLatch& latch = backgroundGraphicsLatches[column];

    if (!latch.valid)
        return;

    // The mode that produced this graphics byte was captured at
    // the actual VIC graphics-fetch cycle.
    const graphicsMode mode = latch.mode;

    activeBgPixel.valid = true;

    activeBgPixel.mode = mode;

    // Multicolor text applies only when the fetch occurred in
    // multicolor text mode and color RAM bit 3 is set.
    activeBgPixel.multicolorText = (mode == graphicsMode::multicolor) && ((latch.colorByte & 0x08) != 0);

    // Graphics data and foreground color come from the fetch latch.
    activeBgPixel.rowBits = latch.graphicsByte;
    activeBgPixel.fg = static_cast<uint8_t>(latch.colorByte & 0x0F);

    // ECM selects one of the four background colors using bits 6-7
    // of the screen matrix byte.
    if (mode == graphicsMode::extendedColorText)
    {
        const uint8_t bgSelect = static_cast<uint8_t>((latch.screenByte >> 6) & 0x03);

        switch (bgSelect)
        {
            case 0:
                activeBgPixel.bg0 = static_cast<uint8_t>(registers.backgroundColor0 & 0x0F);
                activeBgPixel.bg0Source = BackgroundSource::BG0;
                break;

            case 1:
                activeBgPixel.bg0 = static_cast<uint8_t>(registers.backgroundColor[0] & 0x0F);
                activeBgPixel.bg0Source = BackgroundSource::BG1;
                break;

            case 2:
                activeBgPixel.bg0 = static_cast<uint8_t>(registers.backgroundColor[1] & 0x0F);
                activeBgPixel.bg0Source = BackgroundSource::BG2;
                break;

            case 3:
                activeBgPixel.bg0 = static_cast<uint8_t>(registers.backgroundColor[2] & 0x0F);
                activeBgPixel.bg0Source = BackgroundSource::BG3;
                break;
        }
    }
    else
    {
        activeBgPixel.bg0 = static_cast<uint8_t>(registers.backgroundColor0 & 0x0F);
        activeBgPixel.bg0Source = BackgroundSource::BG0;
    }

    // Multicolor text background colors.
    activeBgPixel.bg1 = static_cast<uint8_t>(registers.backgroundColor[0] & 0x0F);
    activeBgPixel.bg2 = static_cast<uint8_t>(registers.backgroundColor[1] & 0x0F);

    activeBgPixel.pxBase = px;
    activeBgPixel.py = fbY(raster);

    activeBgPixel.phase = 0;
}

void Vic::fetchStandardBitmapGraphicsByte(int raster, int column, int fetchX)
{
    if (column < 0 || column >= BACKGROUND_MATRIX_COLUMNS)
        return;

    const uint8_t d011 = d011ForRasterPixelX(raster, fetchX, false);
    const uint8_t d016 = d016ForRasterPixelX(raster, fetchX, false);
    const uint8_t d018 = d018ForRasterPixelX(raster, fetchX, false) & 0xFE;

    const graphicsMode mode = graphicsModeFromRegisters(d011, d016);

    const uint16_t bitmapBase = static_cast<uint16_t>(((d018 >> 3) & 0x01) * 0x2000);
    const uint16_t vc = static_cast<uint16_t>(vicState.vc & 0x03FF);
    const uint8_t rc = static_cast<uint8_t>(vicState.rc & 0x07);

    const uint16_t bitmapAddress = static_cast<uint16_t>(bitmapBase + ((vc & 0x03FF) << 3) + rc);

    const uint8_t graphicsByte = mem ? mem->vicRead(bitmapAddress, raster) : 0x00;

    uint8_t screenByte = 0;
    uint8_t colorByte = 0;

    if (!fetchedMatrixBytesForDisplayCol(column, raster, screenByte, colorByte))
        return;

    updateOpenBus(graphicsByte);

    BackgroundGraphicsLatch& latch = backgroundGraphicsLatches[column];

    latch.valid = true;
    latch.column = column;

    latch.screenByte = screenByte;
    latch.colorByte = static_cast<uint8_t>(colorByte & 0x0F);

    latch.graphicsByte = graphicsByte;
    latch.graphicsAddress = bitmapAddress;

    latch.d011 = d011;
    latch.d016 = d016;
    latch.d018 = d018;
    latch.mode = mode;
}

void Vic::recordRasterColorWrite(uint16_t address, uint8_t oldValue, uint8_t newValue)
{
    if (!(address >= 0xD020 && address <= 0xD02E))
        return;

    RasterColorEvent e;
    e.raster = registers.raster;
    e.cycle = currentCycle;
    e.address = address;
    e.oldValue = oldValue & 0x0F;
    e.newValue = newValue & 0x0F;

    rasterColorEvents.push_back(e);

    recordRasterEventLog(RasterEventKind::Color, address, e.oldValue, e.newValue);
}

void Vic::recordRasterPriorityWrite(uint8_t oldValue, uint8_t newValue)
{
    RasterPriorityEvent e;
    e.raster = registers.raster;
    e.cycle = currentCycle;
    e.oldValue = oldValue;
    e.newValue = newValue;

    rasterPriorityEvents.push_back(e);

    recordRasterEventLog(RasterEventKind::SpritePriority, 0xD01B, oldValue, newValue);
}

void Vic::recordRasterSpriteModeWrite(uint8_t oldValue, uint8_t newValue)
{
    RasterSpriteModeEvent e;
    e.raster = registers.raster;
    e.cycle = currentCycle;
    e.oldValue = oldValue;
    e.newValue = newValue;

    rasterSpriteModeEvents.push_back(e);

    recordRasterEventLog(RasterEventKind::SpriteMode, 0xD01C, oldValue, newValue);
}

void Vic::recordRasterSpriteXExpansionWrite(uint8_t oldValue, uint8_t newValue)
{
    RasterSpriteXExpansionEvent e;
    e.raster = registers.raster;
    e.cycle = currentCycle;
    e.oldValue = oldValue;
    e.newValue = newValue;

    rasterSpriteXExpansionEvents.push_back(e);

    recordRasterEventLog(RasterEventKind::SpriteXExpansion, 0xD01D, oldValue, newValue);
}

void Vic::recordRasterSpriteEnableWrite(uint8_t oldValue, uint8_t newValue)
{
    RasterSpriteEnableEvent e;
    e.raster = registers.raster;
    e.cycle = currentCycle;
    e.oldValue = oldValue;
    e.newValue = newValue;

    rasterSpriteEnableEvents.push_back(e);

    recordRasterEventLog(RasterEventKind::SpriteEnable, 0xD015, oldValue, newValue);
}

void Vic::recordRasterSpriteXWrite(uint16_t address, uint8_t oldValue, uint8_t newValue)
{
    RasterSpriteXEvent e;
    e.raster = registers.raster;
    e.cycle = currentCycle;
    e.address = address;
    e.oldValue = oldValue;
    e.newValue = newValue;

    rasterSpriteXEvents.push_back(e);

    recordRasterEventLog(RasterEventKind::SpriteX, address, oldValue, newValue);
}

void Vic::recordRasterEventLog(RasterEventKind kind, uint16_t address, uint8_t oldValue, uint8_t newValue)
{
    if (oldValue == newValue)
        return;

    RasterEventRecord e;
    e.kind = kind;
    e.raster = registers.raster;
    e.cycle = currentCycle;
    e.address = address;
    e.oldValue = oldValue;
    e.newValue = newValue;

    rasterEventLog.push_back(e);
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
    s.badLine = vicState.badLine;
    s.badLineSampled = vicState.badLineSampled;

    s.d011 = latchedD011ForRaster(raster);
    s.d016 = latchedD016ForRaster(raster);
    s.d018 = latchedD018ForRaster(raster);
}

bool Vic::initialSpritePriorityForRaster(int raster, uint8_t& value) const
{
    for (const RasterPriorityEvent& e : rasterPriorityEvents)
    {
        if (e.raster != raster)
            continue;

        value = e.oldValue;
        return true;
    }

    return false;
}

bool Vic::spriteBehindBackgroundAtPixel(int sprite, int px) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    if (px < 0 || px >= VISIBLE_WIDTH)
        return false;

    uint8_t activePriority = registers.spritePriority;

    if (initialSpritePriorityForRaster(registers.raster, activePriority))
    {
        for (const RasterPriorityEvent& e : rasterPriorityEvents)
        {
            if (e.raster != registers.raster)
                continue;

            const int eventX = rasterEventPixelX(e.cycle);

            if (eventX > px)
                continue;

            activePriority = e.newValue;
        }
    }

    return (activePriority & static_cast<uint8_t>(1u << sprite)) != 0;
}

Vic::SpriteFetchPhase Vic::spriteFetchPhaseForCycle(int sprite, int cycle) const
{
    if (sprite < 0 || sprite >= 8)
        return SpriteFetchPhase::None;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return SpriteFetchPhase::None;

    const int lineCycles = cfg_->cyclesPerLine;
    const int slotStart = spriteFetchSlotStart(sprite);

    if (cycle == slotStart)
        return SpriteFetchPhase::Pointer;

    if (cycle == ((slotStart + 1) % lineCycles))
        return SpriteFetchPhase::Data0;

    if (cycle == ((slotStart + 2) % lineCycles))
        return SpriteFetchPhase::Data1;

    if (cycle == ((slotStart + 3) % lineCycles))
        return SpriteFetchPhase::Data2;

    return SpriteFetchPhase::None;
}

bool Vic::spriteFetchPhaseStealsCpu(SpriteFetchPhase phase) const
{
    const auto phaseIndex = static_cast<uint8_t>(phase);

    if (phaseIndex >= 8)
        return false;

    const uint8_t phaseBit = static_cast<uint8_t>(1u << phaseIndex);

    return (cfg_->spriteCpuStealPhaseMask & phaseBit) != 0;
}

int Vic::firstSpriteCpuStealCycle(int sprite) const
{
    const int lineCycles = cfg_->cyclesPerLine;
    const int slotStart = spriteFetchSlotStart(sprite);

    const std::array<std::pair<SpriteFetchPhase, int>, 4> phases =
    {{
        { SpriteFetchPhase::Pointer, 0 },
        { SpriteFetchPhase::Data0,   1 },
        { SpriteFetchPhase::Data1,   2 },
        { SpriteFetchPhase::Data2,   3 }
    }};

    for (const auto& [phase, offset] : phases)
    {
        if (spriteFetchPhaseStealsCpu(phase))
            return (slotStart + offset) % lineCycles;
    }

    return -1;
}

Vic::BackgroundLineGeometry Vic::computeBackgroundLineGeometry(int raster, int xScroll) const
{
    BackgroundLineGeometry g {};

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return g;

    g.rows = getLatchedRSEL(raster) ? 25 : 24;

    // Hardware-style model:
    // The background sequencer remains 40 matrix columns wide.
    // Horizontal border/CSEL only controls final visibility through
    // borderMaskLine, not whether background pixels are generated.
    g.cols = BACKGROUND_MATRIX_COLUMNS;

    g.charRow = currentCharacterRow();

    if (g.charRow < 0 || g.charRow >= g.rows)
        return g;

    g.fineX = xScroll & 0x07;

    // Always sample/render the full 40-column background row.
    g.fetchCols = BACKGROUND_MATRIX_COLUMNS;

    // Do not clip background stamping to the old per-raster border window.
    // The pixel-aware border mask now decides final visibility.
    g.x0 = 0;
    g.x1 = VISIBLE_WIDTH;

    g.valid = true;
    return g;
}

void Vic::resetActiveBackgroundPixelState()
{
    activeBgPixel.valid = false;
    activeBgPixel.multicolorText = false;

    activeBgPixel.rowBits = 0;

    activeBgPixel.fg = 0;
    activeBgPixel.bg0 = 0;
    activeBgPixel.bg1 = 0;
    activeBgPixel.bg2 = 0;

    activeBgPixel.bg0Source = BackgroundSource::BG0;

    activeBgPixel.pxBase = 0;
    activeBgPixel.py = 0;
    activeBgPixel.phase = 0;
}

void Vic::loadActiveStandardTextPixelState(const TextCellSample& cell, int raster)
{
    (void)raster;

    activeBgPixel.valid = false;

    if (!cell.valid || cell.multicolor)
        return;

    updateOpenBus(cell.rowBits);

    activeBgPixel.valid = true;
    activeBgPixel.rowBits = cell.rowBits;
    activeBgPixel.fg = static_cast<uint8_t>(cell.colorByte & 0x0F);
    activeBgPixel.bg0 = static_cast<uint8_t>(cell.bgColor & 0x0F);
    activeBgPixel.bg0Source = BackgroundSource::BG0;
    activeBgPixel.pxBase = cell.px;
    activeBgPixel.py = cell.py;
    activeBgPixel.phase = 0;
}

Vic::BackgroundPixel Vic::sampleAndAdvanceActiveStandardTextPixel()
{
    BackgroundPixel out {};
    out.color = activeBgPixel.bg0 & 0x0F;
    out.opaque = false;
    out.source = activeBgPixel.bg0Source;

    if (!activeBgPixel.valid)
        return out;

    const int phase = activeBgPixel.phase;

    if (phase < 0 || phase >= 8)
        return out;

    const bool pixelOn = ((activeBgPixel.rowBits >> (7 - phase)) & 0x01) != 0;

    if (pixelOn)
    {
        out.color = activeBgPixel.fg & 0x0F;
        out.opaque = true;
        out.source = BackgroundSource::Foreground;
    }
    else
    {
        out.color = activeBgPixel.bg0 & 0x0F;
        out.opaque = false;
        out.source = activeBgPixel.bg0Source;
    }

    ++activeBgPixel.phase;

    return out;
}

void Vic::loadActiveStandardBitmapPixelStateFromLatch(int raster, int column, int px)
{
    resetActiveBackgroundPixelState();

    if (column < 0 || column >= BACKGROUND_MATRIX_COLUMNS)
        return;

    const BackgroundGraphicsLatch& latch = backgroundGraphicsLatches[column];

    if (!latch.valid)
        return;

    const graphicsMode mode = latch.mode;

    activeBgPixel.valid = true;
    activeBgPixel.mode = mode;
    activeBgPixel.multicolorText = false;

    activeBgPixel.rowBits = latch.graphicsByte;

    activeBgPixel.fg = static_cast<uint8_t>((latch.screenByte >> 4) & 0x0F);

    if (mode == graphicsMode::multicolorBitmap)
    {
        activeBgPixel.bg0 = static_cast<uint8_t>(registers.backgroundColor0 & 0x0F);
        activeBgPixel.bg1 = static_cast<uint8_t>(latch.screenByte & 0x0F);
        activeBgPixel.bg2 = static_cast<uint8_t>(latch.colorByte & 0x0F);
    }
    else
    {
        activeBgPixel.bg0 = static_cast<uint8_t>(latch.screenByte & 0x0F);
        activeBgPixel.bg1 = 0;
        activeBgPixel.bg2 = 0;
    }

    activeBgPixel.pxBase = px;
    activeBgPixel.py = fbY(raster);
    activeBgPixel.phase = 0;
}

Vic::BackgroundPixel Vic::sampleAndAdvanceActiveStandardBitmapPixel()
{
    BackgroundPixel out {};

    if (!activeBgPixel.valid)
        return out;

    const int phase = activeBgPixel.phase;

    if (phase < 0 || phase >= 8)
        return out;

    const bool pixelOn = ((activeBgPixel.rowBits >> (7 - phase)) & 0x01) != 0;

    if (pixelOn)
    {
        out.color = static_cast<uint8_t>(activeBgPixel.fg & 0x0F);
        out.opaque = true;
    }
    else
    {
        out.color = static_cast<uint8_t>(activeBgPixel.bg0 & 0x0F);
        out.opaque = false;
    }

    out.source = BackgroundSource::Bitmap;

    ++activeBgPixel.phase;

    return out;
}

Vic::BackgroundPixel Vic::sampleAndAdvanceActiveMulticolorBitmapPixel()
{
    BackgroundPixel out {};

    if (!activeBgPixel.valid)
        return out;

    const int phase = activeBgPixel.phase;

    if (phase < 0 || phase >= 8)
        return out;

    const int pairIndex = phase / 2;
    const int shift = 6 - (pairIndex * 2);

    const uint8_t value = static_cast<uint8_t>((activeBgPixel.rowBits >> shift) & 0x03);

    switch (value)
    {
        case 0:
            out.color = activeBgPixel.bg0 & 0x0F;
            out.opaque = false;
            out.source = BackgroundSource::BG0;
            break;

        case 1:
            out.color = activeBgPixel.fg & 0x0F;
            out.opaque = true;
            out.source = BackgroundSource::Bitmap;
            break;

        case 2:
            out.color = activeBgPixel.bg1 & 0x0F;
            out.opaque = true;
            out.source = BackgroundSource::Bitmap;
            break;

        case 3:
            out.color = activeBgPixel.bg2 & 0x0F;
            out.opaque = true;
            out.source = BackgroundSource::Bitmap;
            break;
    }

    ++activeBgPixel.phase;

    return out;
}

void Vic::loadBackgroundPipelineFromTextCell(const TextCellSample& cell, int raster, int col)
{
    bgPipeline.valid = true;

    bgPipeline.px = cell.px;
    bgPipeline.py = cell.py;

    bgPipeline.raster = raster;
    bgPipeline.col = col;
    bgPipeline.displayCol = cell.displayCol;
    bgPipeline.yInChar = cell.yInChar;
    bgPipeline.pixelPhase = 0;

    bgPipeline.charCode   = cell.screenByte;
    bgPipeline.screenByte = cell.screenByte;
    bgPipeline.colorByte  = static_cast<uint8_t>(cell.colorByte & 0x0F);
    bgPipeline.bitmapByte = 0;
    bgPipeline.rowBits    = 0;

    // For text modes:
    // - fgColor is the direct cell color for standard text
    // - fgColor low 3 bits are the direct cell color for multicolor text
    bgPipeline.fgColor  = static_cast<uint8_t>(cell.colorByte & 0x0F);
    bgPipeline.bgColor0 = static_cast<uint8_t>(cell.bgColor & 0x0F);
    bgPipeline.bgColor1 = static_cast<uint8_t>(registers.backgroundColor[0] & 0x0F);
    bgPipeline.bgColor2 = static_cast<uint8_t>(registers.backgroundColor[1] & 0x0F);
    bgPipeline.bgColor3 = static_cast<uint8_t>(registers.backgroundColor[2] & 0x0F);

    bgPipeline.multicolor = cell.multicolor;
    bgPipeline.bitmap = false;
    bgPipeline.ecm = false;

    bgPipeline.rowBits = cell.rowBits;
}

void Vic::loadBackgroundPipelineFromBitmapCell(const BitmapCellSample& cell, int raster, int col)
{
    bgPipeline.valid = true;

    bgPipeline.px = cell.px;
    bgPipeline.py = cell.py;

    bgPipeline.raster = raster;
    bgPipeline.col = col;
    bgPipeline.displayCol = cell.displayCol;
    bgPipeline.yInChar = cell.yInChar;
    bgPipeline.pixelPhase = 0;

    bgPipeline.charCode = 0;
    bgPipeline.rowBits = 0;

    bgPipeline.bitmapByte = cell.bitmapByte;
    bgPipeline.screenByte = cell.screenByte;
    bgPipeline.colorByte = cell.colorByte;

    // Standard bitmap only.
    // Multicolor bitmap has its own loader:
    // loadBackgroundPipelineFromMultiColorBitmapCell().
    //
    // Standard bitmap:
    // bit=1 -> high nibble of screen byte
    // bit=0 -> low nibble of screen byte
    bgPipeline.fgColor  = static_cast<uint8_t>((cell.screenByte >> 4) & 0x0F);
    bgPipeline.bgColor0 = static_cast<uint8_t>(cell.screenByte & 0x0F);

    bgPipeline.bgColor1 = 0;
    bgPipeline.bgColor2 = 0;
    bgPipeline.bgColor3 = 0;

    bgPipeline.multicolor = false;
    bgPipeline.bitmap = true;
    bgPipeline.ecm = false;
}

void Vic::loadBackgroundPipelineFromMultiColorBitmapCell(const MultiColorBitmapCellSample& cell, int raster, int col)
{
    bgPipeline.valid = true;

    bgPipeline.px = cell.px;
    bgPipeline.py = cell.py;

    bgPipeline.raster = raster;
    bgPipeline.col = col;
    bgPipeline.displayCol = cell.displayCol;
    bgPipeline.yInChar = cell.yInChar;
    bgPipeline.pixelPhase = 0;

    bgPipeline.charCode = 0;
    bgPipeline.rowBits = 0;

    bgPipeline.bitmapByte = cell.bitmapByte;
    bgPipeline.screenByte = cell.screenByte;
    bgPipeline.colorByte = cell.colorByte;

    bgPipeline.bgColor0 = registers.backgroundColor0 & 0x0F;
    bgPipeline.fgColor  = static_cast<uint8_t>((cell.screenByte >> 4) & 0x0F);
    bgPipeline.bgColor1 = static_cast<uint8_t>(cell.screenByte & 0x0F);
    bgPipeline.bgColor2 = static_cast<uint8_t>(cell.colorByte & 0x0F);
    bgPipeline.bgColor3 = 0;

    bgPipeline.multicolor = true;
    bgPipeline.bitmap = true;
    bgPipeline.ecm = false;
}

void Vic::loadBackgroundPipelineFromECMCell(const ECMCellSample& cell, int raster, int col)
{
    bgPipeline.valid = true;

    bgPipeline.px = cell.px;
    bgPipeline.py = cell.py;

    bgPipeline.raster = raster;
    bgPipeline.col = col;
    bgPipeline.displayCol = cell.displayCol;
    bgPipeline.yInChar = cell.yInChar;
    bgPipeline.pixelPhase = 0;

    bgPipeline.charCode = cell.charIndex;
    bgPipeline.rowBits = cell.rowBits;

    updateOpenBus(bgPipeline.rowBits);

    bgPipeline.bitmapByte = 0;
    bgPipeline.screenByte = 0;
    bgPipeline.colorByte = 0;

    bgPipeline.fgColor  = static_cast<uint8_t>(cell.fgColor & 0x0F);
    bgPipeline.bgColor0 = static_cast<uint8_t>(cell.bgColor & 0x0F);
    bgPipeline.bgColor1 = static_cast<uint8_t>(registers.backgroundColor[0] & 0x0F);
    bgPipeline.bgColor2 = static_cast<uint8_t>(registers.backgroundColor[1] & 0x0F);
    bgPipeline.bgColor3 = static_cast<uint8_t>(registers.backgroundColor[2] & 0x0F);

    bgPipeline.bgSource = cell.bgSource;

    bgPipeline.multicolor = false;
    bgPipeline.bitmap = false;
    bgPipeline.ecm = true;
}

void Vic::resetActiveMatrixRow()
{
    activeMatrixRow.valid = false;
    activeMatrixRow.vcBase = 0;
    activeMatrixRow.row = -1;

    activeMatrixRow.screen.fill(0);
    activeMatrixRow.color.fill(0);
    activeMatrixRow.fetched.fill(0);
    activeMatrixRow.invalid.fill(0);
    activeMatrixRow.invalidScreen.fill(0);
    activeMatrixRow.invalidColor.fill(0);
}

bool Vic::activeMatrixRowByteForDisplayCol(int displayCol, uint8_t& screenByte, uint8_t& colorByte) const
{
    if (displayCol < 0 || displayCol >= BACKGROUND_MATRIX_COLUMNS)
        return false;

    if (!vicState.displayEnabled)
        return false;

    if (!activeMatrixRow.valid)
        return false;

    const uint16_t expectedBase = static_cast<uint16_t>(currentDisplayRowBase());

    if (activeMatrixRow.vcBase != expectedBase)
        return false;

    if (!activeMatrixRow.fetched[displayCol])
        return false;

    screenByte = activeMatrixRow.screen[displayCol];
    colorByte  = static_cast<uint8_t>(activeMatrixRow.color[displayCol] & 0x0F);

    return true;
}

void Vic::resetCAccessLatch()
{
    cAccessScreenLatch = 0;
    cAccessColorLatch = 0;
    cAccessLatchValid = false;
    cAccessLatchIndex = -1;
}

void Vic::resetBackgroundPipeline()
{
    bgPipeline.valid = false;

    bgPipeline.px = 0;
    bgPipeline.py = 0;

    bgPipeline.bitmapByte = 0;
    bgPipeline.screenByte = 0;
    bgPipeline.colorByte = 0;

    bgPipeline.raster = 0;
    bgPipeline.col = 0;
    bgPipeline.displayCol = 0;
    bgPipeline.yInChar = 0;
    bgPipeline.pixelPhase = 0;

    bgPipeline.charCode = 0;
    bgPipeline.rowBits = 0;

    bgPipeline.fgColor = 0;
    bgPipeline.bgColor0 = 0;
    bgPipeline.bgColor1 = 0;
    bgPipeline.bgColor2 = 0;
    bgPipeline.bgColor3 = 0;

    bgPipeline.bgSource = BackgroundSource::BG0;

    bgPipeline.multicolor = false;
    bgPipeline.bitmap = false;
    bgPipeline.ecm = false;
}

void Vic::stampMulticolorTextRowBitsFromPhase(int pxBase, int py, uint8_t rowBits, uint8_t bg0, uint8_t bg1, uint8_t bg2, uint8_t cellColor,
                                              int x0, int x1, int startPhase, int endPhase)
{
    const int begin = std::max(0, startPhase);
    const int end   = std::min(8, endPhase);

    if (begin >= end)
        return;

    for (int phase = begin; phase < end; ++phase)
    {
        const int px = pxBase + phase;
        if (px < x0 || px >= x1)
            continue;

        const int pairIndex = phase >> 1;
        const int shift = 6 - pairIndex * 2;
        const uint8_t bits = static_cast<uint8_t>((rowBits >> shift) & 0x03);

        uint8_t color = bg0 & 0x0F;
        bool opaque = false;
        BackgroundSource source = multicolorTextSourceForBits(bits);

        switch (bits)
        {
            case 0x00:
                color = bg0 & 0x0F;
                opaque = false;
                break;
            case 0x01:
                color = bg1 & 0x0F;
                opaque = true;
                break;
            case 0x02:
                color = bg2 & 0x0F;
                opaque = true;
                break;
            case 0x03:
                color = cellColor & 0x0F;
                opaque = true;
                break;
        }

        stampBackgroundPixelSource(px, py, color, opaque, source);
    }
}

void Vic::stampMulticolorTextPipelineSpan(int pxBase, int py, uint8_t rowBits, uint8_t bg0, uint8_t bg1, uint8_t bg2, uint8_t cellColor,
                                          int x0, int x1, int& phase, int pixelCount)
{
    if (pixelCount <= 0)
        return;

    const int startPhase = std::clamp(phase, 0, 8);
    const int endPhase   = std::clamp(startPhase + pixelCount, 0, 8);

    stampMulticolorTextRowBitsFromPhase(pxBase, py, rowBits, bg0, bg1, bg2, cellColor,
                                        x0, x1, startPhase, endPhase);

    phase = endPhase;
}

Vic::BackgroundSource Vic::multicolorTextSourceForBits(uint8_t bits) const
{
    switch (bits & 0x03)
    {
        case 0x00: return BackgroundSource::BG0;        // $D021
        case 0x01: return BackgroundSource::BG1;        // $D022
        case 0x02: return BackgroundSource::BG2;        // $D023
        case 0x03: return BackgroundSource::Foreground; // color RAM low 3 bits
    }

    return BackgroundSource::Unknown;
}

void Vic::stampStandardBitmapRowBitsFromPhase(int pxBase, int py, uint8_t rowBits, uint8_t fg, uint8_t bg, int x0, int x1,
                                              int startPhase, int endPhase)
{
    const int begin = std::max(0, startPhase);
    const int end   = std::min(8, endPhase);

    if (begin >= end)
        return;

    for (int phase = begin; phase < end; ++phase)
    {
        const int px = pxBase + phase;

        if (px < x0 || px >= x1)
            continue;

        const bool pixelOn = ((rowBits >> (7 - phase)) & 0x01) != 0;

        stampBackgroundPixelSource(
            px,
            py,
            pixelOn ? (fg & 0x0F) : (bg & 0x0F),
            pixelOn,
            BackgroundSource::Bitmap
        );
    }
}

void Vic::stampStandardBitmapPipelineSpan(int pxBase, int py, uint8_t rowBits, uint8_t fg, uint8_t bg, int x0, int x1, int& phase, int pixelCount)
{
    if (pixelCount <= 0)
        return;

    const int startPhase = std::clamp(phase, 0, 8);
    const int endPhase   = std::clamp(startPhase + pixelCount, 0, 8);

    stampStandardBitmapRowBitsFromPhase(pxBase, py, rowBits, fg, bg, x0, x1, startPhase, endPhase);

    phase = endPhase;
}

void Vic::stampMulticolorBitmapRowBitsFromPhase(int pxBase, int py, uint8_t rowBits, uint8_t c00, uint8_t c01, uint8_t c10, uint8_t c11,
                                                int x0, int x1, int startPhase, int endPhase)
{
    const int begin = std::max(0, startPhase);
    const int end   = std::min(8, endPhase);

    if (begin >= end)
        return;

    for (int phase = begin; phase < end; ++phase)
    {
        const int px = pxBase + phase;
        if (px < x0 || px >= x1)
            continue;

        const int pairIndex = phase >> 1;
        const int shift = 6 - pairIndex * 2;
        const uint8_t bits = static_cast<uint8_t>((rowBits >> shift) & 0x03);

        uint8_t color = c00 & 0x0F;
        bool opaque = false;

        switch (bits)
        {
            case 0x00:
                color = c00 & 0x0F;
                opaque = false;
                break;

            case 0x01:
                color = c01 & 0x0F;
                opaque = true;
                break;

            case 0x02:
                color = c10 & 0x0F;
                opaque = true;
                break;

            case 0x03:
                color = c11 & 0x0F;
                opaque = true;
                break;
        }

        stampBackgroundPixelSource(px, py, color, opaque, multicolorBitmapSourceForBits(bits)
        );
    }
}

void Vic::stampMulticolorBitmapPipelineSpan(int pxBase, int py, uint8_t rowBits, uint8_t c00, uint8_t c01, uint8_t c10, uint8_t c11,
                                            int x0, int x1, int& phase, int pixelCount)
{
    if (pixelCount <= 0)
        return;

    const int startPhase = std::clamp(phase, 0, 8);
    const int endPhase   = std::clamp(startPhase + pixelCount, 0, 8);

    stampMulticolorBitmapRowBitsFromPhase(pxBase, py, rowBits, c00, c01, c10, c11, x0, x1, startPhase, endPhase);
    phase = endPhase;
}

Vic::BackgroundSource Vic::multicolorBitmapSourceForBits(uint8_t bits) const
{
    switch (bits & 0x03)
    {
        case 0x00: return BackgroundSource::BG0;    // $D021
        case 0x01: return BackgroundSource::Bitmap; // screen high nibble
        case 0x02: return BackgroundSource::Bitmap; // screen low nibble
        case 0x03: return BackgroundSource::Bitmap; // color RAM
    }

    return BackgroundSource::Unknown;
}

void Vic::stampECMRowBitsFromPhase(int pxBase, int py, uint8_t rowBits, uint8_t fg, uint8_t bg, BackgroundSource bgSource,
                                   int x0, int x1, int startPhase, int endPhase)
{
    const int begin = std::max(0, startPhase);
    const int end   = std::min(8, endPhase);

    if (begin >= end)
        return;

    for (int phase = begin; phase < end; ++phase)
    {
        const int px = pxBase + phase;
        if (px < x0 || px >= x1)
            continue;

        const bool pixelOn = ((rowBits >> (7 - phase)) & 0x01) != 0;

        stampBackgroundPixelSource(px, py, pixelOn ? (fg & 0x0F) : (bg & 0x0F), pixelOn, pixelOn ? BackgroundSource::Foreground : bgSource
        );
    }
}

void Vic::stampECMPipelineSpan(int pxBase, int py, uint8_t rowBits, uint8_t fg, uint8_t bg, BackgroundSource bgSource,
                               int x0, int x1, int& phase, int pixelCount)
{
    if (pixelCount <= 0)
        return;

    const int startPhase = std::clamp(phase, 0, 8);
    const int endPhase   = std::clamp(startPhase + pixelCount, 0, 8);

    stampECMRowBitsFromPhase(pxBase, py, rowBits, fg, bg, bgSource, x0, x1, startPhase, endPhase);
    phase = endPhase;
}

void Vic::stampBackgroundPixelSource(int px, int py, uint8_t color, bool opaque, BackgroundSource source)
{
    (void)py;

    if (px < 0 || px >= 512)
        return;

    bgColorLine[px] = color & 0x0F;
    bgOpaqueLine[px] = opaque ? 1 : 0;
    bgSourceLine[px] = source;
}

bool Vic::sampleTextCell(int raster, int xScroll, int col, TextCellSample& out) const
{
    out = {};

    const int rows = getLatchedRSEL(raster) ? 25 : 24;

    const int charRow = currentCharacterRow();
    if (charRow < 0 || charRow >= rows)
        return false;

    const int yInChar = static_cast<int>(vicState.rc & 0x07);
    const int fine = xScroll & 0x07;

    const int x0 = 0;
    const int x1 = VISIBLE_WIDTH;

    if (col < 0 || col >= BACKGROUND_MATRIX_COLUMNS)
        return false;

    const int px = BACKGROUND_40COL_X0 + fine + col * 8;

    if (px >= x1)
        return false;

    if (px + 8 <= x0)
        return false;

    const int displayCol = col;

    const uint8_t screenByte = resolveDisplayScreenByte(displayCol, raster, px);
    const uint8_t colorByte  = resolveDisplayColorByte(displayCol, raster);

    const uint8_t bgColor = static_cast<uint8_t>(registers.backgroundColor0 & 0x0F);

    const uint8_t d016AtCell = d016ForRasterPixelX(raster, px, false);

    const bool multicolor = ((d016AtCell & 0x10) != 0) && ((colorByte & 0x08) != 0);

    const uint8_t d018 = d018ForRasterPixelX(raster, px, false) & 0xFE;

    const uint16_t charBase = static_cast<uint16_t>(((d018 >> 1) & 0x07) * 0x0800);

    const uint16_t charAddr = static_cast<uint16_t>(charBase + static_cast<uint16_t>(screenByte) * 8 + static_cast<uint16_t>(yInChar & 0x07));

    const uint8_t rowBits = mem ? mem->vicRead(charAddr, raster) : 0x00;

    out.valid = true;
    out.px = px;
    out.py = fbY(raster);
    out.displayCol = displayCol;
    out.yInChar = yInChar;
    out.screenByte = screenByte;
    out.colorByte = static_cast<uint8_t>(colorByte & 0x0F);
    out.bgColor = bgColor;
    out.multicolor = multicolor;

    out.d018 = d018;
    out.charBase = charBase;
    out.charAddr = charAddr;
    out.rowBits = rowBits;

    return true;
}

Vic::BackgroundPixel Vic::sampleAndAdvanceActiveMulticolorTextPixel()
{
    BackgroundPixel out {};

    if (!activeBgPixel.valid)
        return out;

    const int phase = activeBgPixel.phase;

    if (phase < 0 || phase >= 8)
        return out;

    const int pairIndex = phase / 2;
    const int shift = 6 - (pairIndex * 2);

    const uint8_t value = static_cast<uint8_t>((activeBgPixel.rowBits >> shift) & 0x03);

    switch (value)
    {
        case 0:
            out.color = activeBgPixel.bg0;
            out.opaque = false;
            out.source = BackgroundSource::BG0;
            break;

        case 1:
            out.color = activeBgPixel.bg1;
            out.opaque = true;
            out.source = BackgroundSource::BG1;
            break;

        case 2:
            out.color = activeBgPixel.bg2;
            out.opaque = true;
            out.source = BackgroundSource::BG2;
            break;

        case 3:
            out.color = activeBgPixel.fg & 0x07;
            out.opaque = true;
            out.source = BackgroundSource::Foreground;
            break;
    }

    ++activeBgPixel.phase;

    return out;
}

bool Vic::sampleBitmapCell(int raster, int xScroll, int col, BitmapCellSample& out) const
{
    out = {};

    if (!mem)
        return false;

    const int rows = getLatchedRSEL(raster) ? 25 : 24;

    const int charRow = currentCharacterRow();
    if (charRow < 0 || charRow >= rows)
        return false;

    const int yInChar = static_cast<int>(vicState.rc & 0x07);
    const int fine = xScroll & 0x07;

    // Hardware-style display fetch width:
    // CSEL affects border clipping, not the 40-column matrix/bitmap fetch width.
    const int fetchCols = BACKGROUND_MATRIX_COLUMNS;

    const int x0 = 0;
    const int x1 = VISIBLE_WIDTH;

    if (col < 0 || col >= fetchCols)
        return false;

    const int px = BACKGROUND_40COL_X0 + fine + col * 8;

    if (px >= x1)
        return false;

    if (px + 8 <= x0)
        return false;

    const int displayCol = col;

    const uint8_t screenByte = resolveDisplayScreenByte(displayCol, raster, px);
    const uint8_t colorByte  = resolveDisplayColorByte(displayCol, raster);

    const uint16_t cellIndex = static_cast<uint16_t>(charRow * BACKGROUND_MATRIX_COLUMNS + displayCol);
    const uint16_t bitmapBase = getLatchedBitmapBase(raster);
    const uint16_t addr = static_cast<uint16_t>(bitmapBase + cellIndex * 8 + yInChar);
    const uint8_t bitmapByte = mem->vicRead(addr, raster);
    const_cast<Vic*>(this)->updateOpenBus(bitmapByte);

    out.valid = true;
    out.px = px;
    out.py = fbY(raster);
    out.displayCol = displayCol;
    out.yInChar = yInChar;
    out.bitmapByte = bitmapByte;
    out.screenByte = screenByte;
    out.colorByte = colorByte;

    return true;
}

void Vic::drawMulticolorTextCellViaPipeline(const TextCellSample& cell, int raster, int x0, int x1)
{
    (void)raster;

    if (!cell.valid || !cell.multicolor)
        return;

    const uint8_t rowBits = bgPipeline.rowBits;
    const uint8_t bg0 = bgPipeline.bgColor0 & 0x0F;
    const uint8_t bg1 = bgPipeline.bgColor1 & 0x0F;
    const uint8_t bg2 = bgPipeline.bgColor2 & 0x0F;
    const uint8_t cellColor = static_cast<uint8_t>(bgPipeline.fgColor & 0x07);

    int phase = 0;
    stampMulticolorTextPipelineSpan(cell.px, cell.py, rowBits, bg0, bg1, bg2, cellColor, x0, x1, phase, 8);
}

void Vic::renderTextLine(int raster, int xScroll)
{
    const BackgroundLineGeometry g = computeBackgroundLineGeometry(raster, xScroll);
    if (!g.valid)
        return;

    for (int col = 0; col < g.fetchCols; ++col)
    {
        TextCellSample cell {};
        if (!sampleTextCell(raster, xScroll, col, cell))
            continue;

        loadBackgroundPipelineFromTextCell(cell, raster, col);

        if (!cell.multicolor)
        {
            loadActiveStandardTextPixelState(cell, raster);

            while (!activeStandardTextPixelStateFinished())
                emitStandardTextCyclePixelsBudgeted(g.x0, g.x1, 1);
        }
        else
        {
            drawMulticolorTextCellViaPipeline(cell, raster, g.x0, g.x1);
        }
    }
}

void Vic::drawBitmapCellViaPipeline(const BitmapCellSample& cell, int raster, int x0, int x1)
{
    (void)raster;

    if (!cell.valid)
        return;

    const uint8_t rowBits = bgPipeline.bitmapByte;
    const uint8_t fg      = bgPipeline.fgColor & 0x0F;
    const uint8_t bg      = bgPipeline.bgColor0 & 0x0F;

    updateOpenBus(rowBits);

    int phase = 0;
    stampStandardBitmapPipelineSpan(cell.px, cell.py, rowBits, fg, bg, x0, x1, phase, 8);
}

void Vic::renderBitmapLine(int raster, int xScroll)
{
    const BackgroundLineGeometry g = computeBackgroundLineGeometry(raster, xScroll);
    if (!g.valid)
        return;

    for (int col = 0; col < g.fetchCols; ++col)
    {
        BitmapCellSample cell {};
        if (!sampleBitmapCell(raster, xScroll, col, cell))
            continue;

        loadBackgroundPipelineFromBitmapCell(cell, raster, col);
        drawBitmapCellViaPipeline(cell, raster, g.x0, g.x1);
    }
}

bool Vic::sampleMultiColorBitmapCell(int raster, int xScroll, int col, MultiColorBitmapCellSample& out) const
{
    out = {};

    if (!mem)
        return false;

    const int rows = getLatchedRSEL(raster) ? 25 : 24;

    const int charRow = currentCharacterRow();
    if (charRow < 0 || charRow >= rows)
        return false;

    const int yInChar = static_cast<int>(vicState.rc & 0x07);
    const int fine = xScroll & 0x07;

    // Hardware-style display fetch width:
    // CSEL affects border clipping, not the 40-column matrix/bitmap fetch width.
    const int fetchCols = BACKGROUND_MATRIX_COLUMNS;

    const int x0 = 0;
    const int x1 = VISIBLE_WIDTH;

    if (col < 0 || col >= fetchCols)
        return false;

    const int px = BACKGROUND_40COL_X0 + fine + col * 8;

    if (px >= x1)
        return false;

    if (px + 8 <= x0)
        return false;

    const int displayCol = col;

    const uint8_t screenByte = resolveDisplayScreenByte(displayCol, raster, px);
    const uint8_t colorByte  = resolveDisplayColorByte(displayCol, raster);

    const uint16_t cellIndex = static_cast<uint16_t>(charRow * BACKGROUND_MATRIX_COLUMNS + displayCol);
    const uint16_t bitmapBase = getLatchedBitmapBase(raster);
    const uint16_t addr = static_cast<uint16_t>(bitmapBase + cellIndex * 8 + yInChar);

    const uint8_t bitmapByte = mem->vicRead(addr, raster);
    const_cast<Vic*>(this)->updateOpenBus(bitmapByte);

    out.valid = true;
    out.px = px;
    out.py = fbY(raster);
    out.displayCol = displayCol;
    out.yInChar = yInChar;
    out.bitmapByte = bitmapByte;
    out.screenByte = screenByte;
    out.colorByte = colorByte;

    return true;
}

void Vic::drawMultiColorBitmapCellViaPipeline(const MultiColorBitmapCellSample& cell, int raster, int x0, int x1)
{
    (void)raster;

    if (!cell.valid)
        return;

    const uint8_t rowBits = bgPipeline.bitmapByte;
    const uint8_t c00 = bgPipeline.bgColor0 & 0x0F;
    const uint8_t c01 = static_cast<uint8_t>((bgPipeline.screenByte >> 4) & 0x0F);
    const uint8_t c10 = static_cast<uint8_t>(bgPipeline.screenByte & 0x0F);
    const uint8_t c11 = bgPipeline.colorByte & 0x0F;

    updateOpenBus(rowBits);

    int phase = 0;
    stampMulticolorBitmapPipelineSpan(cell.px, cell.py, rowBits, c00, c01, c10, c11, x0, x1, phase, 8);
}

void Vic::renderBitmapMulticolorLine(int raster, int xScroll)
{
    const BackgroundLineGeometry g = computeBackgroundLineGeometry(raster, xScroll);
    if (!g.valid)
        return;

    for (int col = 0; col < g.fetchCols; ++col)
    {
        MultiColorBitmapCellSample cell {};
        if (!sampleMultiColorBitmapCell(raster, xScroll, col, cell))
            continue;

        loadBackgroundPipelineFromMultiColorBitmapCell(cell, raster, col);
        drawMultiColorBitmapCellViaPipeline(cell, raster, g.x0, g.x1);
    }
}

bool Vic::sampleECMCell(int raster, int xScroll, int col, ECMCellSample& out) const
{
    out = {};

    const int rows = getLatchedRSEL(raster) ? 25 : 24;

    const int charRow = currentCharacterRow();
    if (charRow < 0 || charRow >= rows)
        return false;

    const int yInChar = static_cast<int>(vicState.rc & 0x07);
    const int fine = xScroll & 0x07;

    // Hardware-style display fetch width:
    // CSEL affects border clipping, not the 40-column matrix fetch width.
    const int fetchCols = BACKGROUND_MATRIX_COLUMNS;

    const int x0 = 0;
    const int x1 = VISIBLE_WIDTH;

    if (col < 0 || col >= fetchCols)
        return false;

    const int px = BACKGROUND_40COL_X0 + fine + col * 8;

    if (px >= x1)
        return false;

    if (px + 8 <= x0)
        return false;

    const int displayCol = col;

    const uint8_t scrByte   = resolveDisplayScreenByte(displayCol, raster, px);
    const uint8_t colorByte = resolveDisplayColorByte(displayCol, raster);

    // ECM:
    // bits 0-5 = character index
    // bits 6-7 = background color select
    const uint8_t charIndex = static_cast<uint8_t>(scrByte & 0x3F);
    const uint8_t bgSel     = static_cast<uint8_t>((scrByte >> 6) & 0x03);

    const uint16_t charBase = charBaseForRasterPixelX(raster, px);
    const uint16_t charAddr = static_cast<uint16_t>(charBase + static_cast<uint16_t>(charIndex) * 8 + static_cast<uint16_t>(yInChar & 0x07));

    const uint8_t rowBits = mem ? mem->vicRead(charAddr, raster) : 0x00;
    uint8_t bgColor = 0;
    BackgroundSource bgSource = BackgroundSource::BG0;

    switch (bgSel)
    {
        case 0x00:
            bgColor = registers.backgroundColor0 & 0x0F;   // $D021
            bgSource = BackgroundSource::BG0;
            break;

        case 0x01:
            bgColor = getBackgroundColor(0) & 0x0F;        // $D022
            bgSource = BackgroundSource::BG1;
            break;

        case 0x02:
            bgColor = getBackgroundColor(1) & 0x0F;        // $D023
            bgSource = BackgroundSource::BG2;
            break;

        case 0x03:
            bgColor = getBackgroundColor(2) & 0x0F;        // $D024
            bgSource = BackgroundSource::BG3;
            break;
    }

    const uint8_t fgColor = static_cast<uint8_t>(colorByte & 0x0F);

    out.valid = true;
    out.px = px;
    out.py = fbY(raster);
    out.displayCol = displayCol;
    out.yInChar = yInChar;
    out.charIndex = charIndex;
    out.fgColor = fgColor;
    out.bgColor = bgColor;
    out.bgSource = bgSource;
    out.rowBits = rowBits;
    out.charAddr = charAddr;
    out.charBase = charBase;

    return true;
}

void Vic::drawECMCellViaPipeline(const ECMCellSample& cell, int raster, int x0, int x1)
{
    (void)raster;

    if (!cell.valid)
        return;

    const uint8_t rowBits = bgPipeline.rowBits;
    const uint8_t fg      = bgPipeline.fgColor & 0x0F;
    const uint8_t bg      = bgPipeline.bgColor0 & 0x0F;

    int phase = 0;

    stampECMPipelineSpan(cell.px, cell.py, rowBits, fg, bg, bgPipeline.bgSource, x0, x1, phase,8);
}

void Vic::renderECMLine(int raster, int xScroll)
{
    const BackgroundLineGeometry g = computeBackgroundLineGeometry(raster, xScroll);
    if (!g.valid)
        return;

    for (int col = 0; col < g.fetchCols; ++col)
    {
        ECMCellSample cell {};
        if (!sampleECMCell(raster, xScroll, col, cell))
            continue;

        loadBackgroundPipelineFromECMCell(cell, raster, col);
        drawECMCellViaPipeline(cell, raster, g.x0, g.x1);
    }
}

void Vic::clearBadLineFifo()
{
    vicState.vmliFetchIndex = 0;

    for (int i = 0; i < 40; ++i)
    {
        charPtrFIFO[i] = 0;
        colorPtrFIFO[i] = 0;
    }
}

void Vic::clearBackgroundLineBuffers()
{
    bgColorLine.fill(registers.borderColor & 0x0F);
    bgOpaqueLine.fill(0);
    bgSourceLine.fill(BackgroundSource::Border);
}

void Vic::generateBackgroundLine(int raster)
{
    clearBackgroundLineBuffers();
    resetActiveBackgroundPixelState();
    resetBackgroundPipeline();

    const bool DEN = (latchedD011ForRaster(raster) & 0x10) != 0;

    // Border visibility is handled by borderMaskLine.
    if (!DEN)
        return;

    const graphicsMode lineMode = graphicsModeForRaster(raster);

    if (!(lineMode == graphicsMode::bitmap || lineMode == graphicsMode::multicolorBitmap))
    {
        const uint8_t bg = registers.backgroundColor0 & 0x0F;

        for (int px = 0; px < VISIBLE_WIDTH && px < 512; ++px)
        {
            if (!isInnerDisplayPixel(raster, px))
                continue;

            bgColorLine[px] = bg;
            bgOpaqueLine[px] = 0;
            bgSourceLine[px] = BackgroundSource::BG0;
        }
    }

    const int lineXScroll = latchedD016ForRaster(raster) & 0x07;

    switch (lineMode)
    {
        case graphicsMode::standard:
        case graphicsMode::multicolor:
            renderTextLine(raster, lineXScroll);
            break;

        case graphicsMode::bitmap:
            renderBitmapLine(raster, lineXScroll);
            break;

        case graphicsMode::multicolorBitmap:
            renderBitmapMulticolorLine(raster, lineXScroll);
            break;

        case graphicsMode::extendedColorText:
            renderECMLine(raster, lineXScroll);
            break;

        default:
            break;
    }
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

void Vic::emitActiveStandardTextPixels(int x0, int x1, int pixelBudget)
{
    if (!activeBgPixel.valid || pixelBudget <= 0)
        return;

    for (int i = 0; i < pixelBudget; ++i)
    {
        if (activeBgPixel.phase >= 8)
            break;

        const int px = activeBgPixel.pxBase + activeBgPixel.phase;
        const BackgroundPixel pixel = sampleAndAdvanceActiveStandardTextPixel();

        if (px >= x0 && px < x1)
            stampBackgroundPixelSource(px, activeBgPixel.py, pixel.color, pixel.opaque, pixel.source);
    }
}

void Vic::emitStandardTextCyclePixelsBudgeted(int x0, int x1, int pixelBudget)
{
    emitActiveStandardTextPixels(x0, x1, pixelBudget);
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

void Vic::buildBorderMaskLine(int raster)
{
    std::fill(borderMaskLine.begin(), borderMaskLine.begin() + VISIBLE_WIDTH, 1);

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    bool verticalBorder = borderVerticalStart_per_raster[raster] != 0;
    bool horizontalBorder = vicState.horizontalBorder;

    const int open40X = horizontalBorderOpenCompareX(true);
    const int open38X = horizontalBorderOpenCompareX(false);

    const int close40X = horizontalBorderCloseCompareX(true);
    const int close38X = horizontalBorderCloseCompareX(false);

    for (int px = 0; px < VISIBLE_WIDTH; ++px)
    {
        const uint8_t d011AtPixel = d011ForRasterPixelX(raster, px, false);
        const uint8_t d016AtPixel = d016ForRasterPixelX(raster, px, false);

        const bool den = (d011AtPixel & 0x10) != 0;

        const bool rsel25 = (d011AtPixel & 0x08) != 0;
        const bool csel40 = (d016AtPixel & 0x08) != 0;

        const int verticalCompareX = horizontalBorderOpenCompareX(csel40);

        if (px == verticalCompareX)
        {
            const int openRaster = verticalBorderOpenCompareRaster(rsel25);
            const int closeRaster = verticalBorderCloseCompareRaster(rsel25);

            if (raster == closeRaster)
                verticalBorder = true;

            if (raster == openRaster && den)
                verticalBorder = false;
        }

        if (horizontalBorder)
        {
            if (px == open38X && !csel40)
                horizontalBorder = false;

            if (px == open40X && csel40)
                horizontalBorder = false;
        }

        if (!horizontalBorder)
        {
            if (px == close38X && !csel40)
                horizontalBorder = true;

            if (px == close40X && csel40)
                horizontalBorder = true;
        }

        borderMaskLine[px] = (verticalBorder || horizontalBorder) ? 1 : 0;
    }

    vicState.horizontalBorder = horizontalBorder;
}

void Vic::composeFinalRasterLine(int raster)
{
    const int xStart = rasterVisibleStartX(raster);
    const int xEnd   = rasterVisibleEndX(raster);

    for (int px = xStart; px < xEnd; ++px)
        finalColorLine[px] = compositePixelAtX(raster, px);
}

Vic::BackgroundPixel Vic::sampleBackgroundPixelAtX(int raster, int px) const
{
    BackgroundPixel out {};
    out.color = registers.borderColor & 0x0F;
    out.opaque = false;

    if (px < 0 || px >= VISIBLE_WIDTH)
        return out;

    if (borderActiveAtPixel(raster, px))
        return out;

    out.color = bgColorLine[px] & 0x0F;
    out.opaque = (bgOpaqueLine[px] != 0);
    return out;
}

uint8_t Vic::compositePixelAtX(int raster, int px) const
{
    const BackgroundPixel bg = sampleBackgroundPixelAtX(raster, px);

    uint8_t color = bg.color;

    // Sprites behind background:
    // only visible if background is not opaque at this pixel.
    for (int spr = 0; spr < 8; ++spr)
    {
        const bool behind = spriteBehindBackgroundAtPixel(spr, px);
        if (!behind)
            continue;

        if (!spriteOpaqueLine[spr][px])
            continue;

        if (!bg.opaque)
            color = spriteColorLine[spr][px] & 0x0F;
    }

    // Sprites in front of background.
    for (int spr = 0; spr < 8; ++spr)
    {
        const bool behind = (registers.spritePriority & (1 << spr)) != 0;
        if (behind)
            continue;

        if (spriteOpaqueLine[spr][px])
            color = spriteColorLine[spr][px] & 0x0F;
    }

    return color & 0x0F;
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

int Vic::rasterColorEventPixelX(const RasterColorEvent& e) const
{
    return rasterEventPixelX(e.cycle);
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

uint16_t Vic::charBaseForRasterPixelX(int raster, int px) const
{
    const uint8_t d018 = d018ForRasterPixelX(raster, px, false) & 0xFE;
    return static_cast<uint16_t>(((d018 >> 1) & 0x07) * 0x0800);
}

uint16_t Vic::screenBaseForRasterPixelX(int raster, int px) const
{
    const uint8_t d018 = d018ForRasterPixelX(raster, px, false) & 0xFE;
    return static_cast<uint16_t>((d018 & 0xF0) << 6);
}

uint16_t Vic::bitmapBaseForRasterPixelX(int raster, int px) const
{
    const uint8_t d018 = d018ForRasterPixelX(raster, px, false) & 0xFE;
    return static_cast<uint16_t>(((d018 >> 3) & 0x01) * 0x2000);
}

void Vic::applySpriteColorEventsToLine(int raster)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    const int xStart = rasterVisibleStartX(raster);
    const int xEnd   = rasterVisibleEndX(raster);

    auto applyToSpriteRange =
        [&](int sprite, SpriteColorSource source, int startX, int endX, uint8_t color)
        {
            if (sprite < 0 || sprite >= 8)
                return;

            startX = std::clamp(startX, 0, VISIBLE_WIDTH);
            endX   = std::clamp(endX,   0, VISIBLE_WIDTH);

            if (startX >= endX)
                return;

            for (int px = startX; px < endX; ++px)
            {
                if (!spriteOpaqueLine[sprite][px])
                    continue;

                if (spriteColorSourceLine[sprite][px] != source)
                    continue;

                spriteColorLine[sprite][px] = static_cast<uint8_t>(color & 0x0F);
            }
        };

    auto applyToAllSpritesRange =
        [&](SpriteColorSource source, int startX, int endX, uint8_t color)
        {
            for (int sprite = 0; sprite < 8; ++sprite)
                applyToSpriteRange(sprite, source, startX, endX, color);
        };

    // First seed every opaque sprite pixel with the current register colors.
    // This handles the common case where there were no sprite color writes
    // on this raster.
    for (int sprite = 0; sprite < 8; ++sprite)
    {
        applyToSpriteRange(
            sprite,
            SpriteColorSource::SpriteOwnColor,
            xStart,
            xEnd,
            static_cast<uint8_t>(registers.spriteColors[sprite] & 0x0F)
        );
    }

    applyToAllSpritesRange(
        SpriteColorSource::SpriteMultiColor1,
        xStart,
        xEnd,
        static_cast<uint8_t>(registers.spriteMultiColor1 & 0x0F)
    );

    applyToAllSpritesRange(
        SpriteColorSource::SpriteMultiColor2,
        xStart,
        xEnd,
        static_cast<uint8_t>(registers.spriteMultiColor2 & 0x0F)
    );

    auto replayRegisterForSprite =
        [&](uint16_t address, int sprite, SpriteColorSource source)
        {
            uint8_t activeColor = 0;

            // If there was no write to this color register on this raster,
            // the seed pass above is already correct.
            if (!firstRasterColorEventValue(raster, address, activeColor))
                return;

            int startX = xStart;

            for (const RasterColorEvent& e : rasterColorEvents)
            {
                if (e.raster != raster)
                    continue;

                if (e.address != address)
                    continue;

                const int eventX =
                    std::clamp(rasterColorEventPixelX(e), startX, xEnd);

                applyToSpriteRange(sprite, source, startX, eventX, activeColor);

                activeColor = static_cast<uint8_t>(e.newValue & 0x0F);
                startX = eventX;
            }

            applyToSpriteRange(sprite, source, startX, xEnd, activeColor);
        };

    auto replaySharedSpriteRegister =
        [&](uint16_t address, SpriteColorSource source)
        {
            uint8_t activeColor = 0;

            // If there was no write to this shared sprite color register on
            // this raster, the seed pass above is already correct.
            if (!firstRasterColorEventValue(raster, address, activeColor))
                return;

            int startX = xStart;

            for (const RasterColorEvent& e : rasterColorEvents)
            {
                if (e.raster != raster)
                    continue;

                if (e.address != address)
                    continue;

                const int eventX =
                    std::clamp(rasterColorEventPixelX(e), startX, xEnd);

                applyToAllSpritesRange(source, startX, eventX, activeColor);

                activeColor = static_cast<uint8_t>(e.newValue & 0x0F);
                startX = eventX;
            }

            applyToAllSpritesRange(source, startX, xEnd, activeColor);
        };

    // Per-sprite own colors: $D027-$D02E
    for (int sprite = 0; sprite < 8; ++sprite)
    {
        replayRegisterForSprite(
            static_cast<uint16_t>(0xD027 + sprite),
            sprite,
            SpriteColorSource::SpriteOwnColor
        );
    }

    // Shared sprite multicolor registers.
    replaySharedSpriteRegister(0xD025, SpriteColorSource::SpriteMultiColor1);
    replaySharedSpriteRegister(0xD026, SpriteColorSource::SpriteMultiColor2);
}

uint16_t Vic::visibleRasterForIRQCompare() const
{
    if (registers.raster >= cfg_->maxRasterLines)
        return 0;

    return static_cast<uint16_t>(registers.raster);
}

uint16_t Vic::visibleRasterForRead() const
{
    if (registers.raster >= cfg_->maxRasterLines)
        return 0;

    return static_cast<uint16_t>(registers.raster);
}

void Vic::updateIRQLine()
{
    const uint8_t pending =
        (registers.interruptStatus & registers.interruptEnable) & 0x0F;
    const bool any = (pending != 0);

    if (any)
        registers.interruptStatus |= 0x80;
    else
        registers.interruptStatus &= 0x7F;

    if (IRQ)
    {
        if (any)
            IRQ->raiseIRQ(IRQLine::VICII);
        else
            IRQ->clearIRQ(IRQLine::VICII);
    }
}

void Vic::raiseVicIRQSource(uint8_t sourceBitMask)
{
    const uint8_t masked = sourceBitMask & 0x0F;
    if (masked == 0)
        return;

    const uint8_t newlySet = masked & ~registers.interruptStatus;
    if (newlySet == 0)
        return;

    registers.interruptStatus |= newlySet;
    updateIRQLine();
}

void Vic::noteRasterIRQRetargetIfRelevant(uint16_t oldLine, uint16_t newLine)
{
    oldLine &= 0x01FF;
    newLine &= 0x01FF;

    if (oldLine == newLine)
        return;

    // If the new target is outside the current video mode's raster range,
    // it cannot match the current raster.
    if (newLine >= cfg_->maxRasterLines)
        return;

    if (rasterIrqSampledThisLine || currentCycle >= rasterIRQCompareCycle())
        return;

    if (visibleRasterForIRQCompare() != newLine)
        return;
}

void Vic::sampleRasterIRQCompare(const char* reason)
{
    if (rasterIrqSampledThisLine)
        return;

    const char* sampleReason = reason ? reason : "normal-sample";

    // Capture everything used by the comparator at the sample point.
    const uint16_t visibleRaster = visibleRasterForIRQCompare();
    const uint16_t targetRaster = static_cast<uint16_t>(registers.rasterInterruptLine & 0x01FF);
    const bool targetInRange = targetRaster < cfg_->maxRasterLines;
    const bool sampledBefore = rasterIrqSampledThisLine;

    // Use the captured values, not a second helper call.
    const bool matched = targetInRange && (visibleRaster == targetRaster);

    lastRasterIRQSample.valid = true;
    lastRasterIRQSample.raster = static_cast<int>(registers.raster);
    lastRasterIRQSample.cycle = currentCycle;
    lastRasterIRQSample.visibleRaster = visibleRaster;
    lastRasterIRQSample.targetRaster = targetRaster;
    lastRasterIRQSample.targetInRange = targetInRange;
    lastRasterIRQSample.matched = matched;
    lastRasterIRQSample.sampledBefore = sampledBefore;
    lastRasterIRQSample.reason = sampleReason;

    traceVicCycleCheckpoint("raster-irq-sample", registers.raster, currentCycle);
    traceVicRasterRetargetTest(sampleReason, targetRaster, targetRaster, sampledBefore, matched);
    rasterIrqSampledThisLine = true;
    triggerRasterIRQFromSample(matched);
}

void Vic::triggerRasterIRQFromSample(bool matched)
{
    if (!matched)
        return;

    raiseVicIRQSource(0x01);
}

void Vic::setRasterIRQTarget(uint16_t newLine, const char* reason, uint8_t writtenValue)
{
    const uint16_t oldLine = registers.rasterInterruptLine;

    registers.rasterInterruptLine = static_cast<uint16_t>(newLine & 0x01FF);

    noteRasterIRQRetargetIfRelevant(oldLine, registers.rasterInterruptLine);
}

bool Vic::rasterIRQTargetInRange() const
{
    return registers.rasterInterruptLine < cfg_->maxRasterLines;
}

bool Vic::rasterIRQTargetMatchesVisibleRaster() const
{
    if (!rasterIRQTargetInRange())
        return false;

    return visibleRasterForIRQCompare() == registers.rasterInterruptLine;
}

int Vic::rasterIRQCompareCycle() const
{
    return RASTER_IRQ_COMPARE_CYCLE;
}

bool Vic::isRasterIRQCompareCycle(int cycle) const
{
    return cycle == RASTER_IRQ_COMPARE_CYCLE;
}

int Vic::spriteRegisterXForRasterPixel(int sprIndex, int raster, int px) const
{
    if (sprIndex < 0 || sprIndex >= 8)
        return 0;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return registers.spriteX[sprIndex];

    uint8_t xLow = registers.spriteX[sprIndex];
    uint8_t xMsb = registers.spriteX_MSB;

    // If this raster had sprite-X events, start from the old value of the
    // first relevant event. That reconstructs the value that was active
    // before mid-raster writes changed the live register.
    bool seededLow = false;
    bool seededMsb = false;

    for (const RasterSpriteXEvent& e : rasterSpriteXEvents)
    {
        if (e.raster != raster)
            continue;

        if (e.address >= 0xD000 && e.address <= 0xD00E &&
            ((e.address - 0xD000) / 2) == sprIndex &&
            ((e.address - 0xD000) % 2) == 0)
        {
            if (!seededLow)
            {
                xLow = e.oldValue;
                seededLow = true;
            }
        }
        else if (e.address == 0xD010)
        {
            if (!seededMsb)
            {
                xMsb = e.oldValue;
                seededMsb = true;
            }
        }
    }

    // Apply writes that occurred at or before the sampled pixel position.
    for (const RasterSpriteXEvent& e : rasterSpriteXEvents)
    {
        if (e.raster != raster)
            continue;

        const int eventX = rasterEventPixelX(e.cycle);
        if (eventX > px)
            continue;

        if (e.address >= 0xD000 && e.address <= 0xD00E &&
            ((e.address - 0xD000) / 2) == sprIndex &&
            ((e.address - 0xD000) % 2) == 0)
        {
            xLow = e.newValue;
        }
        else if (e.address == 0xD010)
        {
            xMsb = e.newValue;
        }
    }

    int x = static_cast<int>(xLow);
    if (xMsb & (1 << sprIndex))
        x += 256;

    return x;
}

int Vic::spriteScreenXFor(int sprIndex, int raster) const
{
    if (sprIndex < 0 || sprIndex >= 8)
        return 0;

    // Use the beginning of the visible sprite test as the sample point.
    // This prevents end-of-line live X register values from moving the
    // whole sprite after the raster has already been processed.
    const int samplePx = 0;

    const int x =
        spriteRegisterXForRasterPixel(sprIndex, raster, samplePx);

    // Apply VIC-II hardware offset + border.
    return (x - cfg_->hardware_X) + BORDER_SIZE;
}

bool Vic::spriteDisplayCoversRaster(int sprIndex, int raster, int &rowInSprite, int &fbLine) const
{
    rowInSprite = 0;
    fbLine = fbY(raster);

    if (sprIndex < 0 || sprIndex >= 8)
        return false;

    if (!spriteUnits[sprIndex].dmaActive)
        return false;

    const int startY = spriteUnits[sprIndex].startY;
    const bool yExp  = spriteUnits[sprIndex].yExpandLatch;

    int rasterDelta = raster - startY;
    if (rasterDelta < 0)
        rasterDelta += cfg_->maxRasterLines;

    const int spriteHeight = yExp ? 42 : 21;
    if (rasterDelta >= spriteHeight)
        return false;

    const int computedRow = yExp ? (rasterDelta / 2) : rasterDelta;
    rowInSprite = computedRow;

    if (computedRow != spriteUnits[sprIndex].currentRow)
        traceVicSpriteRowMismatch(sprIndex, raster, computedRow);

    return computedRow >= 0 && computedRow < 21;
}

void Vic::latchSpriteSpriteCollision(uint8_t bits, int raster, int firstX)
{
    bits &= 0xFF;
    if (bits == 0)
        return;

    const uint8_t old = registers.spriteCollision;

    registers.spriteCollision =
        static_cast<uint8_t>(registers.spriteCollision | bits);

    const uint8_t newlySet =
        static_cast<uint8_t>(registers.spriteCollision & ~old);

    if (newlySet == 0)
        return;

    lastSpriteSpriteCollision.valid = true;
    lastSpriteSpriteCollision.raster = raster;
    lastSpriteSpriteCollision.x = firstX;
    lastSpriteSpriteCollision.cycle = rasterPixelToCycle(firstX);
    lastSpriteSpriteCollision.bits = newlySet;

    raiseVicIRQSource(0x02);
}

void Vic::latchSpriteBackgroundCollision(uint8_t bits, int raster, int firstX)
{
    bits &= 0xFF;
    if (bits == 0)
        return;

    const uint8_t old = registers.spriteDataCollision;

    registers.spriteDataCollision =
        static_cast<uint8_t>(registers.spriteDataCollision | bits);

    const uint8_t newlySet =
        static_cast<uint8_t>(registers.spriteDataCollision & ~old);

    if (newlySet == 0)
        return;

    lastSpriteBackgroundCollision.valid = true;
    lastSpriteBackgroundCollision.raster = raster;
    lastSpriteBackgroundCollision.x = firstX;
    lastSpriteBackgroundCollision.cycle = rasterPixelToCycle(firstX);
    lastSpriteBackgroundCollision.bits = newlySet;

    raiseVicIRQSource(0x04);
}

void Vic::latchSpriteBackgroundCollisionsAtPixel(int raster, int px)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    if (px < 0 || px >= VISIBLE_WIDTH)
        return;

    if (bgOpaqueLine[px] == 0)
        return;

    uint8_t bits = 0;

    for (int spr = 0; spr < 8; ++spr)
    {
        if (!spriteOpaqueLine[spr][px])
            continue;

        bits = static_cast<uint8_t>(bits | (1 << spr));
    }

    if (bits != 0)
        latchSpriteBackgroundCollision(bits, raster, px);
}

Vic::graphicsMode Vic::graphicsModeFromRegisters(uint8_t d011, uint8_t d016) const
{
    const bool MCM = (d016 & 0x10) != 0;
    const bool BMM = (d011 & 0x20) != 0;
    const bool ECM = (d011 & 0x40) != 0;

    if (!BMM && !MCM && !ECM)
        return graphicsMode::standard;

    if (!BMM && MCM && !ECM)
        return graphicsMode::multicolor;

    if (!BMM && !MCM && ECM)
        return graphicsMode::extendedColorText;

    if (BMM && !MCM && !ECM)
        return graphicsMode::bitmap;

    if (BMM && MCM && !ECM)
        return graphicsMode::multicolorBitmap;

    return graphicsMode::invalid;
}

Vic::graphicsMode Vic::graphicsModeForRaster(int raster) const
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return currentMode;

    const uint8_t d011 = latchedD011ForRaster(raster);
    const uint8_t d016 = latchedD016ForRaster(raster);

    return graphicsModeFromRegisters(d011, d016);
}

Vic::graphicsMode Vic::graphicsModeForRasterPixel(int raster, int px, bool preferPreviousFrame) const
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return currentMode;

    const uint8_t d011 = d011ForRasterPixelX(raster, px, preferPreviousFrame);
    const uint8_t d016 = d016ForRasterPixelX(raster, px, preferPreviousFrame);

    return graphicsModeFromRegisters(d011, d016);
}

void Vic::updateGraphicsMode(int raster)
{
    currentMode = graphicsModeForRaster(raster);
}

void Vic::innerWindowForRaster(int raster, int& x0, int& x1) const
{
    x0 = 0;
    x1 = 0;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    if (borderVertical_per_raster[raster] != 0)
        return;

    int first = -1;
    int last = -1;

    for (int px = 0; px < VISIBLE_WIDTH; ++px)
    {
        if (borderMaskLine[px] == 0)
        {
            if (first < 0)
                first = px;

            last = px + 1;
        }
    }

    if (first < 0 || last <= first)
        return;

    x0 = first;
    x1 = last;
}

uint8_t Vic::fetchScreenByte(int row, int col, int raster) const
{
    if (!mem)
        return 0x00;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        raster = registers.raster;

    row = std::clamp(row, 0, 24);
    col = std::clamp(col, 0, BACKGROUND_MATRIX_COLUMNS - 1);

    // Legacy helper:
    // Display rendering should prefer resolveDisplayScreenByte() /
    // fetchDisplayScreenByte(), because those can use D018 pixel-event timing.
    const uint16_t address =
        static_cast<uint16_t>(
            getLatchedScreenBase(raster) +
            static_cast<uint16_t>(row * BACKGROUND_MATRIX_COLUMNS + col)
        );

    return mem->vicRead(address, raster);
}

uint8_t Vic::fetchColorByte(int row, int col, int raster) const
{
    if (!mem)
        return 0x00;

    (void)raster;

    row = std::clamp(row, 0, 24);
    col = std::clamp(col, 0, BACKGROUND_MATRIX_COLUMNS - 1);

    const uint16_t address =
        static_cast<uint16_t>(
            COLOR_MEMORY_START +
            static_cast<uint16_t>(row * BACKGROUND_MATRIX_COLUMNS + col)
        );

    return mem->vicReadColor(address);
}

int Vic::currentDisplayRowBase() const
{
    // When display is active, use the row latched at bad-line start.
    if (vicState.displayEnabled)
        return static_cast<int>(vicState.vmliBase);

    return static_cast<int>(vicState.vcBase);
}

uint8_t Vic::fetchDisplayScreenByte(int col, int raster, int px) const
{
    if (!mem)
        return 0x00;

    int row = 0;
    int c = 0;

    currentDisplayRowCol(col, row, c);

    if (c < 0)
        c = 0;

    if (c >= BACKGROUND_MATRIX_COLUMNS)
        c = BACKGROUND_MATRIX_COLUMNS - 1;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        raster = registers.raster;

    if (px < 0 || px >= VISIBLE_WIDTH)
    {
        const uint8_t d016 = d016ForRasterPixelX(raster, 0, false);
        const int fine = static_cast<int>(d016 & 0x07);
        px = BACKGROUND_40COL_X0 + fine + c * 8;
    }

    const uint16_t screenBase = screenBaseForRasterPixelX(raster, px);
    const uint16_t address = static_cast<uint16_t>(screenBase + static_cast<uint16_t>(row * BACKGROUND_MATRIX_COLUMNS + c));

    return mem->vicRead(address, raster);
}

uint8_t Vic::fetchDisplayColorByte(int col, int raster) const
{
    int row = 0;
    int c = 0;
    currentDisplayRowCol(col, row, c);
    return fetchColorByte(row, c, raster) & 0x0F;
}

bool Vic::shouldUseFetchedMatrixForDisplayCol(int displayCol, int raster) const
{
    if (displayCol < 0 || displayCol >= BACKGROUND_MATRIX_COLUMNS)
        return false;

    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return false;

    // Only trust matrix bytes while display progression is active.
    if (!vicState.displayEnabled)
        return false;

    // Avoid using stale matrix data outside the active display window.
    if (!rasterWithinVerticalDisplayWindow(raster))
        return false;

    // The matrix cache/FIFO is only meaningful after display has actually
    // started from a real badline this frame.
    if (!denSeenOn30 || firstBadlineY < 0)
        return false;

    return true;
}

bool Vic::fetchedMatrixBytesForDisplayCol(int displayCol, int raster, uint8_t& screenByte, uint8_t& colorByte) const
{
    if (!shouldUseFetchedMatrixForDisplayCol(displayCol, raster))
        return false;

    if (activeMatrixRowByteForDisplayCol(displayCol, screenByte, colorByte))
        return true;

    return false;
}

uint8_t Vic::resolveDisplayScreenByte(int displayCol, int raster, int px) const
{
    uint8_t screenByte = 0;
    uint8_t colorByte = 0;

    if (fetchedMatrixBytesForDisplayCol(displayCol, raster, screenByte, colorByte))
        return screenByte;

    return fetchDisplayScreenByte(displayCol, raster, px);
}

uint8_t Vic::resolveDisplayColorByte(int displayCol, int raster) const
{
    uint8_t screenByte = 0;
    uint8_t colorByte = 0;

    if (fetchedMatrixBytesForDisplayCol(displayCol, raster, screenByte, colorByte))
        return static_cast<uint8_t>(colorByte & 0x0F);

    return static_cast<uint8_t>(fetchDisplayColorByte(displayCol, raster) & 0x0F);
}

void Vic::advanceCharacterSequencerEndOfLine(int raster)
{
    if (!vicState.displayEnabledNext)
    {
        vicState.displayEnabled = false;
        vicState.matrixAdvancePending = false;
        return;
    }

    vicState.displayEnabled = true;

    const int visibleRows = getLatchedRSEL(raster) ? 25 : 24;

    const int currentRowBefore = currentCharacterRow();

    if (currentRowBefore < 0 || currentRowBefore >= visibleRows)
    {
        vicState.displayEnabled = false;
        vicState.displayEnabledNext = false;
        vicState.matrixAdvancePending = false;

        clearBadLineFifo();
        return;
    }

    vicState.rc = static_cast<uint8_t>((vicState.rc + 1) & 0x07);

    if (vicState.rc == 0)
    {
        const uint16_t nextVcBase = static_cast<uint16_t>(vicState.vc & 0x03FF);
        const int nextRow = static_cast<int>(nextVcBase / BACKGROUND_MATRIX_COLUMNS);

        if (nextRow >= visibleRows)
        {
            vicState.displayEnabled = false;
            vicState.displayEnabledNext = false;
            vicState.matrixAdvancePending = false;

            clearBadLineFifo();
            return;
        }

        // VC after the final g-access points at the start of the
        // next character row. Make that the new VCBASE immediately.
        vicState.vcBase = nextVcBase;

        // The cached matrix row still belongs to the old character row.
        // A future bad line will populate a new one.
        vicState.matrixAdvancePending = true;
    }

    const bool den = (latchedD011ForRaster(raster) & 0x10) != 0;

    if (!denSeenOn30 || firstBadlineY < 0 || !den)
    {
        vicState.displayEnabled = false;
        vicState.displayEnabledNext = false;
        vicState.matrixAdvancePending = false;

        clearBadLineFifo();
    }
}

int Vic::currentCharacterRow() const
{
    int row = 0;
    int col = 0;
    currentDisplayRowCol(0, row, col);
    return row;
}

void Vic::currentDisplayRowCol(int displayCol, int& row, int& col) const
{
    const int vc = currentDisplayRowBase() + displayCol;
    row = vc / 40;
    col = vc % 40;
}

void Vic::updateVerticalBorderState(int raster)
{
    applyVerticalBorderCompare(raster, registers.control);
}

void Vic::updateHorizontalBorderState(int raster)
{
    const bool csel40 = getLatchedCSEL(raster);

    const HorizontalBorderWindow w =
        horizontalBorderWindowForCSEL(csel40);

    vicState.leftBorderOpenX = w.openX;
    vicState.rightBorderCloseX = w.closeX;

    vicState.leftBorder  = false;
    vicState.rightBorder = false;

    if (vicState.leftBorderOpenX >= vicState.rightBorderCloseX)
    {
        vicState.leftBorder  = true;
        vicState.rightBorder = true;
    }
}

bool Vic::rasterWithinVerticalDisplayWindow(int raster) const
{
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return false;

    const VerticalBorderWindow w = verticalBorderWindowForRaster(raster);

    return raster >= w.topOpen && raster <= w.bottomClose;
}

bool Vic::borderActiveAtPixel(int raster, int px) const
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return true;

    if (px < 0 || px >= VISIBLE_WIDTH)
        return true;

    return borderMaskLine[px] != 0;
}

void Vic::applyVerticalBorderCompare(int raster, uint8_t d011)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    const bool den = (d011 & 0x10) != 0;

    const bool rsel25 = (d011 & 0x08) != 0;

    const int openCompareRaster = verticalBorderOpenCompareRaster(rsel25);
    const int closeCompareRaster = verticalBorderCloseCompareRaster(rsel25);

    if (raster == closeCompareRaster)
    {
        vicState.verticalBorder = true;
        vicState.bottomBorderCloseRaster = raster;
    }

    if (raster == openCompareRaster && den)
    {
        vicState.verticalBorder = false;

        if (vicState.topBorderOpenRaster < 0)
            vicState.topBorderOpenRaster = raster;
    }
}

uint8_t Vic::latchOpenBus(uint8_t value)
{
    if (dataBus)
        dataBus->drive(value, DataBusLatch::Driver::VIC);

    return value;
}

uint8_t Vic::getOpenBus() const
{
    return dataBus ? dataBus->sample() : 0xFF;
}

uint8_t Vic::latchOpenBusMasked(uint8_t definedBits, uint8_t definedMask)
{
    const uint8_t floatingBits = getOpenBus();

    const uint8_t value = static_cast<uint8_t>((floatingBits & static_cast<uint8_t>(~definedMask)) | (definedBits & definedMask));

    if (dataBus)
        dataBus->drive(value, DataBusLatch::Driver::VIC);

    return value;
}

void Vic::latchNextRasterDD00()
{
    const int raster = registers.raster;
    const uint16_t nextRaster = (raster + 1) % cfg_->maxRasterLines;

    dd00_per_raster[nextRaster] = cia2 ? cia2->getCurrentVICBank() : 0;
}

void Vic::updateOpenBus(uint8_t value)
{
    if (dataBus)
        dataBus->drive(value, DataBusLatch::Driver::VIC);
}

void Vic::performIdleFetchForCurrentCycle()
{
    if (!mem)
        return;

    const uint16_t addr = IDLE_FETCH_ADDRESS;
    const uint8_t value = mem->vicRead(addr, registers.raster);

    updateOpenBus(value);
}

uint8_t Vic::d019Read() const
{
    const uint8_t src = registers.interruptStatus & 0x0F;
    const uint8_t irq = ((src & registers.interruptEnable & 0x0F) != 0) ? 0x80 : 0x00;
    return irq | src;
}

std::string Vic::decodeModeName() const
{
    const uint8_t d011 = effectiveD011ForRaster(registers.raster);
    const uint8_t d016 = effectiveD016ForRaster(registers.raster);

    const bool ecm = (d011 & 0x40) != 0;
    const bool bmm = (d011 & 0x20) != 0;
    const bool mcm = (d016 & 0x10) != 0;

    if (!bmm && !mcm && !ecm) return "Text";
    if (ecm && !bmm && !mcm)  return "ECM (Extended Color Mode)";
    if (!bmm && mcm)          return "Multicolor Text";
    if (bmm && !mcm)          return "Bitmap";
    if (bmm && mcm)           return "Multicolor Bitmap";
    return "Unknown";
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

    const uint16_t bankBase = dd00_per_raster[raster];

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

Vic::VicCycleDebugSnapshot Vic::getCycleDebugSnapshot(int raster, int cycle) const
{
    VicCycleDebugSnapshot s {};

    s.requestedRaster = raster;
    s.requestedCycle = cycle;

    s.currentRaster = static_cast<int>(registers.raster);
    s.currentCycle = currentCycle;
    s.liveSample = (raster == s.currentRaster && cycle == s.currentCycle);

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
    {
        s.valid = false;
        s.error = "Invalid raster: " + std::to_string(raster) + "\n";
        return s;
    }

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
    {
        s.valid = false;
        s.error = "Invalid cycle: " + std::to_string(cycle) + "\n";
        return s;
    }

    s.valid = true;
    s.slot = cycleSlotFor(raster, cycle);

    s.rasterIrqTarget = registers.rasterInterruptLine & 0x01FF;
    s.rasterIrqTargetInRange = s.rasterIrqTarget < cfg_->maxRasterLines;

    s.rasterIrqCompareMatch = s.rasterIrqTargetInRange && static_cast<uint16_t>(raster) == s.rasterIrqTarget;

    s.rasterIrqEnabled = (registers.interruptEnable & 0x01) != 0;
    s.rasterIrqPending = (registers.interruptStatus & 0x01) != 0;
    s.irqLineActiveNow = irqLineActive();

    s.badLine = (raster == registers.raster) ? vicState.badLineSampled : isBadLine(raster);

    s.denAtRaster = (d011_per_raster[raster] & 0x10) != 0;
    s.denSeenOn30 = denSeenOn30;

    s.liveVc = vicState.vc;
    s.liveBadLine = vicState.badLine;
    s.badLineDmaStartCycle = vicState.badLineDmaStartCycle;
    s.badLineFetchIndex = vicState.badLineFetchIndex;

    s.cAccessScreenLatch = cAccessScreenLatch;
    s.cAccessColorLatch = cAccessColorLatch;
    s.cAccessLatchValid = cAccessLatchValid;
    s.cAccessLatchIndex = cAccessLatchIndex;

    s.liveVcBase = vicState.vcBase;
    s.liveVmliFetchIndex = vicState.vmliFetchIndex;
    s.liveRc = vicState.rc;
    s.liveDisplayRow = currentCharacterRow();

    s.fineY = fineYScroll(raster);
    s.fineX = fineXScroll(raster);

    if (s.slot.spriteIndex >= 0 && s.slot.spriteIndex < 8)
    {
        const auto& sp = spriteUnits[s.slot.spriteIndex];

        s.sprite.valid = true;
        s.sprite.active = sp.dmaActive;
        s.sprite.rowLatched = sp.rowDataLatched;
        s.sprite.mc = sp.mc;
        s.sprite.mcBase = sp.mcBase;
        s.sprite.row = spriteRowFromMCBase(s.slot.spriteIndex);
        s.sprite.currentRow = sp.currentRow;
        s.sprite.pointerByte = sp.pointerByte;
        s.sprite.dataBase = sp.dataBase;
    }

    for (int i = 0; i < 8; ++i)
    {
        s.spriteDmaActive[i] = spriteUnits[i].dmaActive;
        s.spriteRowLatched[i] = spriteUnits[i].rowDataLatched;
    }

    return s;
}

Vic::VicSpriteDebugSnapshot Vic::getSpriteDebugSnapshot() const
{
    VicSpriteDebugSnapshot snap {};

    snap.currentRaster = static_cast<int>(registers.raster);
    snap.currentCycle = currentCycle;

    snap.d015 = registers.spriteEnabled;
    snap.d017 = registers.spriteYExpansion;
    snap.d01b = registers.spritePriority;
    snap.d01c = registers.spriteMultiColor;
    snap.d01d = registers.spriteXExpansion;

    for (int i = 0; i < 8; ++i)
    {
        auto& d = snap.sprites[i];
        const auto& s = spriteUnits[i];

        d.enabled = (registers.spriteEnabled & (1 << i)) != 0;

        d.y = registers.spriteY[i];
        d.x = registers.spriteX[i] |
              ((registers.spriteX_MSB & (1 << i)) ? 0x100 : 0);

        d.dmaActive = s.dmaActive;
        d.rowDataLatched = s.rowDataLatched;
        d.yExpandLatch = s.yExpandLatch;

        d.mc = s.mc;
        d.mcBase = s.mcBase;

        d.row = spriteRowFromMCBase(i);
        d.currentRow = s.currentRow;

        d.pointerByte = s.pointerByte;
        d.dataBase = s.dataBase;

        d.shift0 = s.shift0;
        d.shift1 = s.shift1;
        d.shift2 = s.shift2;

        d.rowPrepared = s.rowPrepared;

        d.outputXStart = s.outputXStart;
        d.outputWidth = s.outputWidth;
        d.outputBit = s.outputBit;
        d.outputRepeat = s.outputRepeat;
    }

    snap.spriteSpriteCollision.valid = lastSpriteSpriteCollision.valid;
    snap.spriteSpriteCollision.raster = lastSpriteSpriteCollision.raster;
    snap.spriteSpriteCollision.x = lastSpriteSpriteCollision.x;
    snap.spriteSpriteCollision.cycle = lastSpriteSpriteCollision.cycle;
    snap.spriteSpriteCollision.bits = lastSpriteSpriteCollision.bits;

    snap.spriteBackgroundCollision.valid = lastSpriteBackgroundCollision.valid;
    snap.spriteBackgroundCollision.raster = lastSpriteBackgroundCollision.raster;
    snap.spriteBackgroundCollision.x = lastSpriteBackgroundCollision.x;
    snap.spriteBackgroundCollision.cycle = lastSpriteBackgroundCollision.cycle;
    snap.spriteBackgroundCollision.bits = lastSpriteBackgroundCollision.bits;

    return snap;
}

Vic::VicRegisterDebugSnapshot Vic::getRegisterDebugSnapshot() const
{
    VicRegisterDebugSnapshot s {};

    s.currentRaster = registers.raster;
    s.currentCycle = currentCycle;

    for (int i = 0; i < 8; ++i)
    {
        s.spriteX[i] = registers.spriteX[i];
        s.spriteY[i] = registers.spriteY[i];
        s.spriteColors[i] = registers.spriteColors[i] & 0x0F;
    }

    s.spriteXMsb = registers.spriteX_MSB;

    s.spriteEnabled = registers.spriteEnabled;
    s.spriteYExpansion = registers.spriteYExpansion;
    s.spritePriority = registers.spritePriority;
    s.spriteMultiColor = registers.spriteMultiColor;
    s.spriteXExpansion = registers.spriteXExpansion;

    s.control = registers.control & 0x7F;
    s.control2 = registers.control2;
    s.memoryPointer = registers.memory_pointer & 0xFE;
    s.rasterInterruptLine = registers.rasterInterruptLine & 0x01FF;

    s.interruptStatus = registers.interruptStatus;
    s.interruptEnable = registers.interruptEnable;
    s.irqLineActive = irqLineActive();
    s.rasterIrqSampledThisLine = rasterIrqSampledThisLine;
    s.rasterIrqCompareCycle = rasterIRQCompareCycle();
    s.rasterCompareMatchesNow = rasterCompareMatchesNow();
    s.rasterIrqTargetInRange = rasterIRQTargetInRange();

    s.spriteCollision = registers.spriteCollision;
    s.spriteDataCollision = registers.spriteDataCollision;

    s.borderColor = registers.borderColor & 0x0F;
    s.backgroundColor0 = registers.backgroundColor0 & 0x0F;

    for (int i = 0; i < 3; ++i)
        s.backgroundColor[i] = registers.backgroundColor[i] & 0x0F;

    s.spriteMultiColor1 = registers.spriteMultiColor1 & 0x0F;
    s.spriteMultiColor2 = registers.spriteMultiColor2 & 0x0F;

    s.lightPenX = registers.light_pen_X;
    s.lightPenY = registers.light_pen_Y;
    s.undefinedReg = registers.undefined;

    const int r = std::clamp<int>(registers.raster, 0, static_cast<int>(cfg_->maxRasterLines) - 1);

    s.latchedD011 = d011_per_raster[r];
    s.latchedD016 = d016_per_raster[r];
    s.latchedD018 = d018_per_raster[r];
    s.latchedDD00 = dd00_per_raster[r];

    s.charBase = charBaseCache;
    s.screenBase = screenBaseCache;
    s.bitmapBase = bitmapBaseCache;
    s.vicBankBase = cia2 ? cia2->getCurrentVICBank() : s.latchedDD00;

    s.lastRasterIrqSample.valid = lastRasterIRQSample.valid;
    s.lastRasterIrqSample.raster = lastRasterIRQSample.raster;
    s.lastRasterIrqSample.cycle = lastRasterIRQSample.cycle;
    s.lastRasterIrqSample.visibleRaster = lastRasterIRQSample.visibleRaster;
    s.lastRasterIrqSample.targetRaster = lastRasterIRQSample.targetRaster;
    s.lastRasterIrqSample.targetInRange = lastRasterIRQSample.targetInRange;
    s.lastRasterIrqSample.matched = lastRasterIRQSample.matched;
    s.lastRasterIrqSample.sampledBefore = lastRasterIRQSample.sampledBefore;
    s.lastRasterIrqSample.reason = lastRasterIRQSample.reason;

    // Border/window debug state.
    s.liveVerticalBorder = vicState.verticalBorder;
    s.liveLeftBorder = vicState.leftBorder;
    s.liveRightBorder = vicState.rightBorder;

    s.liveLeftBorderOpenX = vicState.leftBorderOpenX;
    s.liveRightBorderCloseX = vicState.rightBorderCloseX;

    s.latchedVerticalBorder = borderVertical_per_raster[r] != 0;
    s.latchedBorderOpenX = borderLeftOpenX_per_raster[r];
    s.latchedBorderCloseX = borderRightCloseX_per_raster[r];

    innerWindowForRaster(r, s.maskInnerX0, s.maskInnerX1);

    const VerticalBorderWindow vw = verticalBorderWindowForRaster(r);
    s.verticalTopOpen = vw.topOpen;
    s.verticalBottomClose = vw.bottomClose;

    s.withinVerticalDisplayWindow = rasterWithinVerticalDisplayWindow(r);

    return s;
}

Vic::VicBadlineDebugSnapshot Vic::getBadlineDebugSnapshot() const
{
    VicBadlineDebugSnapshot s {};

    s.raster = registers.raster;
    s.cycle = currentCycle;

    s.badLine = vicState.badLine;
    s.badLineSampled = vicState.badLineSampled;

    s.displayEnabled = vicState.displayEnabled;
    s.displayEnabledNext = vicState.displayEnabledNext;

    s.denSeenOn30 = denSeenOn30;
    s.firstBadlineY = firstBadlineY;

    s.vcBase = vicState.vcBase;
    s.vmliBase = vicState.vmliBase;
    s.vmliFetchIndex = vicState.vmliFetchIndex;
    s.rc = vicState.rc;

    return s;
}

void Vic::rebuildBorderRasterLatches()
{
    if ((int)borderVertical_per_raster.size() != cfg_->maxRasterLines)
        borderVertical_per_raster.assign(cfg_->maxRasterLines, 1);

    if ((int)borderVerticalStart_per_raster.size() != cfg_->maxRasterLines)
        borderVerticalStart_per_raster.assign(cfg_->maxRasterLines, 1);

    if ((int)borderLeftOpenX_per_raster.size() != cfg_->maxRasterLines)
        borderLeftOpenX_per_raster.assign(cfg_->maxRasterLines, 0);

    if ((int)borderRightCloseX_per_raster.size() != cfg_->maxRasterLines)
        borderRightCloseX_per_raster.assign(cfg_->maxRasterLines, VISIBLE_WIDTH);

    bool verticalBorder = true;

    for (int r = 0; r < cfg_->maxRasterLines; ++r)
    {
        borderVerticalStart_per_raster[r] = verticalBorder ? 1 : 0;

        const uint8_t d011 = latchedD011ForRaster(r);
        const bool den = (d011 & 0x10) != 0;
        const bool rsel25 = (d011 & 0x08) != 0;

        const int openCompareRaster = verticalBorderOpenCompareRaster(rsel25);
        const int closeCompareRaster = verticalBorderCloseCompareRaster(rsel25);

        if (r == closeCompareRaster)
            verticalBorder = true;

        if (r == openCompareRaster && den)
            verticalBorder = false;

        borderVertical_per_raster[r] = verticalBorder ? 1 : 0;

        updateHorizontalBorderState(r);

        borderLeftOpenX_per_raster[r] = static_cast<int16_t>(vicState.leftBorderOpenX);
        borderRightCloseX_per_raster[r] = static_cast<int16_t>(vicState.rightBorderCloseX);
    }
}

void Vic::updateMonitorCaches(int raster)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        raster = registers.raster;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        raster = 0;

    const uint16_t currentVICBank = dd00_per_raster[raster];

    // Use a representative visible display X for monitor/debug cache reporting.
    // Rendering itself remains pixel-aware through charBaseForRasterPixelX(),
    // screenBaseForRasterPixelX(), and bitmapBaseForRasterPixelX().
    const int samplePx = BACKGROUND_40COL_X0;

    charBaseCache = static_cast<uint16_t>(charBaseForRasterPixelX(raster, samplePx) + currentVICBank);

    screenBaseCache = static_cast<uint16_t>(screenBaseForRasterPixelX(raster, samplePx) + currentVICBank);

    bitmapBaseCache = static_cast<uint16_t>(bitmapBaseForRasterPixelX(raster, samplePx) + currentVICBank);
}

bool Vic::isBadLineForDebug(int raster) const
{
    if (raster < 0 || raster >= getMaxRasterLinesForDebug())
        return false;

    return raster == static_cast<int>(registers.raster)
        ? vicState.badLineSampled
        : isBadLine(raster);
}

Vic::VicBorderRasterDebugSnapshot Vic::getBorderRasterDebugSnapshot(int raster) const
{
    VicBorderRasterDebugSnapshot s {};

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return s;

    const auto vw = verticalBorderWindowForRaster(raster);

    s.valid = true;
    s.raster = raster;

    s.latchedVerticalBorder = borderVertical_per_raster[raster] != 0;
    s.latchedBorderOpenX = borderLeftOpenX_per_raster[raster];
    s.latchedBorderCloseX = borderRightCloseX_per_raster[raster];

    s.verticalTopOpen = vw.topOpen;
    s.verticalBottomClose = vw.bottomClose;
    s.withinVerticalDisplayWindow = rasterWithinVerticalDisplayWindow(raster);

    s.d011 = latchedD011ForRaster(raster);
    s.d016 = latchedD016ForRaster(raster);

    return s;
}

void Vic::setIERExact(uint8_t mask)
{
    registers.interruptEnable = mask & 0x0F;
    updateIRQLine();
}

void Vic::clearPendingIRQs()
{
    uint8_t pending = registers.interruptStatus & 0x0F;
    if (pending) writeRegister(0xD019, pending);
    (void)readRegister(0xD01E);
    (void)readRegister(0xD01F);
}

uint8_t Vic::effectiveD011ForRaster(int raster) const
{
    if (raster == registers.raster)
        return registers.control & 0x7F;   // live current-raster value
    return d011_per_raster[raster] & 0x7F; // latched for other rasters
}

uint8_t Vic::effectiveD016ForRaster(int raster) const
{
    if (raster == registers.raster)
        return registers.control2 & 0x1F;   // live current-raster value
    return d016_per_raster[raster] & 0x1F;  // latched for other rasters
}

uint8_t Vic::effectiveD018ForRaster(int raster) const
{
    if (raster == registers.raster)
        return registers.memory_pointer & 0xFE;   // live current-raster value
    return d018_per_raster[raster] & 0xFE;        // latched for other rasters
}

uint8_t Vic::d011ForRasterPixelX(int raster, int px, bool preferPreviousFrame) const
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return registers.control & 0x7F;

    uint8_t active = latchedD011ForRaster(raster) & 0x7F;

    const std::vector<RasterEventRecord>& events = preferPreviousFrame ? lastFrameRasterEventLog : rasterEventLog;

    for (const RasterEventRecord& e : events)
    {
        if (e.raster != raster)
            continue;

        if (e.kind != RasterEventKind::Control)
            continue;

        const int eventX = rasterEventPixelX(e.cycle);

        if (px >= eventX)
            active = e.newValue & 0x7F;
    }

    return active;
}

uint8_t Vic::d016ForRasterPixelX(int raster, int px, bool preferPreviousFrame) const
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return registers.control2 & 0x1F;

    uint8_t active = latchedD016ForRaster(raster) & 0x1F;

    const std::vector<RasterEventRecord>& events = preferPreviousFrame ? lastFrameRasterEventLog : rasterEventLog;

    for (const RasterEventRecord& e : events)
    {
        if (e.raster != raster)
            continue;

        if (e.kind != RasterEventKind::Control2)
            continue;

        const int eventX = rasterEventPixelX(e.cycle);

        if (px >= eventX)
            active = e.newValue & 0x1F;
    }

    return active;
}

uint8_t Vic::d018ForRasterPixelX(int raster, int px, bool preferPreviousFrame) const
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return registers.memory_pointer & 0xFE;

    uint8_t active = latchedD018ForRaster(raster) & 0xFE;

    const std::vector<RasterEventRecord>& events = preferPreviousFrame ? lastFrameRasterEventLog : rasterEventLog;

    for (const RasterEventRecord& e : events)
    {
        if (e.raster != raster)
            continue;

        if (e.kind != RasterEventKind::MemoryPointer)
            continue;

        const int eventX = rasterEventPixelX(e.cycle);

        if (px >= eventX)
            active = e.newValue & 0xFE;
    }

    return active;
}

Vic::FetchKind Vic::getFetchKindForCycle(int raster, int cycle) const
{
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return FetchKind::None;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return FetchKind::None;

    const bool badLineForThisRaster = (raster == registers.raster) ? vicState.badLine : isBadLine(raster);

    // Character matrix fetches use the visible/background fetch window,
    // not the bus-pressure/DMA warning window.
    if (badLineForThisRaster && cycle >= cfg_->bgFetchStartCycle && cycle <= cfg_->bgFetchEndCycle)
        return FetchKind::CharMatrix;

    for (int s = 0; s < 8; ++s)
    {
        const int slotStart = spriteFetchSlotStart(s);

        if (cycle == slotStart)
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

const char* Vic::fetchKindName(FetchKind kind) const
{
    switch (kind)
    {
        case FetchKind::None:        return "None";
        case FetchKind::Graphics:    return "Graphics";
        case FetchKind::CharMatrix:  return "CharMatrix";

        case FetchKind::SpritePtr0:  return "SpritePtr0";
        case FetchKind::SpritePtr1:  return "SpritePtr1";
        case FetchKind::SpritePtr2:  return "SpritePtr2";
        case FetchKind::SpritePtr3:  return "SpritePtr3";
        case FetchKind::SpritePtr4:  return "SpritePtr4";
        case FetchKind::SpritePtr5:  return "SpritePtr5";
        case FetchKind::SpritePtr6:  return "SpritePtr6";
        case FetchKind::SpritePtr7:  return "SpritePtr7";

        case FetchKind::SpriteData0: return "SpriteData0";
        case FetchKind::SpriteData1: return "SpriteData1";
        case FetchKind::SpriteData2: return "SpriteData2";
        case FetchKind::SpriteData3: return "SpriteData3";
        case FetchKind::SpriteData4: return "SpriteData4";
        case FetchKind::SpriteData5: return "SpriteData5";
        case FetchKind::SpriteData6: return "SpriteData6";
        case FetchKind::SpriteData7: return "SpriteData7";
    }

    return "Unknown";
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

    if (raster < static_cast<int>(lastFrameRasterPixelStates.size()) &&
        lastFrameRasterPixelStates[raster].valid)
    {
        snap = &lastFrameRasterPixelStates[raster];
        snapSource = "previous frame";
    }
    else if (raster < static_cast<int>(rasterPixelStates.size()) &&
             rasterPixelStates[raster].valid)
    {
        snap = &rasterPixelStates[raster];
        snapSource = "current frame";
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

bool Vic::vicTraceOn(TraceManager::TraceDetail d) const
{
    return traceMgr && traceMgr->vicDetailOn(d);
}

TraceManager::Stamp Vic::makeVicStamp() const
{
    if (!traceMgr)
        return TraceManager::Stamp{0, 0xFFFF, 0xFFFF};

    return traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, registers.raster, static_cast<uint16_t>(currentCycle * 8));
}

void Vic::traceBackgroundGraphicsFetch(int raster, int cycle, int column, int fetchPixelX, int outputX) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BUS))
        return;

    std::ostringstream out;

    out << "[VIC:GACCESS] "
        << "raster=" << raster
        << " cycle=" << cycle
        << " col=" << column
        << " liveVC=$"
        << std::hex << std::uppercase
        << std::setw(4) << std::setfill('0')
        << vicState.vc
        << " VMLI=" << std::dec
        << int(vicState.vmliFetchIndex)
        << " fetchX=" << fetchPixelX
        << " displayX="
        << (BACKGROUND_40COL_X0 +
            (d016ForRasterPixelX(
                raster,
                fetchPixelX,
                false) & 0x07) +
            column * 8)
        << " outputX=" << outputX;

    traceVicBusEvent(out.str());
}

void Vic::traceVicEvent(const std::string& text) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_EVENT))
        return;

    traceMgr->recordVicEvent(text, makeVicStamp());
}

void Vic::traceVicRegEvent(const std::string& text) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_REG))
        return;

    traceMgr->recordVicRegister(text, makeVicStamp());
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

    int framebufferX = hardwareX - cfg_->hardware_X + BORDER_SIZE;
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

void Vic::traceVicCycleCheckpoint(const char* phase, int raster, int cycle) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BADLINE) &&
        !vicTraceOn(TraceManager::TraceDetail::VIC_IRQ))
        return;

    const bool den = (effectiveD011ForRaster(raster) & 0x10) != 0;
    const int row = currentCharacterRow();

    std::ostringstream out;
    out << "[VIC:CYCLE] "
        << phase
        << " raster=" << std::dec << raster
        << " cycle=" << cycle
        << " vcBase=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
        << vicState.vcBase
        << " rc=" << std::dec << int(vicState.rc)
        << " bad=" << (vicState.badLine ? 1 : 0)
        << " disp=" << (vicState.displayEnabled ? 1 : 0)
        << " DEN=" << (den ? 1 : 0)
        << " row=" << row;

    traceMgr->recordVicBadline(out.str(), makeVicStamp());
}

void Vic::traceVicBadlineEvent(const std::string& text) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BADLINE))
        return;

    traceMgr->recordVicBadline(text, makeVicStamp());
}

void Vic::traceVicSpriteEvent(const std::string& text) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    traceMgr->recordVicSprite(text, makeVicStamp());
}

void Vic::traceVicBusEvent(const std::string& text) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BUS))
        return;

    traceMgr->recordVicBus(text, makeVicStamp());
}

void Vic::traceVicRasterIrqEvent(const char* phase, uint16_t oldLine, uint16_t newLine, bool matched) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_IRQ))
        return;

    std::ostringstream out;
    out << "[VIC:IRQ] "
        << phase
        << " old=$" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << oldLine
        << " new=$" << std::setw(3) << newLine
        << " cur=$" << std::setw(3) << registers.raster
        << " match=" << std::dec << (matched ? 1 : 0)
        << " ISR=$" << std::hex << std::uppercase << std::setw(2) << int(registers.interruptStatus & 0x0F)
        << " IER=$" << std::setw(2) << int(registers.interruptEnable & 0x0F);

    traceMgr->recordVicEvent(out.str(), makeVicStamp());
}


void Vic::traceVicRasterRetargetTest(const char* phase, uint16_t oldLine, uint16_t newLine, bool sampled, bool matched) const
{
    if (!traceMgr || !vicTraceOn(TraceManager::TraceDetail::VIC_IRQ))
        return;

    std::ostringstream out;
    out << "[VIC:IRQTEST] "
        << phase
        << " curRaster=$" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << registers.raster
        << " cycle=" << std::dec << currentCycle
        << " old=$" << std::hex << std::uppercase << std::setw(3) << oldLine
        << " new=$" << std::setw(3) << newLine
        << " sampled=" << std::dec << (sampled ? 1 : 0)
        << " matched=" << (matched ? 1 : 0)
        << " ISR=$" << std::hex << std::uppercase << std::setw(2) << int(registers.interruptStatus & 0x0F)
        << " IER=$" << std::setw(2) << int(registers.interruptEnable & 0x0F);

    traceMgr->recordVicEvent(out.str(), makeVicStamp());
}

void Vic::traceVicRegWrite(uint16_t address, uint8_t oldValue, uint8_t newValue) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_REG))
        return;

    std::ostringstream out;
    out << "[VIC:REG] $"
        << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << address
        << " old=$" << std::setw(2) << int(oldValue)
        << " new=$" << std::setw(2) << int(newValue);

    traceMgr->recordVicRegister(out.str(), makeVicStamp());
}

void Vic::traceVicBadLineStart(int raster, int cycle, uint16_t vcBase, uint8_t rc, bool den) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BADLINE))
        return;

    std::ostringstream out;
    out << "[VIC:BADLINE] start"
        << " raster=" << std::dec << raster
        << " cycle=" << cycle
        << " vcBase=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << vcBase
        << " rc=" << std::dec << int(rc)
        << " DEN=" << (den ? 1 : 0);

    traceMgr->recordVicBadline(out.str(), makeVicStamp());
}

void Vic::traceVicBadLineFetch(int raster, int cycle, int fetchIndex, uint16_t vc, int row, int col,
                               uint8_t screenByte, uint8_t colorByte) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BUS))
        return;

    std::ostringstream out;

    out << "[VIC:CACCESS] "
        << " raster=" << std::dec << raster
        << " cycle=" << cycle
        << " idx=" << fetchIndex
        << " addrVC=$"
        << std::hex << std::uppercase
        << std::setw(4) << std::setfill('0')
        << vc
        << " liveVC=$"
        << std::setw(4)
        << vicState.vc
        << " VMLI=" << std::dec
        << int(vicState.vmliFetchIndex)
        << " row=" << row
        << " col=" << col
        << " screen=$"
        << std::hex << std::uppercase
        << std::setw(2)
        << int(screenByte)
        << " color=$"
        << std::setw(2)
        << int(colorByte);

    traceVicBusEvent(out.str());
}

void Vic::traceVicSpriteDmaStart(int sprite) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    std::ostringstream out;
    out << "[VIC:SPRITE] DMA start"
        << " spr=" << sprite
        << " raster=" << std::dec << registers.raster
        << " y=$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << int(registers.spriteY[sprite]);

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
}

void Vic::traceVicSpritePtrFetch(int sprite, int raster, uint16_t ptrLoc, uint8_t ptr) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    std::ostringstream out;
    out << "[VIC:SPRITE] ptr fetch"
        << " spr=" << std::dec << sprite
        << " raster=" << raster
        << " addr=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << ptrLoc
        << " ptr=$" << std::setw(2) << int(ptr)
        << " dataBase=$" << std::setw(4) << (uint16_t(ptr) << 6);

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
}

void Vic::traceVicSpriteDataFetch(int sprite, int raster, int byteIndex, uint16_t addr, uint8_t value) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    std::ostringstream out;
    out << "[VIC:SPRITE] data fetch"
        << " spr=" << std::dec << sprite
        << " raster=" << raster
        << " byte=" << byteIndex
        << " addr=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << addr
        << " value=$" << std::setw(2) << int(value);

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
}

void Vic::traceVicSpriteSlotEvent(int sprite, const char* phase, int raster, int cycle, int byteIndex) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    if (sprite < 0 || sprite >= 8)
        return;

    const SpriteUnit& su = spriteUnits[sprite];

    std::ostringstream out;
    out << "[VIC:SPR] "
        << "s=" << sprite
        << " phase=" << phase
        << " ras=$" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << raster
        << " cyc=$" << std::setw(2) << cycle
        << " dot=" << std::dec << (cycle * 8)
        << " slot=$" << std::hex << std::uppercase << std::setw(2) << spriteFetchSlotStart(sprite)
        << " dma=" << std::dec << (su.dmaActive ? 1 : 0)
        << " rowlat=" << (su.rowDataLatched ? 1 : 0)
        << " yexp=" << (su.yExpandLatch ? 1 : 0)
        << " mc=" << int(su.mc)
        << " mcbase=" << int(su.mcBase)
        << " row=" << su.currentRow
        << " ptr=$" << std::hex << std::uppercase << std::setw(2) << int(su.pointerByte)
        << " base=$" << std::setw(4) << su.dataBase;

    if (byteIndex >= 0)
        out << " byte=" << std::dec << byteIndex;

    out << " f0=$" << std::hex << std::uppercase << std::setw(2) << int(su.fetched0)
        << " f1=$" << std::setw(2) << int(su.fetched1)
        << " f2=$" << std::setw(2) << int(su.fetched2);

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
}

void Vic::traceVicSpriteEolState(int sprite, int raster) const
{
    traceVicSpriteSlotEvent(sprite, "eol", raster, currentCycle, -1);
}

void Vic::traceVicSpriteAdvanceDecision(int sprite, int raster, bool willAdvance) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    std::ostringstream out;
    out << "[VIC:SPR] "
        << "s=" << sprite
        << " phase=advance-check"
        << " ras=$" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << raster
        << " cyc=$" << std::setw(2) << currentCycle
        << " dot=" << std::dec << (currentCycle * 8)
        << " willAdvance=" << (willAdvance ? 1 : 0)
        << " dma=" << (spriteUnits[sprite].dmaActive ? 1 : 0)
        << " rowlat=" << (spriteUnits[sprite].rowDataLatched ? 1 : 0)
        << " yexp=" << (spriteUnits[sprite].yExpandLatch ? 1 : 0)
        << " mc=" << int(spriteUnits[sprite].mc)
        << " mcbase=" << int(spriteUnits[sprite].mcBase)
        << " row=" << spriteUnits[sprite].currentRow;

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
}

void Vic::traceVicSpriteStartCheck(int sprite, int raster, uint8_t spriteY, bool enabled, bool yExpanded,
    bool rasterMatch, bool willStart) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    std::ostringstream out;
    out << "[VIC:SPR] "
        << "s=" << sprite
        << " phase=start-check"
        << " ras=$" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << raster
        << " cyc=$" << std::setw(2) << currentCycle
        << " dot=" << std::dec << (currentCycle * 8)
        << " sprY=$" << std::hex << std::uppercase << std::setw(2) << int(spriteY)
        << std::dec
        << " en=" << int(enabled)
        << " yexp=" << int(yExpanded)
        << " match=" << int(rasterMatch)
        << " start=" << int(willStart)
        << " dma=" << int(spriteUnits[sprite].dmaActive)
        << " rowlat=" << int(spriteUnits[sprite].rowDataLatched)
        << " row=" << spriteUnits[sprite].currentRow
        << " mc=" << int(spriteUnits[sprite].mc)
        << " mcbase=" << int(spriteUnits[sprite].mcBase)
        << " startY=" << spriteUnits[sprite].startY;

    traceMgr->recordVicEvent(out.str(), makeVicStamp());
}

void Vic::traceVicSpriteRowMismatch(int sprite, int raster, int computedRow) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    std::ostringstream out;
    out << "[VIC:SPR] row-mismatch"
        << " spr=" << sprite
        << " raster=" << raster
        << " computed=" << computedRow
        << " current=" << spriteUnits[sprite].currentRow
        << " mcBase=" << int(spriteUnits[sprite].mcBase)
        << " dma=" << int(spriteUnits[sprite].dmaActive)
        << " rowlat=" << int(spriteUnits[sprite].rowDataLatched)
        << " yExp=" << int(spriteUnits[sprite].yExpandLatch)
        << " startY=" << spriteUnits[sprite].startY;

    traceMgr->recordVicEvent(out.str(), makeVicStamp());
}

void Vic::traceVicBusArb(bool oldBA, bool oldAEC, bool newBA, bool newAEC, bool badLineNow, bool baLow, bool aecLow) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BUS))
        return;

    std::ostringstream out;
    out << "[VIC:BUS] "
        << "reason=" << busArbReason(registers.raster, currentCycle)
        << " bad=" << (badLineNow ? 1 : 0)
        << " BA " << (oldBA ? 'H' : 'L') << "->" << (newBA ? 'H' : 'L')
        << " AEC " << (oldAEC ? 'H' : 'L') << "->" << (newAEC ? 'H' : 'L')
        << " balow=" << (baLow ? 1 : 0)
        << " aeclow=" << (aecLow ? 1 : 0);

    traceVicBusEvent(out.str());
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

void Vic::postLoadState()
{
    // Reconnect config pointer from current/restored mode.
    cfg_ = (mode_ == VideoMode::NTSC ? &NTSC_CONFIG : &PAL_CONFIG);

    // Keep only basic range safety. Do NOT rewrite restored internal sequencer state.
    if (registers.raster >= cfg_->maxRasterLines)
        registers.raster %= cfg_->maxRasterLines;

    if (currentCycle < 0)
        currentCycle = 0;

    if (currentCycle >= cfg_->cyclesPerLine)
        currentCycle %= cfg_->cyclesPerLine;

    // Normalize raw register-style values only.
    registers.rasterInterruptLine &= 0x01FF;

    registers.control &= 0x7F;
    registers.control2 &= 0x1F;
    registers.memory_pointer &= 0xFE;

    registers.interruptStatus &= 0x0F;
    registers.interruptEnable &= 0x0F;

    registers.borderColor &= 0x0F;
    registers.backgroundColor0 &= 0x0F;

    for (int i = 0; i < 3; ++i)
        registers.backgroundColor[i] &= 0x0F;

    registers.spriteMultiColor1 &= 0x0F;
    registers.spriteMultiColor2 &= 0x0F;

    for (int i = 0; i < 8; ++i)
        registers.spriteColors[i] &= 0x0F;

    // Make sure vectors exist and are valid-sized, but do not overwrite valid restored contents.
    auto fixSizeU8 = [&](std::vector<uint8_t>& v, uint8_t fill)
    {
        if (v.size() != static_cast<size_t>(cfg_->maxRasterLines))
            v.assign(cfg_->maxRasterLines, fill);
    };

    auto fixSizeU16 = [&](std::vector<uint16_t>& v, uint16_t fill)
    {
        if (v.size() != static_cast<size_t>(cfg_->maxRasterLines))
            v.assign(cfg_->maxRasterLines, fill);
    };

    fixSizeU8(d011_per_raster, registers.control & 0x7F);
    fixSizeU8(d016_per_raster, registers.control2 & 0x1F);
    fixSizeU8(d018_per_raster, registers.memory_pointer & 0xFE);

    const uint16_t defaultBank = cia2 ? cia2->getCurrentVICBank() : 0;
    fixSizeU16(dd00_per_raster, defaultBank);

    fixSizeU8(borderVertical_per_raster, vicState.verticalBorder ? 1 : 0);
    fixSizeU8(borderVerticalStart_per_raster, vicState.verticalBorder ? 1 : 0);

    if (borderLeftOpenX_per_raster.size() != static_cast<size_t>(cfg_->maxRasterLines))
        borderLeftOpenX_per_raster.assign(cfg_->maxRasterLines, vicState.leftBorderOpenX);

    if (borderRightCloseX_per_raster.size() != static_cast<size_t>(cfg_->maxRasterLines))
        borderRightCloseX_per_raster.assign(cfg_->maxRasterLines, vicState.rightBorderCloseX);

    rasterRowStates.resize(cfg_->maxRasterLines);
    lastFrameRasterRowStates.resize(cfg_->maxRasterLines);

    rasterPixelStates.resize(cfg_->maxRasterLines);
    lastFrameRasterPixelStates.resize(cfg_->maxRasterLines);

    bgOpaque.resize(cfg_->visibleLines + 2 * BORDER_SIZE);
    for (auto& row : bgOpaque)
        row.fill(0);

    // Keep sprite restored state, only mask fields that have known hardware ranges.
    for (auto& s : spriteUnits)
    {
        s.mc &= 0x3F;
        s.mcBase &= 0x3F;
    }

    // Recompute derived live state from restored values.
    currentCycleSlot = cycleSlotFor(registers.raster, currentCycle);

    updateGraphicsMode(registers.raster);
    updateBusArbitration();
    updateIRQLine();

    // Debug/monitor cache refresh only.
    updateMonitorCaches(registers.raster);

    // Treat this as diagnostic only unless behavior depends on it.
    lastRasterIRQSample = {};
}
