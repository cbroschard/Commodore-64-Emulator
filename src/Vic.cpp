// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Vic.h"
#include "IO.h"

Vic::Vic(VideoMode mode) :
    cia2object(nullptr),
    processor(nullptr),
    IO_adapter(nullptr),
    IRQ(nullptr),
    logger(nullptr),
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
    AEC = true;

    // Raster IRQ
    rasterIrqSampledThisLine = false;
    lastRasterIRQSample = {};

    // Internal VIC state
    vicState.vcBase = 0;
    vicState.vmliBase = 0;
    vicState.vmliFetchIndex = 0;
    vicState.rc = 0;

    vicState.displayEnabled = false;
    vicState.displayEnabledNext = false;
    vicState.badLine = false;
    vicState.badLineSampled = false;

    vicState.verticalBorder = true;
    vicState.leftBorder = true;
    vicState.rightBorder = true;

    vicState.leftBorderOpenX = 0;
    vicState.rightBorderCloseX = VISIBLE_WIDTH;

    vicState.topBorderOpenRaster = -1;
    vicState.bottomBorderCloseRaster = -1;

    vicState.ba = true;
    vicState.aec = true;
    vicState.openBus = 0xFF;

    for (auto& s : spriteUnits)
    {
        s.dmaActive = false;
        s.displayActive = false;
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

    bgPipelineConfig.standardText = true;
    bgPipelineConfig.multicolorText = true;
    bgPipelineConfig.standardBitmap = true;
    bgPipelineConfig.multicolorBitmap = true;
    bgPipelineConfig.ecm = true;

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
    std::fill(borderLeftOpenX_per_raster.begin(), borderLeftOpenX_per_raster.end(), 0);
    std::fill(borderRightCloseX_per_raster.begin(), borderRightCloseX_per_raster.end(), VISIBLE_WIDTH);

    bgColorLine.fill(0);
    bgOpaqueLine.fill(0);
    bgSourceLine.fill(BackgroundSource::Border);

    borderMaskLine.fill(1);
    finalColorLine.fill(0);

    resetActiveBackgroundPixelState();

    // Fill in DD00
    uint16_t currentVICBank = cia2object ? cia2object->getCurrentVICBank() : 0;
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
    for (auto& line : spriteBehindLine)         line.fill(0);
    for (auto& line : spriteMulticolorModeLine) line.fill(0);
    for (auto& line : spriteXExpansionLine)     line.fill(0);
    for (auto& line : spriteEnableLine)         line.fill(0);

    resetActiveMatrixRow();

    // Sprite collision latches
    lastSpriteSpriteCollision = {};
    lastSpriteBackgroundCollision = {};

    // ML Monitor logging default disable
    setLogging = false;
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
    updateMonitorCaches(registers.raster);

    // Notify IO of mode
    if (IO_adapter)
        IO_adapter->setScreenDimensions(320, cfg_->visibleLines, BORDER_SIZE);
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
    {
        wrtr.writeU8(registers.backgroundColor[i]);
    }

    // Dump Sprite Color Registers
    wrtr.writeU8(registers.spriteMultiColor1);
    wrtr.writeU8(registers.spriteMultiColor2);
    for (int i = 0; i < 8; ++i)
    {
        wrtr.writeU8(registers.spriteColors[i]);
    }

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
    wrtr.writeU32(1); // version

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

    // Dump AEC
    wrtr.writeBool(AEC);

    // Dump State
    wrtr.writeU16(vicState.vcBase);
    wrtr.writeU16(vicState.vmliBase);
    wrtr.writeU8(vicState.vmliFetchIndex);
    wrtr.writeU8(vicState.rc);

    wrtr.writeBool(vicState.displayEnabled);
    wrtr.writeBool(vicState.displayEnabledNext);
    wrtr.writeBool(vicState.badLine);
    wrtr.writeBool(vicState.badLineSampled);

    wrtr.writeBool(vicState.verticalBorder);
    wrtr.writeBool(vicState.leftBorder);
    wrtr.writeBool(vicState.rightBorder);

    wrtr.writeI32(vicState.leftBorderOpenX);
    wrtr.writeI32(vicState.rightBorderCloseX);

    wrtr.writeI32(vicState.topBorderOpenRaster);
    wrtr.writeI32(vicState.bottomBorderCloseRaster);

    wrtr.writeBool(vicState.ba);
    wrtr.writeBool(vicState.aec);
    wrtr.writeU8(vicState.openBus);

    wrtr.writeBool(rasterIrqSampledThisLine);

    for (const auto& s : spriteUnits)
    {
        wrtr.writeBool(s.dmaActive);
        wrtr.writeBool(s.displayActive);
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

        for (int i = 0; i < 8; ++i) {
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

        // Preserve the programmed 9-bit raster IRQ target.
        registers.rasterInterruptLine &= 0x01FF;

        // Mask colors to 4-bit (safer on corrupt/old states)
        registers.borderColor &= 0x0F;
        registers.backgroundColor0 &= 0x0F;
        for (int i = 0; i < 3; ++i) registers.backgroundColor[i] &= 0x0F;
        registers.spriteMultiColor1 &= 0x0F;
        registers.spriteMultiColor2 &= 0x0F;
        for (int i = 0; i < 8; ++i) registers.spriteColors[i] &= 0x0F;

        // Re-sync anything derived from registers
        updateGraphicsMode(registers.raster);
        updateIRQLine();
        updateMonitorCaches(registers.raster);

        rdr.exitChunkPayload(chunk);
        return true;
    }

    if (std::memcmp(chunk.tag, "VICX", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                  { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                           { rdr.exitChunkPayload(chunk); return false; }

        uint8_t m = 0;
        if (!rdr.readU8(m))                                     { rdr.exitChunkPayload(chunk); return false; }
        if (m != static_cast<uint8_t>(mode_))                   { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readI32(currentCycle))                         { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 8; ++i)
            if (!rdr.readU16(sprPtrBase[i]))                    { rdr.exitChunkPayload(chunk); return false; }
        for (int i = 0; i < 40; ++i)
            if (!rdr.readU8(charPtrFIFO[i]))                    { rdr.exitChunkPayload(chunk); return false; }
        for (int i = 0; i < 40; ++i)
            if (!rdr.readU8(colorPtrFIFO[i]))                   { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(denSeenOn30))                         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(firstBadlineY))                        { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(AEC))                                 { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU16(vicState.vcBase))                      { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU16(vicState.vmliBase))                    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(vicState.vmliFetchIndex))              { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(vicState.rc))                           { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.displayEnabled))             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.displayEnabledNext))         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.badLine))                    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.badLineSampled))             { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(vicState.verticalBorder))             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.leftBorder))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.rightBorder))                { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readI32(vicState.leftBorderOpenX))             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(vicState.rightBorderCloseX))           { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readI32(vicState.topBorderOpenRaster))         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(vicState.bottomBorderCloseRaster))     { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(vicState.ba))                         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.aec))                        { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(vicState.openBus))                      { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(rasterIrqSampledThisLine))            { rdr.exitChunkPayload(chunk); return false; }

        for (auto& s : spriteUnits)
        {
            if (!rdr.readBool(s.dmaActive))                     { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(s.displayActive))                 { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(s.yExpandLatch))                  { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readU8(s.mc))                              { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.mcBase))                          { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readU8(s.pointerByte))                     { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU16(s.dataBase))                       { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readU8(s.shift0))                          { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.shift1))                          { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.shift2))                          { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readI32(s.currentRow))                     { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readI32(s.startY))                         { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readI32(s.outputBit))                      { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(s.outputRepeat))                   { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(s.rowPrepared))                   { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(s.rowDataLatched))                { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readI32(s.outputXStart))                   { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(s.outputWidth))                    { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readU8(s.fetched0))                        { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.fetched1))                        { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.fetched2))                        { rdr.exitChunkPayload(chunk); return false; }
        }

        if (!rdr.readVectorU8(d011_per_raster))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU8(d016_per_raster))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU8(d018_per_raster))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU16(dd00_per_raster))                { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(frameDone))                           { rdr.exitChunkPayload(chunk); return false; }

        // --- sanitize restored values ---
        if (registers.raster >= cfg_->maxRasterLines)
            registers.raster %= cfg_->maxRasterLines;

        if (currentCycle < 0) currentCycle = 0;
        if (currentCycle >= cfg_->cyclesPerLine)
            currentCycle %= cfg_->cyclesPerLine;

        const int visibleRows = getRSEL(registers.raster) ? 25 : 24;
        const uint16_t maxRowBase = static_cast<uint16_t>((visibleRows - 1) * 40);

        vicState.vcBase   = static_cast<uint16_t>((vicState.vcBase   / 40) * 40);
        vicState.vmliBase = static_cast<uint16_t>((vicState.vmliBase / 40) * 40);

        if (vicState.vcBase > maxRowBase)
            vicState.vcBase = maxRowBase;

        if (vicState.vmliBase > maxRowBase)
            vicState.vmliBase = vicState.vcBase;

        if (vicState.vmliFetchIndex > 40)
            vicState.vmliFetchIndex = 40;

        vicState.rc &= 0x07;
        vicState.openBus = static_cast<uint8_t>(vicState.openBus);

        vicState.leftBorderOpenX = std::clamp(vicState.leftBorderOpenX, 0, VISIBLE_WIDTH);
        vicState.rightBorderCloseX = std::clamp(vicState.rightBorderCloseX, 0, VISIBLE_WIDTH);

        if (vicState.leftBorderOpenX > vicState.rightBorderCloseX)
            std::swap(vicState.leftBorderOpenX, vicState.rightBorderCloseX);

        vicState.topBorderOpenRaster =  std::clamp(vicState.topBorderOpenRaster, 0, int(cfg_->maxRasterLines));
        vicState.bottomBorderCloseRaster =  std::clamp(vicState.bottomBorderCloseRaster, 0, int(cfg_->maxRasterLines));

        if (vicState.topBorderOpenRaster > vicState.bottomBorderCloseRaster)
            std::swap(vicState.topBorderOpenRaster, vicState.bottomBorderCloseRaster);

        for (auto& s : spriteUnits)
        {
            s.mc &= 0x3F;
            s.mcBase &= 0x3F;
            s.pointerByte &= 0xFF;
        }

        auto fixSizeU8  = [&](std::vector<uint8_t>& v, uint8_t fill){
            if (v.size() != (size_t)cfg_->maxRasterLines) v.assign(cfg_->maxRasterLines, fill);
        };
        auto fixSizeU16 = [&](std::vector<uint16_t>& v, uint16_t fill){
            if (v.size() != (size_t)cfg_->maxRasterLines) v.assign(cfg_->maxRasterLines, fill);
        };

        fixSizeU8(d011_per_raster, 0x1B);
        fixSizeU8(d016_per_raster, 0x08);
        fixSizeU8(d018_per_raster, 0x14);

        uint16_t defBank = cia2object ? cia2object->getCurrentVICBank() : 0;
        fixSizeU16(dd00_per_raster, defBank);

        // Recompute mode/caches from restored latches
        updateGraphicsMode(registers.raster);

        // Prefer CIA2's current bank for *current raster* (optional but helps)
        if (cia2object)
            dd00_per_raster[registers.raster] = cia2object->getCurrentVICBank();

        // Rebuild Border Latches
        rebuildBorderRasterLatches();

        updateMonitorCaches(registers.raster);

        // Make sure CPU BA hold matches current DMA now
        updateBusArbitration();

        // IRQ line consistent with restored IER/ISR
        updateIRQLine();

        rdr.exitChunkPayload(chunk);
        return true;
    }

    return false; // not our chunk
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
            return latchOpenBus(getOpenBus());

        case 0xD030:
            return latchOpenBus(getOpenBus());

        default:
        {
            if (logger && setLogging)
            {
                logger->WriteLog("Attempt to read to unhandled VIC address = " +
                                 std::to_string(static_cast<int>(address)));
            }
            return latchOpenBus(getOpenBus());
        }
    }
}

void Vic::writeRegister(uint16_t address, uint8_t value)
{
    // Latch Open Bus
    updateOpenBus(value);

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
            updateVerticalBorderState(raster);
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
            const uint8_t oldD019 = d019Read();

            const uint8_t clearMask = value & 0x0F;

            registers.interruptStatus &= ~clearMask;

            const uint8_t newPending = registers.interruptStatus & 0x0F;
            const uint8_t newD019 = d019Read();

            if (logger && setLogging)
            {
                std::ostringstream oss;
                oss << "[VIC:IRQ] write D019 clear"
                    << " raster=" << registers.raster
                    << " cycle=" << currentCycle
                    << " value=$"
                    << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                    << static_cast<int>(value)
                    << " clearMask=$" << std::setw(2) << static_cast<int>(clearMask)
                    << " oldD019=$" << std::setw(2) << static_cast<int>(oldD019)
                    << " newD019=$" << std::setw(2) << static_cast<int>(newD019)
                    << " oldPending=$" << std::setw(2) << static_cast<int>(oldPending)
                    << " newPending=$" << std::setw(2) << static_cast<int>(newPending)
                    << " IER=$" << std::setw(2) << static_cast<int>(registers.interruptEnable & 0x0F)
                    << std::dec << std::nouppercase << std::setfill(' ');

                logger->WriteLog(oss.str());
            }

            traceVicRegWrite(address, oldPending, newPending);
            updateIRQLine();
            break;
        }

        case 0xD01A:
        {
            const uint8_t oldValue = registers.interruptEnable & 0x0F;

            registers.interruptEnable = value & 0x0F;

            if (logger && setLogging)
            {
                std::ostringstream oss;
                oss << "[VIC:IRQ] write D01A"
                    << " raster=" << registers.raster
                    << " cycle=" << currentCycle
                    << " value=$"
                    << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                    << static_cast<int>(value)
                    << " oldIER=$" << std::setw(2) << static_cast<int>(oldValue)
                    << " newIER=$" << std::setw(2) << static_cast<int>(registers.interruptEnable & 0x0F)
                    << " D019=$" << std::setw(2) << static_cast<int>(d019Read())
                    << " pending=$" << std::setw(2) << static_cast<int>(registers.interruptStatus & 0x0F)
                    << std::dec << std::nouppercase << std::setfill(' ')
                    << " irqLineActive=" << (irqLineActive() ? 1 : 0);

                logger->WriteLog(oss.str());
            }

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
        {
            if (logger && setLogging)
            {
                logger->WriteLog("Attempt to write to unhandled vic area address = " +
                                 std::to_string(static_cast<int>(address)));
            }
            break;
        }
    }
}

void Vic::tick(int cycles)
{
    while (cycles-- > 0)
    {
        beginFrameIfNeeded();

        runCycleDecisionPhase();

        currentCycleSlot = cycleSlotFor(registers.raster, currentCycle);

        updateBusArbitration();

        runFetchPhase();

        advanceCycleAndFinalizeLineIfNeeded();
    }
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

        vicState.vcBase = 0;
        vicState.vmliBase = 0;
        vicState.vmliFetchIndex = 0;
        vicState.rc = 0;
        vicState.badLine = false;
        vicState.badLineSampled = false;
        vicState.displayEnabled = false;
        vicState.displayEnabledNext = false;

        vicState.topBorderOpenRaster = 0;
        vicState.bottomBorderCloseRaster = 0;

        clearBadLineFifo();
    }

    // Latch the "DEN was seen on raster $30" qualifier exactly once,
    // at the beginning of raster line $30, from the live D011 register.
    if (currentCycle == 0 && registers.raster == 0x30)
    {
        denSeenOn30 = (registers.control & 0x10) != 0;
    }
}

void Vic::runCycleDecisionPhase()
{
    if (isRasterIRQCompareCycle(currentCycle))
        sampleRasterIRQCompare("normal-sample");

    switch (currentCycle)
    {
        case 0:
            handleCycle0Decisions();
            break;

        case 14:
            handleCycle14Decisions();
            break;

        case 15:
            handleCycle15Decisions();
            handleSpriteDmaStartDecisions();
            break;

        case 58:
            handleCycle58Decisions();
            break;

        default:
            break;
    }

    if (currentCycle == cfg_->DMAStartCycle)
        handleDmaStartCycleDecisions();
}

void Vic::handleCycle0Decisions()
{
    const int raster = registers.raster;

    // Latch the register state used by completed-raster rendering.
    // Do this once at the start of the raster, not on every register write.
    d011_per_raster[raster] = registers.control & 0x7F;
    d016_per_raster[raster] = registers.control2 & 0x1F;
    d018_per_raster[raster] = registers.memory_pointer & 0xFE;

    latchNextRasterDD00();

    updateVerticalBorderState(raster);
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
    const bool badNow = isBadLine(raster);

    vicState.badLineSampled = badNow;
    vicState.badLine = badNow;

    traceVicCycleCheckpoint("cycle-14", raster, currentCycle);

    if (badNow)
    {
        const bool firstBadlineThisFrame = (firstBadlineY < 0);
        initializeFirstBadLineIfNeeded(raster);

        // First badline of the frame should immediately arm display
        // progression for the following line, even before cycle 58
        // later reaffirms it.
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

void Vic::handleSpriteDmaStartDecisions()
{
    updateSpriteDMAStartForCurrentLine(registers.raster);
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

    const bool badLineCanCarry =
        vicState.badLineSampled &&
        rasterWithinVerticalDisplayWindow(registers.raster);

    vicState.displayEnabledNext =
        vicState.displayEnabled || badLineCanCarry;
}

void Vic::runFetchPhase()
{
    const int raster = registers.raster;
    const int cycle  = currentCycle;

    switch (currentCycleSlot.fetchKind)
    {
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
            const int sprite = spritePointerFetchSpriteForCycle(cycle);
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
            const int sprite = spriteDataFetchSpriteForCycle(cycle);
            if (sprite >= 0)
            {
                const int byteIndex = spriteDataByteIndexForCycle(sprite, cycle);
                if (byteIndex >= 0 && byteIndex < 3)
                    fetchSpriteDataByte(sprite, byteIndex, raster);
            }
            break;
        }

        case FetchKind::None:
        default:
            break;
    }
}

int Vic::spritePointerFetchSpriteForCycle(int cycle) const
{
    for (int s = 0; s < 8; ++s)
    {
        if (isSpritePointerFetchCycle(s, cycle))
            return s;
    }
    return -1;
}

int Vic::spriteDataFetchSpriteForCycle(int cycle) const
{
    for (int s = 0; s < 8; ++s)
    {
        if (spriteUnits[s].dmaActive && isSpriteDMAFetchCycle(s, cycle))
            return s;
    }
    return -1;
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

    const uint16_t screenBase =
        screenBaseForRasterPixelX(raster, px);

    return static_cast<uint16_t>(screenBase + 0x03F8 + sprite);
}

void Vic::performBadLineFetchesForCurrentCycle()
{
    if (!vicState.badLineSampled)
        return;

    if (currentCycle < cfg_->DMAStartCycle || currentCycle > cfg_->DMAEndCycle)
        return;

    if (vicState.vmliFetchIndex >= 40)
        return;

    const int raster = registers.raster;
    const int fetchIndex = vicState.vmliFetchIndex;

    fetchBadLineMatrixByte(fetchIndex, raster);
    ++vicState.vmliFetchIndex;
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
    buildSpriteEnableLine(curRaster);

    prepareSpriteOutputForRaster(curRaster);

    buildSpriteMulticolorModeLine(curRaster);
    buildSpriteXExpansionLine(curRaster);

    beginSpriteRasterOutput(curRaster);

    int sx0, sx1;
    spriteVisibleXRange(sx0, sx1);
    for (int px = sx0; px < sx1; ++px)
    {
        stepSpriteSequencersAtX(curRaster, px);
    }

    renderLine(curRaster);

    snapshotRasterPixelComposition(curRaster);
    snapshotRasterRowState(curRaster);

    updateSpriteDMAEndOfLine(curRaster);
    advanceVideoCountersEndOfLine(curRaster);

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

        if (IO_adapter)
        {
            const int lastFBY = fbY(curRaster);
            const int fbH = cfg_->visibleLines + 2 * BORDER_SIZE;

            for (int y = lastFBY + 1; y < fbH; ++y)
            {
                IO_adapter->renderBorderLine(y, registers.borderColor, 0, 0);
            }
        }
    }
}

void Vic::advanceToNextRaster()
{
    registers.raster = (registers.raster + 1) % cfg_->maxRasterLines;

    vicState.badLineSampled = false;
    rasterIrqSampledThisLine = false;
}

void Vic::traceRasterEnd()
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_RASTER))
        return;

    TraceManager::Stamp stamp =
        traceMgr->makeStamp(processor ? processor->getTotalCycles() : 0,
                            registers.raster,
                            (currentCycle * 8));

    traceMgr->recordVicRaster(registers.raster, currentCycle,
                              (registers.interruptStatus & 0x01) != 0,
                              registers.control,
                              registers.rasterInterruptLine & 0xFF,
                              stamp);
}

void Vic::updatePerCycleState()
{
    // Per-cycle bus arbitration
    updateBusArbitration();
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

    w.topOpen = rsel25 ? 51 : 55;
    w.bottomClose = rsel25 ? 250 : 246;

    return w;
}

void Vic::syncSpriteCompatAddress(int sprite)
{
    sprPtrBase[sprite] = spriteUnits[sprite].dataBase;
}

bool Vic::spriteHasFetchedDisplayRow(int sprite) const
{
    return spriteUnits[sprite].rowDataLatched;
}

bool Vic::spriteCanRenderThisRaster(int sprite) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    if (!spriteEnabledSomewhereOnLine(sprite))
        return false;

    if (!spriteUnits[sprite].dmaActive)
        return false;

    if (!spriteHasFetchedDisplayRow(sprite))
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

bool Vic::firstRasterSpriteModeEventValue(int raster, uint8_t& value) const
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

    if (px < 0 || px >= 512)
        return false;

    return spriteMulticolorModeLine[sprite][px] != 0;
}

bool Vic::firstRasterSpriteXExpansionEventValue(int raster, uint8_t& value) const
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

    if (px < 0 || px >= 512)
        return false;

    return spriteXExpansionLine[sprite][px] != 0;
}

void Vic::buildSpriteXExpansionLine(int raster)
{
    const int xStart = rasterVisibleStartX(raster);
    const int xEnd   = rasterVisibleEndX(raster);

    for (auto& line : spriteXExpansionLine)
        line.fill(0);

    uint8_t activeExpansion = registers.spriteXExpansion;

    if (!firstRasterSpriteXExpansionEventValue(raster, activeExpansion))
    {
        for (int spr = 0; spr < 8; ++spr)
        {
            const uint8_t expanded = ((activeExpansion >> spr) & 0x01) ? 1 : 0;

            for (int px = xStart; px < xEnd; ++px)
                spriteXExpansionLine[spr][px] = expanded;
        }

        return;
    }

    int startX = xStart;

    for (const RasterSpriteXExpansionEvent& e : rasterSpriteXExpansionEvents)
    {
        if (e.raster != raster)
            continue;

        const int eventX = std::clamp(rasterEventPixelX(e.cycle), startX, xEnd);

        for (int spr = 0; spr < 8; ++spr)
        {
            const uint8_t expanded = ((activeExpansion >> spr) & 0x01) ? 1 : 0;

            for (int px = startX; px < eventX; ++px)
                spriteXExpansionLine[spr][px] = expanded;
        }

        activeExpansion = e.newValue;
        startX = eventX;
    }

    for (int spr = 0; spr < 8; ++spr)
    {
        const uint8_t expanded = ((activeExpansion >> spr) & 0x01) ? 1 : 0;

        for (int px = startX; px < xEnd; ++px)
            spriteXExpansionLine[spr][px] = expanded;
    }
}

void Vic::buildSpriteMulticolorModeLine(int raster)
{
    const int xStart = rasterVisibleStartX(raster);
    const int xEnd   = rasterVisibleEndX(raster);

    for (auto& line : spriteMulticolorModeLine)
        line.fill(0);

    uint8_t activeMode = registers.spriteMultiColor;

    if (!firstRasterSpriteModeEventValue(raster, activeMode))
    {
        for (int spr = 0; spr < 8; ++spr)
        {
            const uint8_t multicolor = ((activeMode >> spr) & 0x01) ? 1 : 0;

            for (int px = xStart; px < xEnd; ++px)
                spriteMulticolorModeLine[spr][px] = multicolor;
        }

        return;
    }

    int startX = xStart;

    for (const RasterSpriteModeEvent& e : rasterSpriteModeEvents)
    {
        if (e.raster != raster)
            continue;

        const int eventX = std::clamp(rasterEventPixelX(e.cycle), startX, xEnd);

        for (int spr = 0; spr < 8; ++spr)
        {
            const uint8_t multicolor = ((activeMode >> spr) & 0x01) ? 1 : 0;

            for (int px = startX; px < eventX; ++px)
                spriteMulticolorModeLine[spr][px] = multicolor;
        }

        activeMode = e.newValue;
        startX = eventX;
    }

    for (int spr = 0; spr < 8; ++spr)
    {
        const uint8_t multicolor = ((activeMode >> spr) & 0x01) ? 1 : 0;

        for (int px = startX; px < xEnd; ++px)
            spriteMulticolorModeLine[spr][px] = multicolor;
    }
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

    if (px < 0 || px >= 512)
        return false;

    return spriteEnableLine[sprite][px] != 0;
}

bool Vic::spriteEnabledSomewhereOnLine(int sprite) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    for (int px = 0; px < VISIBLE_WIDTH; ++px)
    {
        if (spriteEnableLine[sprite][px])
            return true;
    }

    return false;
}

void Vic::buildSpriteEnableLine(int raster)
{
    const int xStart = rasterVisibleStartX(raster);
    const int xEnd   = rasterVisibleEndX(raster);

    for (auto& line : spriteEnableLine)
        line.fill(0);

    uint8_t activeEnable = registers.spriteEnabled;

    if (!firstRasterSpriteEnableEventValue(raster, activeEnable))
    {
        for (int spr = 0; spr < 8; ++spr)
        {
            const uint8_t enabled = ((activeEnable >> spr) & 0x01) ? 1 : 0;

            for (int px = xStart; px < xEnd; ++px)
                spriteEnableLine[spr][px] = enabled;
        }

        return;
    }

    int startX = xStart;

    for (const RasterSpriteEnableEvent& e : rasterSpriteEnableEvents)
    {
        if (e.raster != raster)
            continue;

        const int eventX = std::clamp(rasterEventPixelX(e.cycle), startX, xEnd);

        for (int spr = 0; spr < 8; ++spr)
        {
            const uint8_t enabled = ((activeEnable >> spr) & 0x01) ? 1 : 0;

            for (int px = startX; px < eventX; ++px)
                spriteEnableLine[spr][px] = enabled;
        }

        activeEnable = e.newValue;
        startX = eventX;
    }

    for (int spr = 0; spr < 8; ++spr)
    {
        const uint8_t enabled = ((activeEnable >> spr) & 0x01) ? 1 : 0;

        for (int px = startX; px < xEnd; ++px)
            spriteEnableLine[spr][px] = enabled;
    }
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

    syncSpriteCompatAddress(sprite);
    traceVicSpriteSlotEvent(sprite, "ptr", raster, currentCycle);
}

void Vic::prepareSpriteOutputForRaster(int raster)
{
    for (int i = 0; i < 8; ++i)
    {
        resetSpriteLineOutputState(i);

        if (!spriteEnabledSomewhereOnLine(i))
        {
            traceVicSpriteSlotEvent(i, "prep-disabled", raster, currentCycle);
            clearSpriteFetchedRowState(i);
            continue;
        }

        if (!spriteUnits[i].dmaActive)
        {
            traceVicSpriteSlotEvent(i, "prep-inactive", raster, currentCycle);
            clearSpriteFetchedRowState(i);
            continue;
        }

        if (!spriteHasFetchedDisplayRow(i))
        {
            traceVicSpriteSlotEvent(i, "prep-no-row", raster, currentCycle);
            continue;
        }

        traceVicSpriteSlotEvent(i, "prep-output", raster, currentCycle);
        beginSpriteLineOutput(i, raster);
    }
}

int Vic::spritePreparedOutputWidth(int sprIndex) const
{
    (void)sprIndex;
    return 48;
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
    spriteUnits[sprIndex].outputBit = 0;
    spriteUnits[sprIndex].outputRepeat = 0;
    spriteUnits[sprIndex].outputXStart = spriteScreenXFor(sprIndex, raster);
    spriteUnits[sprIndex].outputWidth = spritePreparedOutputWidth(sprIndex);
}

void Vic::advanceSpriteOutputState(int sprIndex, int px)
{
    if (sprIndex < 0 || sprIndex >= 8)
        return;

    const bool expandX = spriteXExpandedAtPixel(sprIndex, px);
    const bool multClr = spriteMulticolorAtPixel(sprIndex, px);

    const int repeatsPerSourceUnit =
        multClr ? (expandX ? 4 : 2)
                : (expandX ? 2 : 1);

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

    const uint8_t bits =
        static_cast<uint8_t>((rowBits >> (22 - srcPair * 2)) & 0x03);

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
    clearSpriteLineBuffers();

    for (int spr = 0; spr < 8; ++spr)
    {
        if (!spriteUnits[spr].rowPrepared)
            continue;

        if (!spriteHasFetchedDisplayRow(spr))
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
        {
            // currentRow tracks physical raster lines of the expanded sprite.
            spriteUnits[s].currentRow += 1;
        }
        else
        {
            spriteUnits[s].currentRow = spriteRowFromMCBase(s);
        }

        if (isSpriteDMAComplete(s))
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

bool Vic::isSpriteDMAComplete(int spr) const
{
    return spriteUnits[spr].mcBase >= 63;
}

void Vic::resetSpriteDMAState(int spr)
{
    spriteUnits[spr].dmaActive = false;
    spriteUnits[spr].displayActive = false;
    spriteUnits[spr].yExpandLatch = false;

    spriteUnits[spr].currentRow = 0;
    spriteUnits[spr].mc = 0;
    spriteUnits[spr].mcBase = 0;
    spriteUnits[spr].startY = 0;

    resetSpriteLineOutputState(spr);
    clearSpriteFetchedRowState(spr);
}

void Vic::performSpriteDataFetches()
{
    const int raster = registers.raster;
    const int cycle = currentCycle;
    const int lineCycles = cfg_->cyclesPerLine;

    for (int s = 0; s < 8; ++s)
    {
        if (!spriteUnits[s].dmaActive)
            continue;

        if (!isSpriteDMAFetchCycle(s, cycle))
            continue;

        const int slotStart = spriteFetchSlotStart(s);
        const int firstDataCycle = (slotStart + 1) % lineCycles;

        int byteIndex = cycle - firstDataCycle;
        if (byteIndex < 0)
            byteIndex += lineCycles;

        if (byteIndex >= 0 && byteIndex < 3)
            fetchSpriteDataByte(s, byteIndex, raster);
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

bool Vic::isSpritePointerFetchCycle(int sprite, int cycle) const
{
    return cycle == spriteFetchSlotStart(sprite);
}

void Vic::updateSpriteDMAStartForCurrentLine(int raster)
{
    for (int s = 0; s < 8; ++s)
    {
        const bool enabled = ((registers.spriteEnabled >> s) & 0x01) != 0;
        const bool yExp = ((registers.spriteYExpansion >> s) & 0x01) != 0;
        const bool rasterMatch = (raster == registers.spriteY[s]);

        const bool alreadyActive = spriteUnits[s].dmaActive;
        const bool willStart = enabled && rasterMatch && !alreadyActive;

        traceVicSpriteStartCheck(s, raster, registers.spriteY[s], enabled, yExp, rasterMatch, willStart);

        if (!willStart)
            continue;

        spriteUnits[s].dmaActive = true;
        spriteUnits[s].displayActive = true;
        spriteUnits[s].yExpandLatch = yExp;
        spriteUnits[s].currentRow = 0;
        spriteUnits[s].mc = 0;
        spriteUnits[s].mcBase = 0;
        spriteUnits[s].startY = registers.spriteY[s];

        resetSpriteLineOutputState(s);
        clearSpriteFetchedRowState(s);

        traceVicSpriteDmaStart(s);
        traceVicSpriteSlotEvent(s, "dma-start", raster, currentCycle);
    }
}

void Vic::updateBusArbitration()
{
    const int raster = registers.raster;

    const bool badLineNow = isBadLine(raster);

    const bool baLow  = currentCycleSlot.baLow;
    const bool aecLow = currentCycleSlot.aecLow;

    const bool oldBA  = vicState.ba;
    const bool oldAEC = vicState.aec;

    vicState.badLine = badLineNow;
    vicState.ba      = !baLow;
    vicState.aec     = !aecLow;

    AEC = vicState.aec;

    if (processor)
        processor->setBAHold(!vicState.ba);

    if (oldBA != vicState.ba || oldAEC != vicState.aec)
    {
        traceVicBusArb(oldBA, oldAEC, vicState.ba, vicState.aec, badLineNow, baLow, aecLow);
    }
}

bool Vic::isBadLineCandidateForBusWarning(int raster) const
{
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return false;

    if (!denSeenOn30)
        return false;

    const uint8_t d011 = effectiveD011ForRaster(raster);

    if ((d011 & 0x10) == 0)   // DEN
        return false;

    const int yScroll = d011 & 0x07;

    // VIC-II bad lines are only possible in the fixed display window.
    // The window itself does not move with YSCROLL or RSEL.
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
    if (!vicState.badLineSampled)
        return false;

    return cycle >= cfg_->DMAStartCycle &&
           cycle <= cfg_->DMAEndCycle;
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

        const int slotStart = spriteFetchSlotStart(s);

        // Data byte 0 is the first CPU-visible sprite data steal in this model.
        const int firstCpuStealCycle = (slotStart + 1) % lineCycles;

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
        if (isSpritePointerFetchCycle(s, cycle))
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

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    if (!spriteUnits[sprite].dmaActive)
        return false;

    if (!isSpriteDMAFetchCycle(sprite, cycle))
        return false;

    const int byteIndex = spriteDataByteIndexForCycle(sprite, cycle);

    // Sprite row data is still fetched as 3 bytes, but only two of those
    // occupy the CPU-visible bus phase in this full-cycle approximation.
    //
    // byte 0: CPU-phase steal
    // byte 1: VIC-phase fetch, do not halt CPU in this model
    // byte 2: CPU-phase steal
    return byteIndex == 0 || byteIndex == 2;
}

bool Vic::shouldBALow(int raster, int cycle) const
{
    return isBadLineBusWarningCycle(raster, cycle) ||
           isBadLineBusStealCycle(raster, cycle)   ||
           isSpriteBusWarningCycle(raster, cycle)  ||
           isSpriteBusStealCycle(raster, cycle);
}

bool Vic::shouldAECLow(int raster, int cycle) const
{
    return isBadLineBusStealCycle(raster, cycle) ||
           isSpriteBusAECStealCycle(raster, cycle);
}

bool Vic::isBadLine(int raster) const
{
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return false;

    const uint8_t d011 = effectiveD011ForRaster(raster);

    if (!denSeenOn30)
        return false;

    if ((d011 & 0x10) == 0)   // DEN
        return false;

    const int yScroll = d011 & 0x07;

    // VIC-II bad lines are only possible in the fixed display window.
    // The window itself does not move with YSCROLL or RSEL.
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

    slot.badlineWarning = isBadLineBusWarningCycle(raster, cycle);
    slot.badlineSteal   = isBadLineBusStealCycle(raster, cycle);

    slot.spriteWarning  = isSpriteBusWarningCycle(raster, cycle);
    slot.spriteSteal    = isSpriteBusStealCycle(raster, cycle);
    slot.spriteAECSteal = isSpriteBusAECStealCycle(raster, cycle);

    slot.baLow = slot.badlineWarning || slot.badlineSteal   || slot.spriteWarning  || slot.spriteSteal;
    slot.aecLow = slot.badlineSteal || slot.spriteAECSteal;
    slot.rasterIrqSample = isRasterIRQCompareCycle(cycle);

    slot.busOwner = BusOwner::CPU;

    switch (slot.fetchKind)
    {
        case FetchKind::CharMatrix:
            slot.busOwner = BusOwner::BadLine;
            break;

        case FetchKind::SpritePtr0:
        case FetchKind::SpritePtr1:
        case FetchKind::SpritePtr2:
        case FetchKind::SpritePtr3:
        case FetchKind::SpritePtr4:
        case FetchKind::SpritePtr5:
        case FetchKind::SpritePtr6:
        case FetchKind::SpritePtr7:
            slot.busOwner = BusOwner::SpritePointer;
            break;

        case FetchKind::SpriteData0:
        case FetchKind::SpriteData1:
        case FetchKind::SpriteData2:
        case FetchKind::SpriteData3:
        case FetchKind::SpriteData4:
        case FetchKind::SpriteData5:
        case FetchKind::SpriteData6:
        case FetchKind::SpriteData7:
            slot.busOwner = BusOwner::SpriteData;
            break;

            case FetchKind::None:
            default:
                if (isRefreshCycle(cycle))
                    slot.busOwner = BusOwner::Refresh;
                else if (slot.baLow || slot.aecLow)
                    slot.busOwner = BusOwner::Idle;
                else
                    slot.busOwner = BusOwner::CPU;
                break;
    }

    return slot;
}

void Vic::beginBadLineFetch()
{
    vicState.rc = 0;

    vicState.displayEnabled = true;
    vicState.displayEnabledNext = true;

    vicState.vmliBase = vicState.vcBase;
    vicState.vmliFetchIndex = 0;

    activeMatrixRow.valid = true;
    activeMatrixRow.vcBase = vicState.vmliBase;
    activeMatrixRow.row = static_cast<int>(vicState.vmliBase / 40);
    activeMatrixRow.screen.fill(0);
    activeMatrixRow.color.fill(0);
    activeMatrixRow.fetched.fill(0);
}

void Vic::fetchBadLineMatrixByte(int fetchIndex, int raster)
{
    const uint16_t vc = static_cast<uint16_t>(vicState.vmliBase + fetchIndex);
    const int row = static_cast<int>(vc / 40);
    const int col = static_cast<int>(vc % 40);

    const uint8_t screenByte = fetchScreenByte(row, col, raster);
    const uint8_t colorByte  = fetchColorByte(row, col, raster) & 0x0F;

    charPtrFIFO[fetchIndex]  = screenByte;
    colorPtrFIFO[fetchIndex] = colorByte;

    if (fetchIndex >= 0 && fetchIndex < BACKGROUND_MATRIX_COLUMNS && activeMatrixRow.valid && activeMatrixRow.vcBase == vicState.vmliBase)
    {
        activeMatrixRow.screen[fetchIndex] = screenByte;
        activeMatrixRow.color[fetchIndex] = static_cast<uint8_t>(colorByte & 0x0F);
        activeMatrixRow.fetched[fetchIndex] = 1;
    }

    traceVicBadLineFetch(raster, currentCycle, fetchIndex, vc, row, col, screenByte, colorByte);
}

void Vic::renderLine(int raster)
{
    if (!IO_adapter || !mem)
        return;

    updateGraphicsMode(raster);

    // Build the pixel-aware border mask first so background generation,
    // color-event replay, and final composition can all use the same
    // CSEL-aware border state.
    buildBorderMaskLine(raster);
    generateBackgroundLine(raster);

    applyBackgroundColorEventsToLine(raster);
    applyExtendedBackgroundColorEventsToLine(raster);
    applySpriteColorEventsToLine(raster);
    buildSpritePriorityLine(raster);

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

std::string Vic::rasterEventDetail(const RasterEventRecord& e) const
{
    std::ostringstream out;

    auto screenBaseFromD018 = [](uint8_t value) -> uint16_t
    {
        return static_cast<uint16_t>((value & 0xF0) << 6);
    };

    auto charBaseFromD018 = [](uint8_t value) -> uint16_t
    {
        return static_cast<uint16_t>(((value >> 1) & 0x07) * 0x0800);
    };

    auto bitmapBaseFromD018 = [](uint8_t value) -> uint16_t
    {
        return static_cast<uint16_t>(((value >> 3) & 0x01) * 0x2000);
    };

    if (e.kind == RasterEventKind::SpriteX)
    {
        if (e.address == 0xD010)
        {
            out << "sprite X MSB";
            return out.str();
        }

        if (e.address >= 0xD000 && e.address <= 0xD00E && isSpriteX(e.address))
        {
            const int sprite = getSpriteIndex(e.address);
            out << "sprite " << sprite << " X low";
            return out.str();
        }
    }

    if (e.kind == RasterEventKind::Control)
    {
        const uint8_t oldVal = e.oldValue & 0x7F;
        const uint8_t newVal = e.newValue & 0x7F;

        const int raster = e.raster;
        const int nextRaster =
            (raster >= 0 && raster < static_cast<int>(cfg_->maxRasterLines))
                ? ((raster + 1) % static_cast<int>(cfg_->maxRasterLines))
                : raster;

        const uint8_t thisLatched =
            (raster >= 0 && raster < static_cast<int>(cfg_->maxRasterLines))
                ? latchedD011ForRaster(raster)
                : registers.control;

        const uint8_t thisEffective =
            (raster >= 0 && raster < static_cast<int>(cfg_->maxRasterLines))
                ? effectiveD011ForRaster(raster)
                : registers.control;

        const uint8_t nextLatched =
            (nextRaster >= 0 && nextRaster < static_cast<int>(cfg_->maxRasterLines))
                ? latchedD011ForRaster(nextRaster)
                : registers.control;

        const uint8_t nextEffective =
            (nextRaster >= 0 && nextRaster < static_cast<int>(cfg_->maxRasterLines))
                ? effectiveD011ForRaster(nextRaster)
                : registers.control;

        auto rselRows = [](uint8_t value) -> int
        {
            return (value & 0x08) ? 25 : 24;
        };

        auto denBit = [](uint8_t value) -> int
        {
            return (value & 0x10) ? 1 : 0;
        };

        out << "$D011"
            << " yscroll " << static_cast<int>(oldVal & 0x07)
            << "->" << static_cast<int>(newVal & 0x07)
            << " RSEL " << rselRows(oldVal)
            << "->" << rselRows(newVal)
            << " DEN " << denBit(oldVal)
            << "->" << denBit(newVal)
            << " this latched yscroll " << static_cast<int>(thisLatched & 0x07)
            << " this effective yscroll " << static_cast<int>(thisEffective & 0x07)
            << " next latched yscroll " << static_cast<int>(nextLatched & 0x07)
            << " next effective yscroll " << static_cast<int>(nextEffective & 0x07)
            << " this RSEL " << rselRows(thisLatched)
            << " next RSEL " << rselRows(nextLatched)
            << " this DEN " << denBit(thisLatched)
            << " next DEN " << denBit(nextLatched);

        return out.str();
    }

    if (e.kind == RasterEventKind::Control2)
    {
        const uint8_t oldVal = e.oldValue & 0x1F;
        const uint8_t newVal = e.newValue & 0x1F;

        out << "$D016"
            << " xscroll " << static_cast<int>(oldVal & 0x07)
            << "->" << static_cast<int>(newVal & 0x07)
            << " CSEL " << (((oldVal & 0x08) != 0) ? 40 : 38)
            << "->" << (((newVal & 0x08) != 0) ? 40 : 38)
            << " MCM " << (((oldVal & 0x10) != 0) ? 1 : 0)
            << "->" << (((newVal & 0x10) != 0) ? 1 : 0);

        return out.str();
    }

    if (e.kind == RasterEventKind::MemoryPointer)
    {
        const uint8_t oldVal = e.oldValue & 0xFE;
        const uint8_t newVal = e.newValue & 0xFE;

        const int raster = e.raster;
        const int nextRaster =
            (raster >= 0 && raster < static_cast<int>(cfg_->maxRasterLines))
                ? ((raster + 1) % static_cast<int>(cfg_->maxRasterLines))
                : raster;

        const uint8_t latchedVal =
            (raster >= 0 && raster < static_cast<int>(cfg_->maxRasterLines))
                ? latchedD018ForRaster(raster)
                : registers.memory_pointer;

        const uint8_t effectiveVal =
            (raster >= 0 && raster < static_cast<int>(cfg_->maxRasterLines))
                ? effectiveD018ForRaster(raster)
                : registers.memory_pointer;

        const uint8_t nextLatchedVal =
            (nextRaster >= 0 && nextRaster < static_cast<int>(cfg_->maxRasterLines))
                ? latchedD018ForRaster(nextRaster)
                : registers.memory_pointer;

        const uint8_t nextEffectiveVal =
            (nextRaster >= 0 && nextRaster < static_cast<int>(cfg_->maxRasterLines))
                ? effectiveD018ForRaster(nextRaster)
                : registers.memory_pointer;

        out << "$D018"
            << " screen $"
            << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
            << screenBaseFromD018(oldVal)
            << "->$"
            << std::setw(4) << screenBaseFromD018(newVal)
            << " char $"
            << std::setw(4) << charBaseFromD018(oldVal)
            << "->$"
            << std::setw(4) << charBaseFromD018(newVal)
            << " bitmap $"
            << std::setw(4) << bitmapBaseFromD018(oldVal)
            << "->$"
            << std::setw(4) << bitmapBaseFromD018(newVal)
            << " this latched char $"
            << std::setw(4) << charBaseFromD018(latchedVal)
            << " this effective char $"
            << std::setw(4) << charBaseFromD018(effectiveVal)
            << " next latched char $"
            << std::setw(4) << charBaseFromD018(nextLatchedVal)
            << " next effective char $"
            << std::setw(4) << charBaseFromD018(nextEffectiveVal)
            << std::dec << std::nouppercase << std::setfill(' ');

            return out.str();
        }

    return "";
}

std::string Vic::rasterRowStateDetail(int raster, bool preferPreviousFrame) const
{
    std::ostringstream out;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return "";

    const std::vector<RasterRowStateSnapshot>& primary =
        preferPreviousFrame ? lastFrameRasterRowStates : rasterRowStates;

    const std::vector<RasterRowStateSnapshot>& fallback =
        preferPreviousFrame ? rasterRowStates : lastFrameRasterRowStates;

    const RasterRowStateSnapshot* s = nullptr;

    if (raster < static_cast<int>(primary.size()) && primary[raster].valid)
        s = &primary[raster];
    else if (raster < static_cast<int>(fallback.size()) && fallback[raster].valid)
        s = &fallback[raster];

    if (!s)
        return " rowstate unavailable";

    const int rel = s->firstBadlineY >= 0 ? (raster - s->firstBadlineY) : -1;
    const int displayRow = rel >= 0 ? (rel / 8) : -1;

    const int fineY = static_cast<int>(s->d011 & 0x07);
    const int rasterLow3 = raster & 0x07;
    const bool badlineByFineY = (rasterLow3 == fineY);

    const int matrixRow = s->vcBase / 40;
    const int vmliRow = s->vmliBase / 40;

    out << " rowstate"
        << " firstBadlineY " << s->firstBadlineY
        << " fineY " << fineY
        << " rows " << (((s->d011 & 0x08) != 0) ? 25 : 24)
        << " rasterLow3 " << rasterLow3
        << " badlineByFineY " << (badlineByFineY ? 1 : 0)
        << " rc " << static_cast<int>(s->rc)
        << " vcBase " << s->vcBase
        << " matrixRow " << matrixRow
        << " vmliBase " << s->vmliBase
        << " vmliRow " << vmliRow
        << " vmliFetchIndex " << static_cast<int>(s->vmliFetchIndex)
        << " displayEnabled " << (s->displayEnabled ? 1 : 0)
        << " displayEnabledNext " << (s->displayEnabledNext ? 1 : 0)
        << " badLine " << (s->badLine ? 1 : 0)
        << " badLineSampled " << (s->badLineSampled ? 1 : 0)
        << " displayRowApprox " << displayRow;

    return out.str();
}

const char* Vic::rasterEventKindName(RasterEventKind kind) const
{
    switch (kind)
    {
        case RasterEventKind::Color:
            return "Color";

        case RasterEventKind::Control:
            return "Control $D011";

        case RasterEventKind::Control2:
            return "Control2 $D016";

        case RasterEventKind::MemoryPointer:
            return "Memory ptr $D018";

        case RasterEventKind::SpritePriority:
            return "Sprite priority";

        case RasterEventKind::SpriteMode:
            return "Sprite multicolor mode";

        case RasterEventKind::SpriteXExpansion:
            return "Sprite X expansion";

        case RasterEventKind::SpriteEnable:
            return "Sprite enable";

        case RasterEventKind::SpriteX:
            return "Sprite X position";
    }

    return "Unknown";
}

bool Vic::firstRasterPriorityEventValue(int raster, uint8_t& value) const
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

void Vic::buildSpritePriorityLine(int raster)
{
    const int xStart = rasterVisibleStartX(raster);
    const int xEnd   = rasterVisibleEndX(raster);

    for (auto& line : spriteBehindLine)
        line.fill(0);

    uint8_t activePriority = registers.spritePriority;

    if (!firstRasterPriorityEventValue(raster, activePriority))
    {
        for (int spr = 0; spr < 8; ++spr)
        {
            const uint8_t behind = ((activePriority >> spr) & 0x01) ? 1 : 0;

            for (int px = xStart; px < xEnd; ++px)
                spriteBehindLine[spr][px] = behind;
        }

        return;
    }

    int startX = xStart;

    for (const RasterPriorityEvent& e : rasterPriorityEvents)
    {
        if (e.raster != raster)
            continue;

        const int eventX = std::clamp(rasterEventPixelX(e.cycle), startX, xEnd);

        for (int spr = 0; spr < 8; ++spr)
        {
            const uint8_t behind = ((activePriority >> spr) & 0x01) ? 1 : 0;

            for (int px = startX; px < eventX; ++px)
                spriteBehindLine[spr][px] = behind;
        }

        activePriority = e.newValue;
        startX = eventX;
    }

    for (int spr = 0; spr < 8; ++spr)
    {
        const uint8_t behind = ((activePriority >> spr) & 0x01) ? 1 : 0;

        for (int px = startX; px < xEnd; ++px)
            spriteBehindLine[spr][px] = behind;
    }
}

bool Vic::spriteBehindBackgroundAtPixel(int sprite, int px) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    if (px < 0 || px >= 512)
        return false;

    return spriteBehindLine[sprite][px] != 0;
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
    activeBgPixel.rowBits = 0;
    activeBgPixel.fg = 0;
    activeBgPixel.bg0 = 0;
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
    activeBgPixel.pxBase = cell.px;
    activeBgPixel.py = cell.py;
    activeBgPixel.phase = 0;
}

Vic::BackgroundPixel Vic::sampleAndAdvanceActiveStandardTextPixel()
{
    BackgroundPixel out {};
    out.color = activeBgPixel.bg0 & 0x0F;
    out.opaque = false;

    if (!activeBgPixel.valid)
        return out;

    const int phase = activeBgPixel.phase;
    if (phase < 0 || phase >= 8)
        return out;

    const bool pixelOn = ((activeBgPixel.rowBits >> (7 - phase)) & 0x01) != 0;

    out.color = pixelOn ? (activeBgPixel.fg & 0x0F)
                        : (activeBgPixel.bg0 & 0x0F);
    out.opaque = pixelOn;

    activeBgPixel.phase++;
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

uint8_t Vic::fetchBackgroundPipelineTextRowBits() const
{
    if (!bgPipeline.valid || !mem)
        return 0;

    const int raster = bgPipeline.raster;

    const uint16_t charBase =
        charBaseForRasterPixelX(raster, bgPipeline.px);

    const uint16_t glyphAddr = static_cast<uint16_t>(
        charBase +
        static_cast<uint16_t>(bgPipeline.charCode) * 8 +
        static_cast<uint16_t>(bgPipeline.yInChar & 0x07)
    );

    return mem->vicRead(glyphAddr, raster);
}

Vic::BackgroundPixel Vic::sampleTextPipelinePixel() const
{
    BackgroundPixel out {};
    out.color = bgPipeline.bgColor0 & 0x0F;
    out.opaque = false;

    if (!bgPipeline.valid || bgPipeline.bitmap || bgPipeline.ecm)
        return out;

    if (!bgPipeline.multicolor)
    {
        const int bitIndex = 7 - (bgPipeline.pixelPhase & 0x07);
        const bool set = ((bgPipeline.rowBits >> bitIndex) & 0x01) != 0;

        if (set)
        {
            out.color = bgPipeline.fgColor & 0x0F;
            out.opaque = true;
        }

        return out;
    }

    const int phase = bgPipeline.pixelPhase & 0x07;
    const int pairIndex = phase / 2;
    const int shift = 6 - (pairIndex * 2);
    const uint8_t bits = static_cast<uint8_t>((bgPipeline.rowBits >> shift) & 0x03);

    switch (bits)
    {
        case 0x00:
            out.color = bgPipeline.bgColor0 & 0x0F;
            out.opaque = false;
            break;
        case 0x01:
            out.color = bgPipeline.bgColor1 & 0x0F;
            out.opaque = true;
            break;
        case 0x02:
            out.color = bgPipeline.bgColor2 & 0x0F;
            out.opaque = true;
            break;
        case 0x03:
            out.color = bgPipeline.fgColor & 0x0F;
            out.opaque = true;
            break;
    }

    return out;
}

Vic::BackgroundPixel Vic::sampleBitmapPipelinePixel() const
{
    BackgroundPixel out {};
    out.color = bgPipeline.bgColor0 & 0x0F;
    out.opaque = false;

    if (!bgPipeline.valid || !bgPipeline.bitmap || bgPipeline.ecm)
        return out;

    if (!bgPipeline.multicolor)
    {
        const int bitIndex = 7 - (bgPipeline.pixelPhase & 0x07);
        const bool set = ((bgPipeline.bitmapByte >> bitIndex) & 0x01) != 0;

        out.color = set ? (bgPipeline.fgColor & 0x0F)
                        : (bgPipeline.bgColor0 & 0x0F);
        out.opaque = set;
        return out;
    }

    const int phase = bgPipeline.pixelPhase & 0x07;
    const int pairIndex = phase / 2;
    const int shift = 6 - (pairIndex * 2);
    const uint8_t bits = static_cast<uint8_t>((bgPipeline.bitmapByte >> shift) & 0x03);

    switch (bits)
    {
        case 0x00:
            out.color = bgPipeline.bgColor0 & 0x0F;
            out.opaque = false;
            break;
        case 0x01:
            out.color = static_cast<uint8_t>((bgPipeline.screenByte >> 4) & 0x0F);
            out.opaque = true;
            break;
        case 0x02:
            out.color = static_cast<uint8_t>(bgPipeline.screenByte & 0x0F);
            out.opaque = true;
            break;
        case 0x03:
            out.color = bgPipeline.colorByte & 0x0F;
            out.opaque = true;
            break;
    }

    return out;
}

Vic::BackgroundPixel Vic::sampleECMPipelinePixel() const
{
    BackgroundPixel out {};
    out.color = bgPipeline.bgColor0 & 0x0F;
    out.opaque = false;

    if (!bgPipeline.valid || !bgPipeline.ecm)
        return out;

    const int bitIndex = 7 - (bgPipeline.pixelPhase & 0x07);
    const bool set = ((bgPipeline.rowBits >> bitIndex) & 0x01) != 0;

    if (set)
    {
        out.color = bgPipeline.fgColor & 0x0F;
        out.opaque = true;
    }
    else
    {
        out.color = bgPipeline.bgColor0 & 0x0F;
        out.opaque = false;
    }

    return out;
}

void Vic::resetActiveMatrixRow()
{
    activeMatrixRow.valid = false;
    activeMatrixRow.vcBase = 0;
    activeMatrixRow.row = -1;

    activeMatrixRow.screen.fill(0);
    activeMatrixRow.color.fill(0);
    activeMatrixRow.fetched.fill(0);
}

bool Vic::activeMatrixRowByteForDisplayCol(int displayCol,
                                           uint8_t& screenByte,
                                           uint8_t& colorByte) const
{
    if (displayCol < 0 || displayCol >= BACKGROUND_MATRIX_COLUMNS)
        return false;

    if (!vicState.displayEnabled)
        return false;

    if (!activeMatrixRow.valid)
        return false;

    const uint16_t expectedBase =
        static_cast<uint16_t>(currentDisplayRowBase());

    if (activeMatrixRow.vcBase != expectedBase)
        return false;

    if (!activeMatrixRow.fetched[displayCol])
        return false;

    screenByte = activeMatrixRow.screen[displayCol];
    colorByte  = static_cast<uint8_t>(activeMatrixRow.color[displayCol] & 0x0F);

    return true;
}

Vic::BackgroundPixel Vic::sampleBackgroundPipelinePixel() const
{
    if (!bgPipeline.valid)
    {
        BackgroundPixel out {};
        out.color = 0;
        out.opaque = false;
        return out;
    }

    if (bgPipeline.ecm)
        return sampleECMPipelinePixel();

    if (backgroundPipelineIsBitmapLike())
        return sampleBitmapPipelinePixel();

    if (backgroundPipelineIsTextLike())
        return sampleTextPipelinePixel();

    BackgroundPixel out {};
    out.color = bgPipeline.bgColor0 & 0x0F;
    out.opaque = false;
    return out;
}

void Vic::advanceBackgroundPipelinePixelPhase()
{
    bgPipeline.pixelPhase = (bgPipeline.pixelPhase + 1) & 0x07;
}

void Vic::rewindBackgroundPipelinePixelPhase()
{
    bgPipeline.pixelPhase = 0;
}

std::array<Vic::BackgroundPixel, 8> Vic::sampleBackgroundPipelineRow() const
{
    std::array<BackgroundPixel, 8> out {};

    if (!bgPipeline.valid)
        return out;

    BackgroundPipelineState saved = bgPipeline;

    for (int i = 0; i < 8; ++i)
    {
        out[i] = sampleBackgroundPipelinePixel();
        const_cast<Vic*>(this)->advanceBackgroundPipelinePixelPhase();
    }

    const_cast<Vic*>(this)->bgPipeline = saved;
    return out;
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

void Vic::stampStandardTextRowBits(int pxBase, int py, uint8_t rowBits, uint8_t fg, uint8_t bg, int x0, int x1)
{
    const int startPx = std::max(pxBase, x0);
    const int endPx   = std::min(pxBase + 8, x1);

    for (int px = startPx; px < endPx; ++px)
    {
        const int bit = px - pxBase;
        const bool pixelOn = ((rowBits >> (7 - bit)) & 0x01) != 0;

        stampBackgroundPixel(px, py, pixelOn ? (fg & 0x0F) : (bg & 0x0F), pixelOn);
    }
}

void Vic::stampStandardTextRowBitsFromPhase(int pxBase, int py, uint8_t rowBits, uint8_t fg, uint8_t bg, int x0, int x1, int startPhase, int endPhase)
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
        stampBackgroundPixel(px, py, pixelOn ? (fg & 0x0F) : (bg & 0x0F), pixelOn);
    }
}

void Vic::stampStandardTextPipelineSpan(int pxBase, int py, uint8_t rowBits, uint8_t fg, uint8_t bg, int x0, int x1, int& phase, int pixelCount)
{
    if (pixelCount <= 0)
        return;

    const int startPhase = std::clamp(phase, 0, 8);
    const int endPhase   = std::clamp(startPhase + pixelCount, 0, 8);

    stampStandardTextRowBitsFromPhase(pxBase, py, rowBits, fg, bg, x0, x1, startPhase, endPhase);

    phase = endPhase;
}

void Vic::stampMulticolorTextRowBits(int pxBase, int py, uint8_t rowBits,
                                     uint8_t bg0, uint8_t bg1, uint8_t bg2, uint8_t cellColor,
                                     int x0, int x1)
{
    const int startPx = std::max(pxBase, x0);
    const int endPx   = std::min(pxBase + 8, x1);

    for (int px = startPx; px < endPx; ++px)
    {
        const int localX = px - pxBase;
        const int pairIndex = localX >> 1;
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

void Vic::stampMulticolorTextRowBitsFromPhase(int pxBase, int py, uint8_t rowBits,
                                              uint8_t bg0, uint8_t bg1, uint8_t bg2, uint8_t cellColor,
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

void Vic::stampStandardBitmapRowBits(int pxBase, int py, uint8_t rowBits,
                                     uint8_t fg, uint8_t bg,
                                     int x0, int x1)
{
    const int startPx = std::max(pxBase, x0);
    const int endPx   = std::min(pxBase + 8, x1);

    for (int px = startPx; px < endPx; ++px)
    {
        const int bit = px - pxBase;
        const bool pixelOn = ((rowBits >> (7 - bit)) & 0x01) != 0;

        stampBackgroundPixelSource(
            px,
            py,
            pixelOn ? (fg & 0x0F) : (bg & 0x0F),
            pixelOn,
            BackgroundSource::Bitmap
        );
    }
}

void Vic::stampStandardBitmapRowBitsFromPhase(int pxBase, int py, uint8_t rowBits,
                                              uint8_t fg, uint8_t bg,
                                              int x0, int x1,
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

void Vic::stampMulticolorBitmapRowBits(int pxBase, int py, uint8_t rowBits,
                                       uint8_t c00, uint8_t c01, uint8_t c10, uint8_t c11,
                                       int x0, int x1)
{
    const int startPx = std::max(pxBase, x0);
    const int endPx   = std::min(pxBase + 8, x1);

    for (int px = startPx; px < endPx; ++px)
    {
        const int localX = px - pxBase;
        const int pairIndex = localX >> 1;
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

        stampBackgroundPixelSource(px, py, color, opaque, multicolorBitmapSourceForBits(bits));
    }
}

void Vic::stampMulticolorBitmapRowBitsFromPhase(int pxBase, int py, uint8_t rowBits,
                                                uint8_t c00, uint8_t c01, uint8_t c10, uint8_t c11,
                                                int x0, int x1,
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

        stampBackgroundPixelSource(
            px,
            py,
            color,
            opaque,
            multicolorBitmapSourceForBits(bits)
        );
    }
}

void Vic::stampMulticolorBitmapPipelineSpan(int pxBase, int py, uint8_t rowBits,
                                            uint8_t c00, uint8_t c01, uint8_t c10, uint8_t c11,
                                            int x0, int x1,
                                            int& phase, int pixelCount)
{
    if (pixelCount <= 0)
        return;

    const int startPhase = std::clamp(phase, 0, 8);
    const int endPhase   = std::clamp(startPhase + pixelCount, 0, 8);

    stampMulticolorBitmapRowBitsFromPhase(pxBase, py, rowBits, c00, c01, c10, c11,
                                          x0, x1, startPhase, endPhase);

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

void Vic::stampECMRowBits(int pxBase, int py, uint8_t rowBits,
                          uint8_t fg, uint8_t bg,
                          BackgroundSource bgSource,
                          int x0, int x1)
{
    const int startPx = std::max(pxBase, x0);
    const int endPx   = std::min(pxBase + 8, x1);

    for (int px = startPx; px < endPx; ++px)
    {
        const int bit = px - pxBase;
        const bool pixelOn = ((rowBits >> (7 - bit)) & 0x01) != 0;

        stampBackgroundPixelSource(
            px,
            py,
            pixelOn ? (fg & 0x0F) : (bg & 0x0F),
            pixelOn,
            pixelOn ? BackgroundSource::Foreground : bgSource
        );
    }
}

void Vic::stampECMRowBitsFromPhase(int pxBase, int py, uint8_t rowBits,
                                   uint8_t fg, uint8_t bg,
                                   BackgroundSource bgSource,
                                   int x0, int x1,
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
            pixelOn ? BackgroundSource::Foreground : bgSource
        );
    }
}

void Vic::stampECMPipelineSpan(int pxBase, int py, uint8_t rowBits,
                               uint8_t fg, uint8_t bg,
                               BackgroundSource bgSource,
                               int x0, int x1,
                               int& phase, int pixelCount)
{
    if (pixelCount <= 0)
        return;

    const int startPhase = std::clamp(phase, 0, 8);
    const int endPhase   = std::clamp(startPhase + pixelCount, 0, 8);

    stampECMRowBitsFromPhase(
        pxBase,
        py,
        rowBits,
        fg,
        bg,
        bgSource,
        x0,
        x1,
        startPhase,
        endPhase
    );

    phase = endPhase;
}

Vic::BackgroundSource Vic::ecmBackgroundSourceForCharIndex(uint8_t charIndex) const
{
    switch ((charIndex >> 6) & 0x03)
    {
        case 0x00: return BackgroundSource::BG0; // $D021
        case 0x01: return BackgroundSource::BG1; // $D022
        case 0x02: return BackgroundSource::BG2; // $D023
        case 0x03: return BackgroundSource::BG3; // $D024
    }

    return BackgroundSource::Unknown;
}

void Vic::stampBackgroundPixel(int px, int py, uint8_t color, bool opaque)
{
    stampBackgroundPixelSource(
        px,
        py,
        color,
        opaque,
        opaque ? BackgroundSource::Foreground : BackgroundSource::BG0
    );
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

    const uint8_t screenByte = resolveDisplayScreenByte(displayCol, raster);
    const uint8_t colorByte  = resolveDisplayColorByte(displayCol, raster);

    const uint8_t bgColor =
        static_cast<uint8_t>(registers.backgroundColor0 & 0x0F);

    const uint8_t d016AtCell =
        d016ForRasterPixelX(raster, px, false);

    const bool multicolor =
        ((d016AtCell & 0x10) != 0) &&
        ((colorByte & 0x08) != 0);

    const uint8_t d018 =
        d018ForRasterPixelX(raster, px, false) & 0xFE;

    const uint16_t charBase =
        static_cast<uint16_t>(((d018 >> 1) & 0x07) * 0x0800);

    const uint16_t charAddr =
        static_cast<uint16_t>(
            charBase +
            static_cast<uint16_t>(screenByte) * 8 +
            static_cast<uint16_t>(yInChar & 0x07)
        );

    const uint8_t rowBits =
        mem ? mem->vicRead(charAddr, raster) : 0x00;

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

Vic::BackgroundPixel Vic::sampleStandardTextPixel(const TextCellSample& cell, int px, int raster) const
{
    (void)raster;

    BackgroundPixel out {};
    out.color = static_cast<uint8_t>(cell.bgColor & 0x0F);
    out.opaque = false;

    if (!cell.valid || cell.multicolor)
        return out;

    if (px < cell.px || px >= cell.px + 8)
        return out;

    const uint8_t rowBits = cell.rowBits;

    const int col = px - cell.px;
    const bool pixelOn = ((rowBits >> (7 - col)) & 0x01) != 0;

    out.color = pixelOn
        ? static_cast<uint8_t>(cell.colorByte & 0x0F)
        : static_cast<uint8_t>(cell.bgColor & 0x0F);

    out.opaque = pixelOn;
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

    const uint8_t screenByte = resolveDisplayScreenByte(displayCol, raster);
    const uint8_t colorByte  = resolveDisplayColorByte(displayCol, raster);

    const uint16_t cellIndex =
        static_cast<uint16_t>(charRow * BACKGROUND_MATRIX_COLUMNS + displayCol);

    const uint16_t bitmapBase =
        getLatchedBitmapBase(raster);

    const uint16_t addr =
        static_cast<uint16_t>(bitmapBase + cellIndex * 8 + yInChar);

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

void Vic::drawStandardTextCell(const TextCellSample& cell, int raster, int x0, int x1)
{
    (void)raster;

    if (!cell.valid || cell.multicolor)
        return;

    const uint8_t rowBits = cell.rowBits;

    updateOpenBus(rowBits);

    const uint8_t fg = static_cast<uint8_t>(cell.colorByte & 0x0F);
    const uint8_t bg = static_cast<uint8_t>(cell.bgColor & 0x0F);

    stampStandardTextRowBits(cell.px, cell.py, rowBits, fg, bg, x0, x1);
}

void Vic::drawStandardTextCellViaPipeline(const TextCellSample& cell, int raster, int x0, int x1)
{
    bgPipeline.pixelPhase = 0;

    for (int i = 0; i < 8; ++i)
        drawStandardTextCellViaPipelineBudgeted(cell, raster, x0, x1, 1);
}

void Vic::drawStandardTextCellViaPipelineBudgeted(const TextCellSample& cell, int raster, int x0, int x1, int pixelBudget)
{
    (void)raster;

    if (!cell.valid || cell.multicolor || pixelBudget <= 0)
        return;

    const uint8_t rowBits = bgPipeline.rowBits;
    const uint8_t fg      = bgPipeline.fgColor & 0x0F;
    const uint8_t bg      = bgPipeline.bgColor0 & 0x0F;

    int phase = std::clamp(bgPipeline.pixelPhase, 0, 8);
    stampStandardTextPipelineSpan(cell.px, cell.py, rowBits, fg, bg, x0, x1, phase, pixelBudget);
    bgPipeline.pixelPhase = phase;
}

void Vic::drawStandardTextCellViaActivePixelStateBudgeted(const TextCellSample& cell, int raster, int x0, int x1, int pixelBudget, bool reloadState)
{
    if (!cell.valid || cell.multicolor || pixelBudget <= 0)
        return;

    if (reloadState)
        loadActiveStandardTextPixelState(cell, raster);

    if (!activeBgPixel.valid)
        return;

    for (int i = 0; i < pixelBudget; ++i)
    {
        if (activeBgPixel.phase >= 8)
            break;

        const int px = cell.px + activeBgPixel.phase;
        const BackgroundPixel pixel = sampleAndAdvanceActiveStandardTextPixel();

        if (px >= x0 && px < x1)
            stampBackgroundPixel(px, cell.py, pixel.color, pixel.opaque);
    }
}

void Vic::drawMulticolorTextCell(const TextCellSample& cell, int raster, int x0, int x1)
{
    (void)raster;

    if (!cell.valid || !cell.multicolor)
        return;

    const uint8_t rowBits = cell.rowBits;

    updateOpenBus(rowBits);

    const uint8_t bg0 = static_cast<uint8_t>(cell.bgColor & 0x0F);
    const uint8_t bg1 = static_cast<uint8_t>(registers.backgroundColor[0] & 0x0F);
    const uint8_t bg2 = static_cast<uint8_t>(registers.backgroundColor[1] & 0x0F);
    const uint8_t cellColor = static_cast<uint8_t>(cell.colorByte & 0x07);

    stampMulticolorTextRowBits(cell.px, cell.py, rowBits, bg0, bg1, bg2, cellColor, x0, x1);
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
    stampMulticolorTextPipelineSpan(cell.px, cell.py, rowBits, bg0, bg1, bg2, cellColor,
                                    x0, x1, phase, 8);
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
            if (bgPipelineConfig.standardText)
            {
                loadActiveStandardTextPixelState(cell, raster);

                while (!activeStandardTextPixelStateFinished())
                    emitStandardTextCyclePixelsBudgeted(g.x0, g.x1, 1);
            }
            else
            {
                drawStandardTextCell(cell, raster, g.x0, g.x1);
            }
        }
        else
        {
            if (bgPipelineConfig.multicolorText)
                drawMulticolorTextCellViaPipeline(cell, raster, g.x0, g.x1);
            else
                drawMulticolorTextCell(cell, raster, g.x0, g.x1);
        }
    }
}

Vic::BackgroundPixel Vic::sampleBitmapPixel(const BitmapCellSample& cell, int px) const
{
    BackgroundPixel out {};
    out.color = static_cast<uint8_t>(cell.screenByte & 0x0F);
    out.opaque = false;

    if (!cell.valid)
        return out;

    if (px < cell.px || px >= cell.px + 8)
        return out;

    const int bit = px - cell.px;
    const bool pixelOn = ((cell.bitmapByte >> (7 - bit)) & 0x01) != 0;

    const uint8_t fgColor = static_cast<uint8_t>((cell.screenByte >> 4) & 0x0F);
    const uint8_t bgColor = static_cast<uint8_t>(cell.screenByte & 0x0F);

    out.color = pixelOn ? fgColor : bgColor;
    out.opaque = pixelOn;
    return out;
}

void Vic::drawBitmapCell(const BitmapCellSample& cell, int raster, int x0, int x1)
{
    (void)raster;

    if (!cell.valid)
        return;

    const uint8_t rowBits = cell.bitmapByte;
    const uint8_t fg = static_cast<uint8_t>((cell.screenByte >> 4) & 0x0F);
    const uint8_t bg = static_cast<uint8_t>(cell.screenByte & 0x0F);

    stampStandardBitmapRowBits(cell.px, cell.py, rowBits, fg, bg, x0, x1);
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

        if (bgPipelineConfig.standardBitmap)
            drawBitmapCellViaPipeline(cell, raster, g.x0, g.x1);
        else
            drawBitmapCell(cell, raster, g.x0, g.x1);
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

    const uint8_t screenByte = resolveDisplayScreenByte(displayCol, raster);
    const uint8_t colorByte  = resolveDisplayColorByte(displayCol, raster);

    const uint16_t cellIndex =
        static_cast<uint16_t>(charRow * BACKGROUND_MATRIX_COLUMNS + displayCol);

    const uint16_t bitmapBase =
        getLatchedBitmapBase(raster);

    const uint16_t addr =
        static_cast<uint16_t>(bitmapBase + cellIndex * 8 + yInChar);

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

Vic::BackgroundPixel Vic::sampleMultiColorBitmapPixel(const MultiColorBitmapCellSample& cell, int px) const
{
    BackgroundPixel out {};
    out.color = static_cast<uint8_t>(registers.backgroundColor0 & 0x0F);
    out.opaque = false;

    if (!cell.valid)
        return out;

    if (px < cell.px || px >= cell.px + 8)
        return out;

    const int localX = px - cell.px;
    const int pairIndex = localX >> 1;
    const int shift = 6 - pairIndex * 2;
    const uint8_t bits = static_cast<uint8_t>((cell.bitmapByte >> shift) & 0x03);

    const uint8_t color00 = static_cast<uint8_t>(registers.backgroundColor0 & 0x0F);
    const uint8_t color01 = static_cast<uint8_t>((cell.screenByte >> 4) & 0x0F);
    const uint8_t color10 = static_cast<uint8_t>(cell.screenByte & 0x0F);
    const uint8_t color11 = static_cast<uint8_t>(cell.colorByte & 0x0F);

    switch (bits)
    {
        case 0x00:
            out.color = color00;
            out.opaque = false;
            break;

        case 0x01:
            out.color = color01;
            out.opaque = true;
            break;

        case 0x02:
            out.color = color10;
            out.opaque = true;
            break;

        case 0x03:
            out.color = color11;
            out.opaque = true;
            break;
    }

    return out;
}

void Vic::drawMultiColorBitmapCell(const MultiColorBitmapCellSample& cell, int raster, int x0, int x1)
{
    (void)raster;

    if (!cell.valid)
        return;

    const uint8_t rowBits = cell.bitmapByte;
    const uint8_t c00 = static_cast<uint8_t>(registers.backgroundColor0 & 0x0F);
    const uint8_t c01 = static_cast<uint8_t>((cell.screenByte >> 4) & 0x0F);
    const uint8_t c10 = static_cast<uint8_t>(cell.screenByte & 0x0F);
    const uint8_t c11 = static_cast<uint8_t>(cell.colorByte & 0x0F);

    stampMulticolorBitmapRowBits(cell.px, cell.py, rowBits,
                                 c00, c01, c10, c11, x0, x1);
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
    stampMulticolorBitmapPipelineSpan(cell.px, cell.py, rowBits, c00, c01, c10, c11,
                                      x0, x1, phase, 8);
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

        if (bgPipelineConfig.multicolorBitmap)
            drawMultiColorBitmapCellViaPipeline(cell, raster, g.x0, g.x1);
        else
            drawMultiColorBitmapCell(cell, raster, g.x0, g.x1);
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

    const uint8_t scrByte   = resolveDisplayScreenByte(displayCol, raster);
    const uint8_t colorByte = resolveDisplayColorByte(displayCol, raster);

    // ECM:
    // bits 0-5 = character index
    // bits 6-7 = background color select
    const uint8_t charIndex = static_cast<uint8_t>(scrByte & 0x3F);
    const uint8_t bgSel     = static_cast<uint8_t>((scrByte >> 6) & 0x03);

    const uint16_t charBase =
        charBaseForRasterPixelX(raster, px);

    const uint16_t charAddr =
        static_cast<uint16_t>(
            charBase +
            static_cast<uint16_t>(charIndex) * 8 +
            static_cast<uint16_t>(yInChar & 0x07)
        );

    const uint8_t rowBits =
        mem ? mem->vicRead(charAddr, raster) : 0x00;

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

void Vic::drawECMCell(const ECMCellSample& cell, int raster, int x0, int x1)
{
    (void)raster;

    if (!cell.valid)
        return;

    const uint8_t rowBits = cell.rowBits;

    updateOpenBus(rowBits);

    const uint8_t fg = static_cast<uint8_t>(cell.fgColor & 0x0F);
    const uint8_t bg = static_cast<uint8_t>(cell.bgColor & 0x0F);

    stampECMRowBits(cell.px, cell.py, rowBits, fg, bg, cell.bgSource, x0, x1);
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

    stampECMPipelineSpan(
        cell.px,
        cell.py,
        rowBits,
        fg,
        bg,
        bgPipeline.bgSource,
        x0,
        x1,
        phase,
        8
    );
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

        if (bgPipelineConfig.ecm)
            drawECMCellViaPipeline(cell, raster, g.x0, g.x1);
        else
            drawECMCell(cell, raster, g.x0, g.x1);
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

    const BorderWindow w = borderWindowForRaster(raster);

    // If display is effectively closed, leave border-filled line buffer.
    if (!DEN || w.vertical)
    {
        return;
    }

    // Fill the interior with background color first for non-bitmap modes.
    const graphicsMode lineMode = graphicsModeForRaster(raster);

    if (!(lineMode == graphicsMode::bitmap || lineMode == graphicsMode::multiColorBitmap))
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
        case graphicsMode::multiColor:
            renderTextLine(raster, lineXScroll);
            break;
        case graphicsMode::bitmap:
            renderBitmapLine(raster, lineXScroll);
            break;
        case graphicsMode::multiColorBitmap:
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
    if (!IO_adapter)
        return;

    const int screenY = fbY(raster);

    const int xStart = rasterVisibleStartX(raster);
    const int xEnd   = rasterVisibleEndX(raster);

    for (int px = xStart; px < xEnd; ++px)
    {
        IO_adapter->setPixel(px, screenY, finalColorLine[px] & 0x0F);
    }
}

void Vic::emitRasterPixel(int raster, int px)
{
    if (!IO_adapter)
        return;

    const int screenY = fbY(raster);
    IO_adapter->setPixel(px, screenY, produceRasterPixel(raster, px));
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
            stampBackgroundPixel(px, activeBgPixel.py, pixel.color, pixel.opaque);
    }
}

void Vic::emitStandardTextCyclePixels(int x0, int x1)
{
    emitStandardTextCyclePixelsBudgeted(x0, x1, 8);
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

    if (borderVertical_per_raster[raster] != 0)
        return false;

    return borderMaskLine[px] == 0;
}

void Vic::buildBorderMaskLine(int raster)
{
    std::fill(borderMaskLine.begin(),
              borderMaskLine.begin() + VISIBLE_WIDTH,
              1);

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    if (borderVertical_per_raster[raster] != 0)
        return;

    bool inBorder = true;

    for (int px = 0; px < VISIBLE_WIDTH; ++px)
    {
        const uint8_t d016 =
            d016ForRasterPixelX(raster, px, false);

        const bool csel40 = (d016 & 0x08) != 0;

        const HorizontalBorderWindow w =
            horizontalBorderWindowForCSEL(csel40);

        // Open transition: only happens when the current pixel reaches
        // the active CSEL opening comparison point.
        if (inBorder && px == w.openX)
            inBorder = false;

        // Close transition: only happens when the current pixel reaches
        // the active CSEL closing comparison point.
        if (!inBorder && px == w.closeX)
            inBorder = true;

        borderMaskLine[px] = inBorder ? 1 : 0;
    }
}

void Vic::composeFinalRasterLine(int raster)
{
    const int xStart = rasterVisibleStartX(raster);
    const int xEnd   = rasterVisibleEndX(raster);

    for (int px = xStart; px < xEnd; ++px)
    {
        latchSpriteBackgroundCollisionsAtPixel(raster, px);
        finalColorLine[px] = compositePixelAtX(raster, px);
    }
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

uint8_t Vic::produceRasterPixel(int raster, int px) const
{
    (void)raster;
    return finalColorLine[px] & 0x0F;
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
    if (currentCycle == 0)
        return static_cast<uint16_t>((registers.raster + 1) % cfg_->maxRasterLines);

    return registers.raster;
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

void Vic::triggerRasterIRQIfMatched()
{
    const bool matched = rasterCompareMatchesNow();
    const bool pending = (registers.interruptStatus & 0x01) != 0;
    const bool enabled = (registers.interruptEnable & 0x01) != 0;

    if (!matched)
        return;

    if (logger && setLogging)
    {
        std::ostringstream oss;
        oss << "[VIC:IRQ] trigger-check"
            << " raster=" << registers.raster
            << " cycle=" << currentCycle
            << " target=" << registers.rasterInterruptLine
            << " matched=1"
            << " pendingBefore=" << (pending ? 1 : 0)
            << " enabled=" << (enabled ? 1 : 0);

        logger->WriteLog(oss.str());
    }

    if (!pending)
        raiseVicIRQSource(0x01);
}

void Vic::raiseVicIRQSource(uint8_t sourceBitMask)
{
    const uint8_t masked = sourceBitMask & 0x0F;
    if (masked == 0)
        return;

    const uint8_t newlySet = masked & ~registers.interruptStatus;
    if (newlySet == 0)
        return;

    const uint8_t oldStatus = registers.interruptStatus;

    registers.interruptStatus |= newlySet;
    updateIRQLine();

    if (logger && setLogging)
    {
        std::ostringstream oss;
        oss << "[VIC:IRQ] raise"
            << " raster=" << registers.raster
            << " cycle=" << currentCycle
            << " source=$"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << int(masked)
            << " oldIFR=$" << std::setw(2) << int(oldStatus)
            << " newIFR=$" << std::setw(2) << int(registers.interruptStatus)
            << " IER=$" << std::setw(2) << int(registers.interruptEnable)
            << std::dec << std::nouppercase << std::setfill(' ');

        logger->WriteLog(oss.str());
    }
}

void Vic::checkRasterIRQCompareTransition(uint16_t oldLine, uint16_t newLine)
{
    oldLine &= 0x01FF;
    newLine &= 0x01FF;

    if (oldLine == newLine)
        return;

    // If the new target is outside the current video mode's raster range,
    // it cannot match the current raster.
    if (newLine >= cfg_->maxRasterLines)
        return;

    // If this raster's compare point has already happened, a retarget to the
    // current raster should not immediately fire from the register write path.
    if (rasterIrqSampledThisLine || currentCycle >= rasterIRQCompareCycle())
        return;

    if (visibleRasterForIRQCompare() != newLine)
        return;

    // Do not trigger here. The upcoming normal compare sample will see the
    // new target at the hardware compare point.
    if (logger && setLogging)
    {
        std::ostringstream oss;
        oss << "[VIC:IRQ] retarget armed"
            << " raster=" << registers.raster
            << " cycle=" << currentCycle
            << " oldTarget=" << oldLine
            << " newTarget=" << newLine
            << " compareCycle=" << rasterIRQCompareCycle();

        logger->WriteLog(oss.str());
    }
}

void Vic::sampleRasterIRQCompare(const char* reason)
{
    if (rasterIrqSampledThisLine)
        return;

    const char* sampleReason =
        reason ? reason : "normal-sample";

    // Capture everything used by the comparator at the sample point.
    const uint16_t visibleRaster =
        visibleRasterForIRQCompare();

    const uint16_t targetRaster =
        static_cast<uint16_t>(registers.rasterInterruptLine & 0x01FF);

    const bool targetInRange =
        targetRaster < cfg_->maxRasterLines;

    const bool sampledBefore =
        rasterIrqSampledThisLine;

    // Use the captured values, not a second helper call.
    const bool matched =
        targetInRange && (visibleRaster == targetRaster);

    lastRasterIRQSample.valid = true;
    lastRasterIRQSample.raster = static_cast<int>(registers.raster);
    lastRasterIRQSample.cycle = currentCycle;
    lastRasterIRQSample.visibleRaster = visibleRaster;
    lastRasterIRQSample.targetRaster = targetRaster;
    lastRasterIRQSample.targetInRange = targetInRange;
    lastRasterIRQSample.matched = matched;
    lastRasterIRQSample.sampledBefore = sampledBefore;
    lastRasterIRQSample.reason = sampleReason;

    traceVicCycleCheckpoint(
        "raster-irq-sample",
        registers.raster,
        currentCycle
    );

    traceVicRasterRetargetTest(
        sampleReason,
        targetRaster,
        targetRaster,
        sampledBefore,
        matched
    );

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

    // Preserve the 9-bit programmed IRQ target.
    // Do not modulo it to maxRasterLines. If the target is outside
    // the machine's raster range, the normal compare simply will not match.
    registers.rasterInterruptLine = static_cast<uint16_t>(newLine & 0x01FF);

    if (logger && setLogging)
    {
        std::ostringstream oss;
        oss << "[VIC:IRQ] target update"
            << " reason=" << (reason ? reason : "unknown")
            << " raster=" << registers.raster
            << " cycle=" << currentCycle
            << " value=$"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(writtenValue)
            << std::dec << std::nouppercase << std::setfill(' ')
            << " oldTarget=" << oldLine
            << " newTarget=" << registers.rasterInterruptLine
            << " targetInRange=" << (rasterIRQTargetInRange() ? 1 : 0)
            << " sampledThisLine=" << (rasterIrqSampledThisLine ? 1 : 0)
            << " compareMatchNow=" << (rasterCompareMatchesNow() ? 1 : 0);

        logger->WriteLog(oss.str());
    }

    checkRasterIRQCompareTransition(oldLine, registers.rasterInterruptLine);
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
    return cycle == rasterIRQCompareCycle();
}

void Vic::detectSpriteToSpriteCollision(int raster)
{
    uint8_t collisionBits = 0;
    int firstCollisionX = -1;

    for (int i = 0; i < 8; ++i)
    {
        if (!spriteEnabledSomewhereOnLine(i))
            continue;

        for (int j = i + 1; j < 8; ++j)
        {
            if (!spriteEnabledSomewhereOnLine(j))
                continue;

            const int x = firstSpriteSpriteCollisionXOnLine(i, j, raster);
            if (x < 0)
                continue;

            collisionBits =
                static_cast<uint8_t>(collisionBits | (1 << i) | (1 << j));

            if (firstCollisionX < 0 || x < firstCollisionX)
                firstCollisionX = x;
        }
    }

    latchSpriteSpriteCollision(collisionBits, raster, firstCollisionX);
}

bool Vic::checkSpriteSpriteOverlapOnLine(int A, int B, int raster)
{
    return firstSpriteSpriteCollisionXOnLine(A, B, raster) >= 0;
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

void Vic::detectSpriteToBackgroundCollision(int raster)
{
    uint8_t collisionBits = 0;
    int firstCollisionX = -1;

    for (int i = 0; i < 8; ++i)
    {
        if (!spriteEnabledSomewhereOnLine(i))
            continue;

        const int x = firstSpriteBackgroundCollisionXOnLine(i, raster);
        if (x < 0)
            continue;

        collisionBits =
            static_cast<uint8_t>(collisionBits | (1 << i));

        if (firstCollisionX < 0 || x < firstCollisionX)
            firstCollisionX = x;
    }

    latchSpriteBackgroundCollision(collisionBits, raster, firstCollisionX);
}

bool Vic::checkSpriteBackgroundOverlap(int spriteIndex, int raster)
{
    return firstSpriteBackgroundCollisionXOnLine(spriteIndex, raster) >= 0;
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

    if (!spriteUnits[sprIndex].displayActive)
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

int Vic::firstSpriteSpriteCollisionXOnLine(int A, int B, int raster) const
{
    if (A < 0 || A >= 8 || B < 0 || B >= 8 || A == B)
        return -1;

    int ra = 0;
    int rb = 0;
    int fbLine = 0;

    if (!spriteDisplayCoversRaster(A, raster, ra, fbLine))
        return -1;

    if (!spriteDisplayCoversRaster(B, raster, rb, fbLine))
        return -1;

    if (!spriteUnits[A].rowPrepared || !spriteUnits[B].rowPrepared)
        return -1;

    const int a0 = spriteUnits[A].outputXStart;
    const int a1 = a0 + spriteUnits[A].outputWidth;

    const int b0 = spriteUnits[B].outputXStart;
    const int b1 = b0 + spriteUnits[B].outputWidth;

    const int startX = std::max(a0, b0);
    const int endX   = std::min(a1, b1);

    if (startX >= endX)
        return -1;

    const int clippedStart = std::max(startX, 0);
    const int clippedEnd   = std::min(endX, VISIBLE_WIDTH);

    for (int px = clippedStart; px < clippedEnd; ++px)
    {
        if (spriteOpaqueLine[A][px] && spriteOpaqueLine[B][px])
            return px;
    }

    return -1;
}

int Vic::firstSpriteBackgroundCollisionXOnLine(int spriteIndex, int raster) const
{
    if (spriteIndex < 0 || spriteIndex >= 8)
        return -1;

    int rowInSprite = 0;
    int fbLine = 0;

    if (!spriteDisplayCoversRaster(spriteIndex, raster, rowInSprite, fbLine))
        return -1;

    if (!spriteUnits[spriteIndex].rowPrepared)
        return -1;

    const int startX = spriteUnits[spriteIndex].outputXStart;
    const int endX   = startX + spriteUnits[spriteIndex].outputWidth;

    const int clippedStart = std::max(startX, 0);
    const int clippedEnd   = std::min(endX, VISIBLE_WIDTH);

    for (int px = clippedStart; px < clippedEnd; ++px)
    {
        if (!spriteOpaqueLine[spriteIndex][px])
            continue;

        // Collision is based on the current rendered background opacity line.
        // This branch runs immediately after renderLine(), before the raster
        // advances, so bgOpaqueLine is the relevant current line.
        if (bgOpaqueLine[px] != 0)
            return px;
    }

    return -1;
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

    if (logger && setLogging)
    {
        const int approxCycle = rasterPixelToCycle(firstX);
        const int approxDot = firstX;

        std::ostringstream oss;
        oss << "[VIC:COLL] sprite-sprite"
            << " raster=" << raster
            << " firstX=" << firstX
            << " approxCycle=" << approxCycle
            << " approxDot=" << approxDot
            << " bits=$"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(bits)
            << " old=$" << std::setw(2)
            << static_cast<int>(old)
            << " new=$" << std::setw(2)
            << static_cast<int>(registers.spriteCollision)
            << " newlySet=$" << std::setw(2)
            << static_cast<int>(newlySet)
            << std::dec << std::nouppercase << std::setfill(' ');

        logger->WriteLog(oss.str());
    }

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

    if (logger && setLogging)
    {
        const int approxCycle = rasterPixelToCycle(firstX);
        const int approxDot = firstX;

        std::ostringstream oss;
        oss << "[VIC:COLL] sprite-background"
            << " raster=" << raster
            << " firstX=" << firstX
            << " approxCycle=" << approxCycle
            << " approxDot=" << approxDot
            << " bits=$"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(bits)
            << " old=$" << std::setw(2)
            << static_cast<int>(old)
            << " new=$" << std::setw(2)
            << static_cast<int>(registers.spriteDataCollision)
            << " newlySet=$" << std::setw(2)
            << static_cast<int>(newlySet)
            << std::dec << std::nouppercase << std::setfill(' ');

        logger->WriteLog(oss.str());
    }

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

bool Vic::isBackgroundPixelOpaque(int x, int y)
{
    if (x < 0 || x >= 512)
        return false;

    const int currentLine = fbY(registers.raster);

    if (y == currentLine)
        return bgOpaqueLine[x] != 0;

    if (y < 0 || y >= static_cast<int>(bgOpaque.size()))
        return false;

    return bgOpaque[y][x] != 0;
}

Vic::graphicsMode Vic::graphicsModeFromRegisters(uint8_t d011, uint8_t d016) const
{
    const bool MCM = (d016 & 0x10) != 0;
    const bool BMM = (d011 & 0x20) != 0;
    const bool ECM = (d011 & 0x40) != 0;

    if (!BMM && !MCM && !ECM)
        return graphicsMode::standard;

    if (!BMM && MCM && !ECM)
        return graphicsMode::multiColor;

    if (!BMM && !MCM && ECM)
        return graphicsMode::extendedColorText;

    if (BMM && !MCM && !ECM)
        return graphicsMode::bitmap;

    if (BMM && MCM && !ECM)
        return graphicsMode::multiColorBitmap;

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

void Vic::renderChar(uint8_t c, int x, int y, uint8_t fg, uint8_t bg, int yInChar, int raster, int x0, int x1)
{
    if (!mem)
        return;

    const uint16_t charBase =
        charBaseForRasterPixelX(raster, x);

    const uint16_t address =
        static_cast<uint16_t>(
            charBase +
            static_cast<uint16_t>(c) * 8
        );

    const uint8_t row =
        mem->vicRead(static_cast<uint16_t>(address + (yInChar & 0x07)), raster);

    updateOpenBus(row);

    for (int col = 0; col < 8; ++col)
    {
        const int pxRaw = x + col;
        if (pxRaw < x0 || pxRaw >= x1)
            continue;

        const bool bit = ((row >> (7 - col)) & 0x01) != 0;

        const uint8_t color = bit
            ? static_cast<uint8_t>(fg & 0x0F)
            : static_cast<uint8_t>(bg & 0x0F);

        bgColorLine[pxRaw] = static_cast<uint8_t>(color & 0x0F);

        if (bit)
            markBGOpaque(y, pxRaw);
    }
}

void Vic::renderCharMultiColor(uint8_t c, int x, int y, uint8_t cellCol, uint8_t bg, int yInChar, int raster, int x0, int x1)
{
    if (!mem)
        return;

    const uint16_t charBase =
        charBaseForRasterPixelX(raster, x);

    const uint16_t address =
        static_cast<uint16_t>(
            charBase +
            static_cast<uint16_t>(c) * 8
        );

    const uint8_t row =
        mem->vicRead(static_cast<uint16_t>(address + (yInChar & 0x07)), raster);

    updateOpenBus(row);

    const uint8_t bg0 = static_cast<uint8_t>(bg & 0x0F);
    const uint8_t bg1 = static_cast<uint8_t>(registers.backgroundColor[0] & 0x0F);
    const uint8_t bg2 = static_cast<uint8_t>(registers.backgroundColor[1] & 0x0F);
    const uint8_t colRAM = static_cast<uint8_t>(cellCol & 0x07);

    for (int pair = 0; pair < 4; ++pair)
    {
        const uint8_t bits =
            static_cast<uint8_t>((row >> ((3 - pair) * 2)) & 0x03);

        const uint8_t color =
            (bits == 0) ? bg0 :
            (bits == 1) ? bg1 :
            (bits == 2) ? bg2 :
                          colRAM;

        const int p0 = x + pair * 2;
        const int p1 = p0 + 1;

        if (p0 >= x1)
            break;

        if (p1 < x0)
            continue;

        if (p0 >= x0 && p0 < x1)
        {
            bgColorLine[p0] = static_cast<uint8_t>(color & 0x0F);
            if (bits != 0)
                markBGOpaque(y, p0);
        }

        if (p1 >= x0 && p1 < x1)
        {
            bgColorLine[p1] = static_cast<uint8_t>(color & 0x0F);
            if (bits != 0)
                markBGOpaque(y, p1);
        }
    }
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

uint8_t Vic::fetchDisplayScreenByte(int col, int raster) const
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

    const int fine =
        latchedD016ForRaster(raster) & 0x07;

    const int px =
        BACKGROUND_40COL_X0 + fine + c * 8;

    const uint16_t screenBase =
        screenBaseForRasterPixelX(raster, px);

    const uint16_t address =
        static_cast<uint16_t>(
            screenBase +
            static_cast<uint16_t>(row * BACKGROUND_MATRIX_COLUMNS + c)
        );

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

    // Prefer the active matrix row because it records whether this exact
    // display column was fetched for the current badline/display row.
    if (activeMatrixRowByteForDisplayCol(displayCol, screenByte, colorByte))
        return true;

    // Fallback to the existing FIFO path to preserve current behavior when
    // activeMatrixRow is not populated but the badline FIFO is valid.
    screenByte = charPtrFIFO[displayCol];
    colorByte  = static_cast<uint8_t>(colorPtrFIFO[displayCol] & 0x0F);

    return true;
}

uint8_t Vic::resolveDisplayScreenByte(int displayCol, int raster) const
{
    uint8_t screenByte = 0;
    uint8_t colorByte = 0;

    if (fetchedMatrixBytesForDisplayCol(displayCol, raster, screenByte, colorByte))
        return screenByte;

    return fetchDisplayScreenByte(displayCol, raster);
}

uint8_t Vic::resolveDisplayColorByte(int displayCol, int raster) const
{
    uint8_t screenByte = 0;
    uint8_t colorByte = 0;

    if (fetchedMatrixBytesForDisplayCol(displayCol, raster, screenByte, colorByte))
        return static_cast<uint8_t>(colorByte & 0x0F);

    return static_cast<uint8_t>(fetchDisplayColorByte(displayCol, raster) & 0x0F);
}

void Vic::advanceVideoCountersEndOfLine(int raster)
{
    advanceCharacterSequencerEndOfLine(raster);
}

void Vic::advanceCharacterSequencerEndOfLine(int raster)
{
    if (!vicState.displayEnabledNext)
    {
        vicState.displayEnabled = false;
        return;
    }

    vicState.displayEnabled = true;

    const int visibleRows = getLatchedRSEL(raster) ? 25 : 24;
    const int currentRowBefore = currentCharacterRow();

    if (currentRowBefore < 0 || currentRowBefore >= visibleRows)
    {
        vicState.displayEnabled = false;
        vicState.displayEnabledNext = false;
        clearBadLineFifo();
        return;
    }

    vicState.rc = static_cast<uint8_t>((vicState.rc + 1) & 0x07);

    if (vicState.rc == 0)
    {
        const uint16_t nextVcBase = static_cast<uint16_t>(vicState.vcBase + 40);
        const int nextRow = nextVcBase / 40;

        if (nextRow >= visibleRows)
        {
            vicState.displayEnabled = false;
            vicState.displayEnabledNext = false;
            clearBadLineFifo();
            return;
        }

        vicState.vcBase = nextVcBase;
        vicState.vmliBase = nextVcBase;
    }

    const bool den = (latchedD011ForRaster(raster) & 0x10) != 0;

    if (!denSeenOn30 || firstBadlineY < 0 || !den)
    {
        vicState.displayEnabled = false;
        vicState.displayEnabledNext = false;
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

bool Vic::verticalDisplayOpenForRaster(int raster) const
{
    const BorderWindow w = borderWindowForRaster(raster);
    return !w.vertical;
}

bool Vic::horizontalBorderLatchedAtPixel(int raster, int px) const
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return true;

    if (px < 0 || px >= VISIBLE_WIDTH)
        return true;

    if (borderVertical_per_raster[raster] != 0)
        return true;

    return borderMaskLine[px] != 0;
}

void Vic::updateVerticalBorderState(int raster)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
    {
        vicState.verticalBorder = true;
        return;
    }

    const uint8_t d011 = latchedD011ForRaster(raster);
    const bool den = (d011 & 0x10) != 0;

    if (!den || !denSeenOn30)
    {
        vicState.verticalBorder = true;
        return;
    }

    const VerticalBorderWindow w =
        verticalBorderWindowForRaster(raster);

    if (raster == w.topOpen)
    {
        vicState.verticalBorder = false;

        if (vicState.topBorderOpenRaster < 0)
            vicState.topBorderOpenRaster = raster;
    }

    const int closeRaster = w.bottomClose + 1;

    if (raster == closeRaster)
    {
        vicState.verticalBorder = true;
        vicState.bottomBorderCloseRaster = raster;
    }

    if (raster < w.topOpen || raster > closeRaster)
    {
        vicState.verticalBorder = true;
    }
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

    if (borderVertical_per_raster[raster] != 0)
        return true;

    return borderMaskLine[px] != 0;
}

uint8_t Vic::latchOpenBus(uint8_t value)
{
    vicState.openBus = value;
    return value;
}

uint8_t Vic::getOpenBus() const
{
    return vicState.openBus;
}

uint8_t Vic::latchOpenBusMasked(uint8_t definedBits, uint8_t definedMask)
{
    const uint8_t value =
        (getOpenBus() & static_cast<uint8_t>(~definedMask)) |
        (definedBits & definedMask);

    vicState.openBus = value;
    return value;
}

void Vic::markBGOpaque(int screenY, int px)
{
    (void)screenY;

    if (px >= 0 && px < 512)
    {
        bgOpaqueLine[px] = 1;
    }
}

void Vic::latchNextRasterDD00()
{
    const int raster = registers.raster;
    const uint16_t nextRaster = (raster + 1) % cfg_->maxRasterLines;

    dd00_per_raster[nextRaster] = cia2object ? cia2object->getCurrentVICBank() : 0;
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

std::string Vic::dumpRegisters(const std::string& group) const
{

    // Color lookup for colors section
    static const char* C64ColorNames[16] =
    {
        "Black","White","Red","Cyan","Purple","Green","Blue","Yellow",
        "Orange","Brown","Light Red","Dark Grey","Grey","Light Green","Light Blue","Light Grey"
    };

    uint16_t currentRaster = registers.raster;

    std::stringstream out;
    out << std::hex << std::uppercase << std::setfill('0');

    // Dump the regs

    // Raster and Control
    if (group == "all" || group == "raster")
    {
        out << "Raster and Control Registers:\n\n";

        uint8_t rasterHi = (registers.raster >> 8) & 0x01;
        out << "D011 = $" << std::setw(2) << std::hex << ( (registers.control & 0x7F) | (rasterHi << 7) )
            << "   (CTRL1: YSCROLL = " << (registers.control & 0x07)
            << ", 25row = " << ((registers.control & 0x08) ? "Yes" : "No")
            << ", Screen = " << ((registers.control & 0x10) ? "On" : "Off")
            << ", Bitmap = " << ((registers.control & 0x20) ? "Yes" : "No")
            << ", ECM = " << ((registers.control & 0x40) ? "Yes" : "No")
            << ", RasterHi = " << int(rasterHi) << ")\n";

        out << "D012 = $" << std::setw(2) << std::hex << (registers.raster & 0xFF) << "   (RASTER = " << std::dec << currentRaster << ")\n";

        out << "D016 = $" << std::setw(2) << std::hex << static_cast<int>(registers.control2) << "   (CTRL2: XSCROLL = " << (registers.control2 & 0x07)
            << ", 40COL = " << ((registers.control2 & 0x08) ? "Yes" : "No") << ", Multicolor = " << ((registers.control2 & 0x10) ? "Yes" : "No")
            << ",  HIRES = " << ((registers.control2 & 0x10) ? "No" : "Yes") << ")\n";

        out << "D018 = $" << std::setw(2) << static_cast<int>(registers.memory_pointer) << "   (MEM_PTR:   Screen = $" << std::setw(4)
            << screenBaseCache << ", CHAR = $" << std::setw(4) << charBaseCache << ", Bitmap = $"
            << std::setw(4) << bitmapBaseCache << ")\n";
    }

    // Interrupts
    if (group == "all" || group == "irq")
    {
        const uint8_t d019Status = d019Read();
        const uint8_t pending   = registers.interruptStatus & 0x0F;
        const uint8_t enabled   = registers.interruptEnable & 0x0F;
        const bool irqLine      = (pending & enabled) != 0;

        out << "\nInterrupt Registers:\n\n";

        out << "D019 = $" << std::setw(2) << int(d019Status)
            << "   (Pending: Raster=" << ((pending & 0x01) ? "Y" : "N")
            << ", SprSpr=" << ((pending & 0x02) ? "Y" : "N")
            << ", SprBg="  << ((pending & 0x04) ? "Y" : "N")
            << ", LightPen=" << ((pending & 0x08) ? "Y" : "N")
            << ", IRQ line=" << (irqLine ? "Low" : "High") << ")\n";

        out << "D01A = $" << std::setw(2) << int(registers.interruptEnable)
            << "   (Enable: Raster=" << ((enabled & 0x01) ? "Y" : "N")
            << ", SprSpr=" << ((enabled & 0x02) ? "Y" : "N")
            << ", SprBg="  << ((enabled & 0x04) ? "Y" : "N")
            << ", LightPen=" << ((enabled & 0x08) ? "Y" : "N") << ")\n";

        const uint16_t visibleRaster = visibleRasterForRead();
        const uint16_t compareRaster = visibleRasterForIRQCompare();
        const uint16_t irqTarget = registers.rasterInterruptLine;
        const bool irqTargetValid = rasterIRQTargetInRange();

        out << "\nRaster IRQ Timing:\n";

        out << "Raster now      = " << std::dec << registers.raster
            << "   Cycle=" << currentCycle
            << "   Dot=" << getRasterDot() << "\n";

        out << "Visible raster  = " << visibleRaster
            << "   D012 read=$"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << int(visibleRaster & 0xFF)
            << std::dec << std::nouppercase << std::setfill(' ') << "\n";

        out << "Compare raster  = " << compareRaster
            << "   targetInRange=" << (rasterIRQTargetInRange() ? 1 : 0)
            << "\n";

        out << "IRQ target      = ";

        if (irqTargetValid)
        {
            out << std::dec << irqTarget
                << "   D011 IRQ bit8=" << ((irqTarget & 0x0100) ? 1 : 0)
                << "   D012 IRQ low=$"
                << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                << int(irqTarget & 0xFF)
                << std::dec << std::nouppercase << std::setfill(' ') << "\n";
        }
        else
        {
            out << "disabled/unset"
                << "   raw=" << irqTarget << "\n";
        }

        out << "Compare cycle   = " << rasterIRQCompareCycle()
            << "   Sampled this line="
            << (rasterIrqSampledThisLine ? "Yes" : "No") << "\n";

        out << "Compare match   = "
            << (rasterCompareMatchesNow() ? "Yes" : "No")
            << "   targetInRange=" << (rasterIRQTargetInRange() ? 1 : 0)
            << "\n";

        const bool compareWillSampleNext =
            !rasterIrqSampledThisLine &&
            currentCycle < RASTER_IRQ_COMPARE_CYCLE &&
            rasterCompareMatchesNow();

        out << "Compare status  = ";

        if (rasterIrqSampledThisLine)
        {
            out << "already sampled this line\n";
        }
        else if (currentCycle < rasterIRQCompareCycle())
        {
            out << (compareWillSampleNext ? "will match at compare cycle" : "waiting for compare cycle")
                << "\n";
        }
        else if (currentCycle == rasterIRQCompareCycle())
        {
            out << (rasterCompareMatchesNow() ? "sampling match now" : "sampling no-match now")
                << "\n";
        }
        else
        {
            out << "compare point already passed\n";
        }

        out << "Raw IFR         = $"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << int(registers.interruptStatus)
            << "   Raw IER=$"
            << std::setw(2)
            << int(registers.interruptEnable)
            << std::dec << std::nouppercase << std::setfill(' ') << "\n";

        out << "Last Raster IRQ Sample:\n";

        if (lastRasterIRQSample.valid)
        {
            out << "  reason=" << lastRasterIRQSample.reason
                << " raster=" << lastRasterIRQSample.raster
                << " cycle=" << lastRasterIRQSample.cycle
                << " visibleRaster=" << lastRasterIRQSample.visibleRaster
                << " target=" << lastRasterIRQSample.targetRaster
                << " targetInRange=" << (lastRasterIRQSample.targetInRange ? 1 : 0)
                << " matched=" << (lastRasterIRQSample.matched ? 1 : 0)
                << " sampledBefore=" << (lastRasterIRQSample.sampledBefore ? 1 : 0)
                << "\n";
        }
        else
        {
            out << "  none\n";
        }
    }

    // Sprite control
    if (group == "all" || group == "sprites")
    {
        out << "\nSprite Control Registers:\n\n";

        out << "D015 = $" << std::setw(2) << static_cast<int>(registers.spriteEnabled) << " (Enable Mask: " << std::bitset<8> (registers.spriteEnabled)
            << ")\n";

        out << "D017 = $" << std::setw(2) << static_cast<int>(registers.spriteYExpansion) << " (Y-Expand: " << std::bitset<8>(registers.spriteYExpansion)
            << ")\n";

        out << "D01B = $" << std::setw(2) << static_cast<int>(registers.spritePriority) << " (Sprite Priority: " << std::bitset<8>(registers.spritePriority)
            << ")\n";

        out << "D01C = $" << std::setw(2) << static_cast<int>(registers.spriteMultiColor) << " (Sprite Multicolor: "
            << std::bitset<8>(registers.spriteMultiColor) << ")\n";

        out << "D01D = $" << std::setw(2) << static_cast<int>(registers.spriteXExpansion) << " (Sprite X Expansion: "
            << std::bitset<8>(registers.spriteXExpansion) << ")\n";
    }

    // Sprite collision
    if (group == "all" || group == "collisions")
    {
        out << "\nSprite Collision Registers\n\n";

        out << "D01E $" << std::setw(2) << static_cast<int>(registers.spriteCollision) << " (Sprite to Sprite Collision: "
            << std::bitset<8>(registers.spriteCollision) << ")\n";

        out << "D01F $" << std::setw(2) << static_cast<int>(registers.spriteDataCollision) << " (Sprite to Background Collision " <<
            std::bitset<8>(registers.spriteDataCollision) << ")\n";
    }

    // Latched registers
    if (group == "all" || group == "latch")
    {
        const int raster = static_cast<int>(registers.raster);
        const int nextRaster = (raster + 1) % static_cast<int>(cfg_->maxRasterLines);

        const uint8_t liveD011 = registers.control & 0x7F;
        const uint8_t liveD016 = registers.control2 & 0x1F;
        const uint8_t liveD018 = registers.memory_pointer & 0xFE;

        const uint8_t curD011 = latchedD011ForRaster(raster);
        const uint8_t curD016 = latchedD016ForRaster(raster);
        const uint8_t curD018 = latchedD018ForRaster(raster);

        const uint8_t nextD011 = latchedD011ForRaster(nextRaster);
        const uint8_t nextD016 = latchedD016ForRaster(nextRaster);
        const uint8_t nextD018 = latchedD018ForRaster(nextRaster);

        auto yn = [](bool v) { return v ? "Y" : "N"; };

        out << "\nVIC Register Latches:\n\n";

        out << "Raster=" << std::dec << raster
            << "   Cycle=" << currentCycle
            << "   NextRaster=" << nextRaster << "\n\n";

        out << "Live:      "
            << "D011=$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(liveD011)
            << " D016=$" << std::setw(2) << int(liveD016)
            << " D018=$" << std::setw(2) << int(liveD018)
            << std::dec << std::nouppercase << std::setfill(' ') << "\n";

        out << "Latched:   "
            << "D011=$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(curD011)
            << " D016=$" << std::setw(2) << int(curD016)
            << " D018=$" << std::setw(2) << int(curD018)
            << std::dec << std::nouppercase << std::setfill(' ')
            << "   for raster " << raster << "\n";

        out << "NextLatch: "
            << "D011=$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(nextD011)
            << " D016=$" << std::setw(2) << int(nextD016)
            << " D018=$" << std::setw(2) << int(nextD018)
            << std::dec << std::nouppercase << std::setfill(' ')
            << "   for raster " << nextRaster << "\n\n";

        out << "Live flags:      "
            << "DEN=" << yn((liveD011 & 0x10) != 0)
            << " RSEL=" << yn((liveD011 & 0x08) != 0)
            << " CSEL=" << yn((liveD016 & 0x08) != 0)
            << " YSCROLL=" << int(liveD011 & 0x07)
            << " XSCROLL=" << int(liveD016 & 0x07)
            << "\n";

        out << "Latched flags:   "
            << "DEN=" << yn((curD011 & 0x10) != 0)
            << " RSEL=" << yn((curD011 & 0x08) != 0)
            << " CSEL=" << yn((curD016 & 0x08) != 0)
            << " YSCROLL=" << int(curD011 & 0x07)
            << " XSCROLL=" << int(curD016 & 0x07)
            << "\n";

        out << "Next flags:      "
            << "DEN=" << yn((nextD011 & 0x10) != 0)
            << " RSEL=" << yn((nextD011 & 0x08) != 0)
            << " CSEL=" << yn((nextD016 & 0x08) != 0)
            << " YSCROLL=" << int(nextD011 & 0x07)
            << " XSCROLL=" << int(nextD016 & 0x07)
            << "\n\n";

        out << "Live border:     "
            << "vertical=" << (vicState.verticalBorder ? "on" : "off")
            << " left=" << (vicState.leftBorder ? "on" : "off")
            << " right=" << (vicState.rightBorder ? "on" : "off")
            << " openX=" << vicState.leftBorderOpenX
            << " closeX=" << vicState.rightBorderCloseX
            << "\n";

        out << "Latched border:  "
            << "vertical=" << ((borderVertical_per_raster[raster] != 0) ? "on" : "off")
            << " openX=" << borderLeftOpenX_per_raster[raster]
            << " closeX=" << borderRightCloseX_per_raster[raster]
            << "\n";

        out << "Next border:     "
            << "vertical=" << ((borderVertical_per_raster[nextRaster] != 0) ? "on" : "off")
            << " openX=" << borderLeftOpenX_per_raster[nextRaster]
            << " closeX=" << borderRightCloseX_per_raster[nextRaster]
            << "\n";
    }

    // Color registers
    if (group == "all" || group == "colors")
    {
        out << "\nColor Registers:\n\n";

        auto printColor = [&](const char* label, uint8_t value)
        {
            int idx = value & 0x0F; // only low 4 bits used
            out << label << " = $" << std::setw(2) << static_cast<int>(value)
                << "   (" << C64ColorNames[idx] << ")\n";
        };

        printColor("D020 (Border Color)", registers.borderColor);
        printColor("D021 (Background 0)", registers.backgroundColor0);
        printColor("D022 (Background 1)", registers.backgroundColor[0]);
        printColor("D023 (Background 2)", registers.backgroundColor[1]);
        printColor("D024 (Background 3)", registers.backgroundColor[2]);
        printColor("D025 (Sprite Multicolor 0)", registers.spriteMultiColor1);
        printColor("D026 (Sprite Multicolor 1)", registers.spriteMultiColor2);

        // Per-sprite colors
        for (int i = 0; i < 8; i++)
        {
            std::ostringstream label;
            label << "D0" << std::hex << std::uppercase << (27+i)
                  << " (Sprite " << i << " Color)";
            printColor(label.str().c_str(), registers.spriteColors[i]);
        }
    }

    // Sprite position
    if (group == "all" || group == "pos")
    {
        out << "\nSprite Position Registers:\n\n";

        for (int i = 0; i < 8; i++)
        {
            // X coordinate is 9 bits: low 8 from spriteX[i], high bit from spriteX_MSB
            int x = registers.spriteX[i] | ((registers.spriteX_MSB >> i) & 0x01) << 8;
            int y = registers.spriteY[i];

            out << "Sprite " << i
                << " -> X=$" << std::setw(3) << x
                << " (" << std::dec << x << "), "
                << "Y=$" << std::setw(3) << y
                << " (" << std::dec << y << ")\n";
        }
    }

    return out.str();
}

void Vic::rebuildBorderRasterLatches()
{
    if ((int)borderVertical_per_raster.size() != cfg_->maxRasterLines)
        borderVertical_per_raster.assign(cfg_->maxRasterLines, 1);
    if ((int)borderLeftOpenX_per_raster.size() != cfg_->maxRasterLines)
        borderLeftOpenX_per_raster.assign(cfg_->maxRasterLines, 0);
    if ((int)borderRightCloseX_per_raster.size() != cfg_->maxRasterLines)
        borderRightCloseX_per_raster.assign(cfg_->maxRasterLines, VISIBLE_WIDTH);

    for (int r = 0; r < cfg_->maxRasterLines; ++r)
    {
        updateVerticalBorderState(r);
        updateHorizontalBorderState(r);

        borderVertical_per_raster[r] = vicState.verticalBorder ? 1 : 0;
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

uint8_t Vic::d016ForRasterPixelX(int raster, int px, bool preferPreviousFrame) const
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return registers.control2 & 0x1F;

    uint8_t active = latchedD016ForRaster(raster) & 0x1F;

    const std::vector<RasterEventRecord>& events =
        preferPreviousFrame ? lastFrameRasterEventLog : rasterEventLog;

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

    // Rendering must only use the current frame's events.
    // Previous-frame events are useful for monitor diagnostics, but they
    // should never leak into current-frame rendering.
    const std::vector<RasterEventRecord>& events =
        preferPreviousFrame ? lastFrameRasterEventLog : rasterEventLog;

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

    bool badLineForThisRaster = (raster == registers.raster) ? vicState.badLineSampled : isBadLine(raster);

    if (badLineForThisRaster &&
        cycle >= cfg_->DMAStartCycle &&
        cycle <= cfg_->DMAEndCycle)
    {
        return FetchKind::CharMatrix;
    }

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

std::string Vic::dumpCycleDebugFor(int raster, int cycle) const
{
    std::ostringstream out;

    if (raster < 0 || raster >= cfg_->maxRasterLines)
    {
        out << "Invalid raster: " << raster << "\n";
        return out.str();
    }

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
    {
        out << "Invalid cycle: " << cycle << "\n";
        return out.str();
    }

    const bool badLine = (raster == registers.raster) ? vicState.badLineSampled : isBadLine(raster);
    const FetchKind fk = getFetchKindForCycle(raster, cycle);
    const VicCycleSlot slot = cycleSlotFor(raster, cycle);

    const bool ptrMismatch =
        fetchKindIsSpritePointer(slot.fetchKind) &&
        slot.busOwner != BusOwner::SpritePointer;

    const bool dataMismatch =
        fetchKindIsSpriteData(slot.fetchKind) &&
        slot.busOwner != BusOwner::SpriteData;

    out << "VIC Cycle Debug\n\n";
    out << "Raster: " << raster << "\n";
    out << "Cycle : " << cycle << "\n";
    out << "Fetch : " << fetchKindName(fk) << "\n";
    out << "Badline: " << (badLine ? "Yes" : "No") << "\n";
    out << "VCBASE: " << vicState.vcBase << "\n";
    out << "RC    : " << int(vicState.rc) << "\n";
    out << "BA    : " << (slot.baLow ? "Low" : "High") << "\n";
    out << "AEC   : " << (slot.aecLow ? "Low" : "High") << "\n";
    out << "BA src: "
        << (slot.badlineWarning ? "badline-warning " : "")
        << (slot.badlineSteal   ? "badline-steal "   : "")
        << (slot.spriteWarning  ? "sprite-warning "  : "")
        << (slot.spriteSteal    ? "sprite-steal "    : "")
        << ((!slot.badlineWarning && !slot.badlineSteal &&
             !slot.spriteWarning && !slot.spriteSteal) ? "none" : "")
        << "\n";
    out << "AECsrc: "
        << (slot.badlineSteal   ? "badline-steal " : "")
        << (slot.spriteAECSteal ? "sprite-steal "  : "")
        << ((!slot.badlineSteal && !slot.spriteAECSteal) ? "none" : "")
        << "\n";
    out << "DEN@raster: " << (((d011_per_raster[raster] & 0x10) != 0) ? "On" : "Off") << "\n";
    out << "DisplayRow: " << currentCharacterRow() << "\n";
    out << "FineY: " << int(fineYScroll(raster)) << "\n";
    out << "FineX: " << int(fineXScroll(raster)) << "\n";
    out << "Owner: " << busOwnerName(slot.busOwner) << "\n";

    if (ptrMismatch)
        out << "WARNING: sprite pointer fetch does not have SpritePointer bus owner\n";

    if (dataMismatch)
        out << "WARNING: sprite data fetch does not have SpriteData bus owner\n";

    out << "\nSprite DMA active:";
    bool any = false;
    for (int i = 0; i < 8; ++i)
    {
        if (spriteUnits[i].dmaActive)
        {
            out << " " << i;
            any = true;
        }
    }
    if (!any)
        out << " none";

    out << "\nSprite display active:";
    any = false;
    for (int i = 0; i < 8; ++i)
    {
        if (spriteUnits[i].displayActive)
        {
            out << " " << i;
            any = true;
        }
    }
    if (!any)
        out << " none";

    out << "\n";

    return out.str();
}

std::string Vic::dumpCurrentCycleDebug() const
{
    const int raster = registers.raster;
    const int cycle  = currentCycle;

    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return "Invalid current raster\n";

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return "Invalid current cycle\n";

    return dumpCycleDebugFor(raster, cycle);
}

std::string Vic::dumpRasterFetchMap(int raster) const
{
    std::ostringstream out;

    if (raster < 0 || raster >= cfg_->maxRasterLines)
    {
        out << "Invalid raster: " << raster << "\n";
        return out.str();
    }

    out << "VIC Raster Fetch Map\n\n";
    out << "Raster: " << raster << "\n";
    const bool badLine = (raster == registers.raster) ? vicState.badLineSampled : isBadLine(raster);

    out << "Badline: " << (badLine ? "Yes" : "No") << "\n\n";

    for (int c = 0; c < cfg_->cyclesPerLine; ++c)
    {
        out << std::setw(2) << c << ": "
            << fetchKindName(getFetchKindForCycle(raster, c))
            << "\n";
    }

    return out.str();
}

std::string Vic::dumpAllRasterEvents() const
{
    std::ostringstream out;

    const std::vector<RasterEventRecord>* events = &lastFrameRasterEventLog;
    const char* sourceName = "previous frame";

    if (events->empty() && !rasterEventLog.empty())
    {
        events = &rasterEventLog;
        sourceName = "current frame";
    }

    out << "All Raster Events (" << sourceName << ")\n";
    out << "-------------------------------------\n";
    out << "Total events: " << events->size() << "\n";
    out << "  raster  type                    cycle  x     addr   old  new  detail\n";

    if (events->empty())
    {
        out << "No recorded events.\n";
        return out.str();
    }

    for (const RasterEventRecord& e : *events)
    {
        out << "  "
            << std::dec << std::setw(6) << e.raster
            << "  "
            << std::left << std::setw(22) << rasterEventKindName(e.kind)
            << std::right
            << std::setw(5) << e.cycle
            << "  "
            << std::setw(4) << rasterEventPixelX(e.cycle)
            << "  $"
            << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << e.address
            << "  $"
            << std::setw(2) << static_cast<int>(e.oldValue)
            << "   $"
            << std::setw(2) << static_cast<int>(e.newValue)
            << std::dec << std::nouppercase << std::setfill(' ');

        const std::string detail = rasterEventDetail(e);
        if (!detail.empty())
            out << "  " << detail;

        out << "\n";
    }

    return out.str();
}

std::string Vic::dumpRasterEventSummary() const
{
    std::ostringstream out;

    auto summarize = [&](const char* name, const std::vector<RasterEventRecord>& events)
    {
        out << name << ": " << events.size() << " events\n";

        if (events.empty())
            return;

        int minRaster = static_cast<int>(cfg_->maxRasterLines);
        int maxRaster = -1;

        int color = 0;
        int control = 0;
        int control2 = 0;
        int memoryPointer = 0;
        int priority = 0;
        int mode = 0;
        int xexp = 0;
        int enable = 0;
        int spriteX = 0;

        for (const RasterEventRecord& e : events)
        {
            minRaster = std::min(minRaster, e.raster);
            maxRaster = std::max(maxRaster, e.raster);

            switch (e.kind)
            {
                case RasterEventKind::Color:
                    ++color;
                    break;

                case RasterEventKind::Control:
                    ++control;
                    break;

                case RasterEventKind::Control2:
                    ++control2;
                    break;

                case RasterEventKind::MemoryPointer:
                    ++memoryPointer;
                    break;

                case RasterEventKind::SpritePriority:
                    ++priority;
                    break;

                case RasterEventKind::SpriteMode:
                    ++mode;
                    break;

                case RasterEventKind::SpriteXExpansion:
                    ++xexp;
                    break;

                case RasterEventKind::SpriteEnable:
                    ++enable;
                    break;

                case RasterEventKind::SpriteX:
                    ++spriteX;
                    break;
            }
        }

        out << "  raster range: " << minRaster << " - " << maxRaster << "\n";
        out << "  Color: " << color << "\n";
        out << "  Control $D011: " << control << "\n";
        out << "  Control2 $D016: " << control2 << "\n";
        out << "  Memory ptr $D018: " << memoryPointer << "\n";
        out << "  Sprite priority: " << priority << "\n";
        out << "  Sprite mode: " << mode << "\n";
        out << "  Sprite X expansion: " << xexp << "\n";
        out << "  Sprite enable: " << enable << "\n";
        out << "  Sprite X position: " << spriteX << "\n";
    };

    out << "Raster Event Summary\n";
    out << "--------------------\n";
    summarize("Current frame", rasterEventLog);
    summarize("Previous frame", lastFrameRasterEventLog);

    return out.str();
}

std::string Vic::dumpRasterEvents(int raster) const
{
    std::ostringstream out;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
    {
        out << "Raster " << raster << " is out of range\n";
        return out.str();
    }

    const std::vector<RasterEventRecord>* events = &lastFrameRasterEventLog;
    const char* sourceName = "previous frame";
    bool usingPreviousFrame = true;

    // Prefer the previous completed frame for stable diagnostics.
    // Fall back to current frame only if no previous-frame log exists yet.
    if (events->empty())
    {
        events = &rasterEventLog;
        sourceName = "current frame";
        usingPreviousFrame = false;
    }

    out << "Raster Events for line " << raster
        << " (" << sourceName << ")\n";
    out << "--------------------------------\n";
    out << "Total events in " << sourceName << ": " << events->size() << "\n";
    out << "  type                    cycle  x     addr   old  new  detail\n";

    bool any = false;

    for (const RasterEventRecord& e : *events)
    {
        if (e.raster != raster)
            continue;

        any = true;

        out << "  "
            << std::left << std::setw(22) << rasterEventKindName(e.kind)
            << std::right
            << std::dec << std::setw(5) << e.cycle
            << "  "
            << std::setw(4) << rasterEventPixelX(e.cycle)
            << "  $"
            << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << e.address
            << "  $"
            << std::setw(2) << static_cast<int>(e.oldValue)
            << "   $"
            << std::setw(2) << static_cast<int>(e.newValue)
            << std::dec << std::nouppercase << std::setfill(' ');

        const std::string detail = rasterEventDetail(e);
        if (!detail.empty())
            out << "  " << detail;

        const std::string rowDetail =
            rasterRowStateDetail(e.raster, usingPreviousFrame);

        if (!rowDetail.empty())
            out << "  " << rowDetail;

        out << "\n";
    }

    if (!any)
        out << "No recorded events on this raster.\n";

    return out.str();
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

std::string Vic::dumpRasterRowState(int raster) const
{
    std::ostringstream out;

    out << "Raster Row State for line " << raster << " (previous frame)\n";
    out << "----------------------------------------\n";

    const std::string detail = rasterRowStateDetail(raster, true);

    if (detail.empty())
        out << "No row-state snapshot available.\n";
    else
        out << detail << "\n";

    return out.str();
}

std::string Vic::dumpBackgroundRowDebug(int raster) const
{
    std::ostringstream out;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
    {
        out << "Raster " << raster << " is out of range\n";
        return out.str();
    }

    const RasterRowStateSnapshot* snap = nullptr;
    const char* snapSource = "none";
    bool usingPreviousFrame = false;

    if (raster < static_cast<int>(lastFrameRasterRowStates.size()) &&
        lastFrameRasterRowStates[raster].valid)
    {
        snap = &lastFrameRasterRowStates[raster];
        snapSource = "previous frame";
        usingPreviousFrame = true;
    }
    else if (raster < static_cast<int>(rasterRowStates.size()) &&
             rasterRowStates[raster].valid)
    {
        snap = &rasterRowStates[raster];
        snapSource = "current frame";
        usingPreviousFrame = false;
    }

    if (!snap)
    {
        out << "No row-state snapshot available for raster " << raster << "\n";
        return out.str();
    }

    const uint8_t d011 = snap->d011 & 0x7F;
    const uint8_t d016 = snap->d016 & 0x1F;
    const uint8_t rowD018 = snap->d018 & 0xFE;

    const uint16_t screenBase =
        static_cast<uint16_t>((rowD018 & 0xF0) << 6);

    const uint16_t rowCharBase =
        static_cast<uint16_t>(((rowD018 >> 1) & 0x07) * 0x0800);

    const uint16_t bitmapBase =
        static_cast<uint16_t>(((rowD018 >> 3) & 0x01) * 0x2000);

    const int fineY = d011 & 0x07;
    const int fineX = d016 & 0x07;
    const int matrixRow = snap->vcBase / 40;
    const int vmliRow = snap->vmliBase / 40;
    const uint8_t yInChar = static_cast<uint8_t>(snap->rc & 0x07);

    out << "Background Row Debug\n";
    out << "--------------------\n";
    out << "snapshot: " << snapSource << "\n";
    out << "raster: " << raster << "\n";
    out << "mode: " << decodeModeName() << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "D011: $" << std::setw(2) << static_cast<int>(d011)
        << std::dec
        << " fineY=" << fineY
        << " RSEL=" << (((d011 & 0x08) != 0) ? 25 : 24)
        << " DEN=" << (((d011 & 0x10) != 0) ? 1 : 0)
        << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "D016: $" << std::setw(2) << static_cast<int>(d016)
        << std::dec
        << " fineX=" << fineX
        << " CSEL=" << (((d016 & 0x08) != 0) ? 40 : 38)
        << " MCM=" << (((d016 & 0x10) != 0) ? 1 : 0)
        << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "D018 row-latched: $" << std::setw(2) << static_cast<int>(rowD018) << "\n";
    out << "screenBase: $" << std::setw(4) << screenBase << "\n";
    out << "rowCharBase: $" << std::setw(4) << rowCharBase << "\n";
    out << "bitmapBase:  $" << std::setw(4) << bitmapBase << "\n";

    out << std::dec << std::setfill(' ');

    out << "firstBadlineY: " << snap->firstBadlineY << "\n";
    out << "vcBase: " << snap->vcBase << "\n";
    out << "matrixRow: " << matrixRow << "\n";
    out << "vmliBase: " << snap->vmliBase << "\n";
    out << "vmliRow: " << vmliRow << "\n";
    out << "vmliFetchIndex: " << static_cast<int>(snap->vmliFetchIndex) << "\n";
    out << "rc/yInChar: " << static_cast<int>(yInChar) << "\n";
    out << "badLine: " << (snap->badLine ? 1 : 0) << "\n";
    out << "badLineSampled: " << (snap->badLineSampled ? 1 : 0) << "\n";

    out << "\n";
    out << "  col  x    scrAddr scr color d018 charBase charAddr bits\n";
    out << "  --------------------------------------------------------\n";

    for (int col = 0; col < BACKGROUND_MATRIX_COLUMNS; ++col)
    {
        const int matrixOffset = snap->vcBase + col;

        const uint16_t screenAddr =
            static_cast<uint16_t>(screenBase + (matrixOffset & 0x03FF));

        const uint8_t screenByte =
            mem ? mem->vicRead(screenAddr, raster) : 0xFF;

        const uint16_t colorAddr =
            static_cast<uint16_t>(COLOR_MEMORY_START + (matrixOffset & 0x03FF));

        const uint8_t colorByte =
            mem ? static_cast<uint8_t>(mem->read(colorAddr) & 0x0F) : 0x0F;

        const int colX = BACKGROUND_40COL_X0 + fineX + (col * 8);

        const uint8_t colD018 =
            d018ForRasterPixelX(raster, colX, usingPreviousFrame) & 0xFE;

        const uint16_t colCharBase =
            static_cast<uint16_t>(((colD018 >> 1) & 0x07) * 0x0800);

        const uint16_t charAddr =
            static_cast<uint16_t>(
                colCharBase +
                (static_cast<uint16_t>(screenByte) * 8) +
                yInChar
            );

        const uint8_t rowBits =
            mem ? mem->vicRead(charAddr, raster) : 0x00;

        out << "  "
            << std::dec << std::setw(3) << col
            << "  "
            << std::setw(3) << colX
            << "  $"
            << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << screenAddr
            << "   $"
            << std::setw(2) << static_cast<int>(screenByte)
            << "  $"
            << std::setw(2) << static_cast<int>(colorByte)
            << "   $"
            << std::setw(2) << static_cast<int>(colD018)
            << "   $"
            << std::setw(4) << colCharBase
            << "   $"
            << std::setw(4) << charAddr
            << "    $"
            << std::setw(2) << static_cast<int>(rowBits);

        if (rowBits != 0)
            out << "  *";

        out << std::dec << std::nouppercase << std::setfill(' ')
            << "\n";
    }

    return out.str();
}

std::string Vic::dumpBackgroundCellDebug(int raster, int col) const
{
    std::ostringstream out;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
    {
        out << "Raster " << raster << " is out of range\n";
        return out.str();
    }

    if (col < 0 || col >= BACKGROUND_MATRIX_COLUMNS)
    {
        out << "Column " << col << " is out of range\n";
        return out.str();
    }

    const RasterRowStateSnapshot* snap = nullptr;
    const char* snapSource = "none";

    if (raster < static_cast<int>(lastFrameRasterRowStates.size()) &&
        lastFrameRasterRowStates[raster].valid)
    {
        snap = &lastFrameRasterRowStates[raster];
        snapSource = "previous frame";
    }
    else if (raster < static_cast<int>(rasterRowStates.size()) &&
             rasterRowStates[raster].valid)
    {
        snap = &rasterRowStates[raster];
        snapSource = "current frame";
    }

    if (!snap)
    {
        out << "No row-state snapshot available for raster " << raster << "\n";
        return out.str();
    }

    const uint8_t d011 = snap->d011 & 0x7F;
    const uint8_t d016 = snap->d016 & 0x1F;
    const uint8_t d018 = snap->d018 & 0xFE;

    const uint16_t screenBase = static_cast<uint16_t>((d018 & 0xF0) << 6);
    const uint16_t charBase   = static_cast<uint16_t>(((d018 >> 1) & 0x07) * 0x0800);
    const uint16_t bitmapBase = static_cast<uint16_t>(((d018 >> 3) & 0x01) * 0x2000);

    const int fineY = d011 & 0x07;
    const int fineX = d016 & 0x07;

    const int matrixRow = snap->vcBase / 40;
    const int vmliRow = snap->vmliBase / 40;
    const int matrixOffset = snap->vcBase + col;

    const uint16_t screenAddr =
        static_cast<uint16_t>(screenBase + (matrixOffset & 0x03FF));

    const uint8_t screenByte =
        mem ? mem->vicRead(screenAddr, raster) : 0xFF;

    const uint16_t colorAddr =
        static_cast<uint16_t>(COLOR_MEMORY_START + (matrixOffset & 0x03FF));

    const uint8_t colorByte =
        mem ? static_cast<uint8_t>(mem->read(colorAddr) & 0x0F) : 0x0F;

    const uint8_t yInChar = static_cast<uint8_t>(snap->rc & 0x07);

    const uint16_t charAddr =
        static_cast<uint16_t>(charBase + (static_cast<uint16_t>(screenByte) * 8) + yInChar);

    const uint8_t rowBits =
        mem ? mem->vicRead(charAddr, raster) : 0x00;

    out << "Background Cell Debug\n";
    out << "---------------------\n";
    out << "snapshot: " << snapSource << "\n";
    out << "raster: " << raster << "\n";
    out << "col: " << col << "\n";
    out << "mode: " << decodeModeName() << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "D011: $" << std::setw(2) << static_cast<int>(d011)
        << std::dec
        << " fineY=" << fineY
        << " RSEL=" << (((d011 & 0x08) != 0) ? 25 : 24)
        << " DEN=" << (((d011 & 0x10) != 0) ? 1 : 0)
        << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "D016: $" << std::setw(2) << static_cast<int>(d016)
        << std::dec
        << " fineX=" << fineX
        << " CSEL=" << (((d016 & 0x08) != 0) ? 40 : 38)
        << " MCM=" << (((d016 & 0x10) != 0) ? 1 : 0)
        << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "D018: $" << std::setw(2) << static_cast<int>(d018) << "\n";
    out << "screenBase: $" << std::setw(4) << screenBase << "\n";
    out << "charBase:   $" << std::setw(4) << charBase << "\n";
    out << "bitmapBase: $" << std::setw(4) << bitmapBase << "\n";

    out << std::dec << std::setfill(' ');

    out << "firstBadlineY: " << snap->firstBadlineY << "\n";
    out << "vcBase: " << snap->vcBase << "\n";
    out << "matrixRow: " << matrixRow << "\n";
    out << "vmliBase: " << snap->vmliBase << "\n";
    out << "vmliRow: " << vmliRow << "\n";
    out << "vmliFetchIndex: " << static_cast<int>(snap->vmliFetchIndex) << "\n";
    out << "rc/yInChar: " << static_cast<int>(yInChar) << "\n";
    out << "displayEnabled: " << (snap->displayEnabled ? 1 : 0) << "\n";
    out << "displayEnabledNext: " << (snap->displayEnabledNext ? 1 : 0) << "\n";
    out << "badLine: " << (snap->badLine ? 1 : 0) << "\n";
    out << "badLineSampled: " << (snap->badLineSampled ? 1 : 0) << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "screenAddr: $" << std::setw(4) << screenAddr << "\n";
    out << "screenByte: $" << std::setw(2) << static_cast<int>(screenByte) << "\n";
    out << "colorAddr:  $" << std::setw(4) << colorAddr << "\n";
    out << "colorByte:  $" << std::setw(2) << static_cast<int>(colorByte) << "\n";
    out << "charAddr:   $" << std::setw(4) << charAddr << "\n";
    out << "rowBits:    $" << std::setw(2) << static_cast<int>(rowBits) << "\n";

    out << std::dec << std::nouppercase << std::setfill(' ');

    return out.str();
}

std::string Vic::dumpBadlineState() const
{
    std::ostringstream oss;

    oss << "VIC badline/display row state\n";
    oss << "  raster=" << registers.raster
        << " cycle=" << currentCycle << "\n";

    oss << "  badLine=" << vicState.badLine
        << " badLineSampled=" << vicState.badLineSampled << "\n";

    oss << "  displayEnabled=" << vicState.displayEnabled
        << " displayEnabledNext=" << vicState.displayEnabledNext << "\n";

    oss << "  denSeenOn30=" << denSeenOn30
        << " firstBadlineY=" << firstBadlineY << "\n";

    oss << "  vcBase=" << vicState.vcBase
        << " vmliBase=" << vicState.vmliBase
        << " vmliFetchIndex=" << static_cast<int>(vicState.vmliFetchIndex)
        << " rc=" << static_cast<int>(vicState.rc) << "\n";

    return oss.str();
}

std::string Vic::dumpBorderState() const
{
    std::ostringstream oss;

    int raster = static_cast<int>(registers.raster);

    oss << "VIC border state\n";
    oss << "  raster=" << raster
        << " cycle=" << currentCycle << "\n";

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
    {
        oss << "  raster out of range\n";
        return oss.str();
    }

    const bool liveVertical =
        vicState.verticalBorder;

    const bool latchedVertical =
        borderVertical_per_raster[raster] != 0;

    int maskInnerX0 = 0;
    int maskInnerX1 = 0;
    innerWindowForRaster(raster, maskInnerX0, maskInnerX1);

    oss << "  live verticalBorder="
        << (liveVertical ? "on" : "off")
        << " latched verticalBorder="
        << (latchedVertical ? "on" : "off")
        << " match="
        << ((liveVertical == latchedVertical) ? "yes" : "NO")
        << "\n";

    oss << "  horizontal border window:\n"
        << "    latched openX=" << borderLeftOpenX_per_raster[raster]
        << " closeX=" << borderRightCloseX_per_raster[raster] << "\n"
        << "    mask innerX0=" << maskInnerX0
        << " innerX1=" << maskInnerX1 << "\n";

    oss << "  live border flags:"
        << " verticalBorder=" << (vicState.verticalBorder ? "on" : "off")
        << " leftBorder=" << (vicState.leftBorder ? "on" : "off")
        << " rightBorder=" << (vicState.rightBorder ? "on" : "off")
        << "\n";

    oss << "  live border window:"
        << " leftBorderOpenX=" << vicState.leftBorderOpenX
        << " rightBorderCloseX=" << vicState.rightBorderCloseX
        << "\n";

    const uint8_t latchedD011 = latchedD011ForRaster(raster);
    const uint8_t latchedD016 = latchedD016ForRaster(raster);

    oss << "  latched RSEL=" << ((latchedD011 & 0x08) ? 1 : 0)
        << " latched CSEL=" << ((latchedD016 & 0x08) ? 1 : 0)
        << "\n";

    oss << "  latched D011=$"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(latchedD011)
        << " latched D016=$"
        << std::setw(2)
        << static_cast<int>(latchedD016)
        << std::dec << std::nouppercase << std::setfill(' ')
        << "\n";

    oss << "  live D011=$"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(registers.control)
        << " live D016=$"
        << std::setw(2)
        << static_cast<int>(registers.control2)
        << std::dec << std::nouppercase << std::setfill(' ')
        << "\n";

    const VerticalBorderWindow vw =
        verticalBorderWindowForRaster(raster);

    oss << "  verticalWindow topOpen=" << vw.topOpen
        << " bottomClose=" << vw.bottomClose << "\n";

    oss << "  latched border:"
        << " verticalBorder="
        << ((borderVertical_per_raster[raster] != 0) ? "on" : "off")
        << " openX=" << borderLeftOpenX_per_raster[raster]
        << " closeX=" << borderRightCloseX_per_raster[raster]
        << "\n";

    return oss.str();
}

std::string Vic::dumpSpriteDmaState() const
{
    std::ostringstream oss;

    oss << "Sprite DMA / output state\n";
    oss << "------------------------------------------------------------\n";
    oss << "Raster=" << registers.raster
        << " Cycle=" << currentCycle
        << " D015=$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(registers.spriteEnabled)
        << " D017=$" << std::setw(2) << static_cast<int>(registers.spriteYExpansion)
        << " D01B=$" << std::setw(2) << static_cast<int>(registers.spritePriority)
        << " D01C=$" << std::setw(2) << static_cast<int>(registers.spriteMultiColor)
        << " D01D=$" << std::setw(2) << static_cast<int>(registers.spriteXExpansion)
        << std::dec << std::nouppercase << std::setfill(' ')
        << "\n\n";

    oss << "Spr En  Y    X    DMA Disp YExp MC MCBase Row CurRow Ptr  DataBase "
           "RowPrep RowLatched XStart Width OutBit Rep ShiftBytes  Mode@X Exp@X En@X\n";

    for (int s = 0; s < 8; ++s)
    {
        const SpriteUnit& u = spriteUnits[s];

        const bool enabled = (registers.spriteEnabled & (1 << s)) != 0;
        const int sx = spriteScreenXFor(s, static_cast<int>(registers.raster));

        const bool modeAtX =
            (sx >= 0 && sx < 512) ? spriteMulticolorAtPixel(s, sx) : false;

        const bool expAtX =
            (sx >= 0 && sx < 512) ? spriteXExpandedAtPixel(s, sx) : false;

        const bool enAtX =
            (sx >= 0 && sx < 512) ? spriteEnabledAtPixel(s, sx) : false;

        oss << std::setw(3) << s << " "
            << " " << (enabled ? 1 : 0) << "  "
            << std::setw(3) << static_cast<int>(registers.spriteY[s]) << "  "
            << std::setw(4) << sx << "   "
            << (u.dmaActive ? 1 : 0) << "    "
            << (u.displayActive ? 1 : 0) << "    "
            << (u.yExpandLatch ? 1 : 0) << "   "
            << std::setw(2) << static_cast<int>(u.mc) << "   "
            << std::setw(2) << static_cast<int>(u.mcBase) << "     "
            << std::setw(2) << spriteRowFromMCBase(s) << "    "
            << std::setw(2) << u.currentRow << "   "
            << "$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(u.pointerByte)
            << "  $"
            << std::setw(4) << static_cast<int>(u.dataBase)
            << "   $"
            << std::setw(2) << static_cast<int>(u.shift0)
            << " $"
            << std::setw(2) << static_cast<int>(u.shift1)
            << " $"
            << std::setw(2) << static_cast<int>(u.shift2)
            << std::dec << std::nouppercase << std::setfill(' ')
            << "      "
            << (u.rowPrepared ? 1 : 0) << "          "
            << (u.rowDataLatched ? 1 : 0) << "      "
            << std::setw(4) << u.outputXStart << "   "
            << std::setw(3) << u.outputWidth << "    "
            << std::setw(2) << u.outputBit << "   "
            << std::setw(2) << u.outputRepeat << "      "
            << (modeAtX ? 1 : 0) << "     "
            << (expAtX ? 1 : 0) << "    "
            << (enAtX ? 1 : 0)
            << "\n";
    }

    oss << "\nNotes:\n";
    oss << "  Row = mcBase / 3. CurRow tracks physical raster line in Y-expanded mode.\n";
    oss << "  Mode@X / Exp@X / En@X are sampled at the sprite's current X start.\n";
    oss << "  ShiftBytes are the currently latched 3-byte sprite row.\n";

    oss << "\nLast collision timing:\n";

    if (lastSpriteSpriteCollision.valid)
    {
        oss << "  sprite-sprite:"
            << " raster=" << lastSpriteSpriteCollision.raster
            << " x=" << lastSpriteSpriteCollision.x
            << " cycle=" << lastSpriteSpriteCollision.cycle
            << " bits=$"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(lastSpriteSpriteCollision.bits)
            << std::dec << std::nouppercase << std::setfill(' ')
            << "\n";
    }
    else
    {
        oss << "  sprite-sprite: none\n";
    }

    if (lastSpriteBackgroundCollision.valid)
    {
        oss << "  sprite-background:"
            << " raster=" << lastSpriteBackgroundCollision.raster
            << " x=" << lastSpriteBackgroundCollision.x
            << " cycle=" << lastSpriteBackgroundCollision.cycle
            << " bits=$"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(lastSpriteBackgroundCollision.bits)
            << std::dec << std::nouppercase << std::setfill(' ')
            << "\n";
    }
    else
    {
        oss << "  sprite-background: none\n";
    }

    return oss.str();
}

std::string Vic::dumpBadlineTimelineAroundRaster(int centerRaster) const
{
    std::ostringstream oss;

    oss << "Bad-line timeline around raster " << centerRaster << "\n";
    oss << "------------------------------------------------------------\n";
    oss << "Raster  D011  DEN  YSC  Bad  LatchedBad  Disp  DispNext  RC  VCBase  VMLIBase  VMLIIdx\n";

    for (int r = centerRaster - 8; r <= centerRaster + 8; ++r)
    {
        if (r < 0 || r >= cfg_->maxRasterLines)
            continue;

        const uint8_t d011 = effectiveD011ForRaster(r);
        const bool den = (d011 & 0x10) != 0;
        const int yscroll = d011 & 0x07;
        const bool bad = isBadLine(r);

        oss << std::setw(6) << r << "  "
            << "$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(d011)
            << std::dec << std::nouppercase << std::setfill(' ') << "    "
            << (den ? 1 : 0) << "    "
            << yscroll << "    "
            << (bad ? 1 : 0) << "       ";

        if (r == registers.raster)
        {
            oss << (vicState.badLineSampled ? 1 : 0) << "          "
                << (vicState.displayEnabled ? 1 : 0) << "         "
                << (vicState.displayEnabledNext ? 1 : 0) << "       "
                << static_cast<int>(vicState.rc) << "   "
                << vicState.vcBase << "      "
                << vicState.vmliBase << "       "
                << static_cast<int>(vicState.vmliFetchIndex);
        }
        else
        {
            oss << "-          -         -       -   -       -        -";
        }

        oss << "\n";
    }

    return oss.str();
}

std::string Vic::dumpBorderWindowAroundRaster(int centerRaster) const
{
    std::ostringstream oss;

    oss << "VIC border window around raster " << centerRaster << "\n";

    for (int r = centerRaster - 4; r <= centerRaster + 4; ++r)
    {
        if (r < 0 || r >= static_cast<int>(cfg_->maxRasterLines))
            continue;

        const VerticalBorderWindow vw = verticalBorderWindowForRaster(r);
        const bool latchedVertical = borderVertical_per_raster[r] != 0;
        const bool withinWindow = rasterWithinVerticalDisplayWindow(r);

        oss << "  raster=" << r
            << " withinWindow=" << (withinWindow ? "yes" : "no")
            << " latchedVertical=" << (latchedVertical ? "on" : "off")
            << " topOpen=" << vw.topOpen
            << " bottomClose=" << vw.bottomClose
            << " D011=$"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(latchedD011ForRaster(r))
            << " D016=$"
            << std::setw(2)
            << static_cast<int>(latchedD016ForRaster(r))
            << std::dec << std::nouppercase << std::setfill(' ')
            << "\n";
    }

    return oss.str();
}

bool Vic::vicTraceOn(TraceManager::TraceDetail d) const
{
    return traceMgr && traceMgr->vicDetailOn(d);
}

TraceManager::Stamp Vic::makeVicStamp() const
{
    if (!traceMgr)
        return TraceManager::Stamp{0, 0xFFFF, 0xFFFF};

    return traceMgr->makeStamp(
        processor ? processor->getTotalCycles() : 0,
        registers.raster,
        static_cast<uint16_t>(currentCycle * 8));
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
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BADLINE))
        return;

    std::ostringstream out;
    out << "[VIC:BADLINE] fetch"
        << " raster=" << std::dec << raster
        << " cycle=" << cycle
        << " idx=" << fetchIndex
        << " vc=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << vc
        << " row=" << std::dec << row
        << " col=" << col
        << " screen=$" << std::hex << std::uppercase << std::setw(2) << int(screenByte)
        << " color=$" << std::setw(2) << int(colorByte);

    traceMgr->recordVicBadline(out.str(), makeVicStamp());
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
        << " disp=" << (su.displayActive ? 1 : 0)
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
        << " disp=" << (spriteUnits[sprite].displayActive ? 1 : 0)
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
        << " disp=" << int(spriteUnits[sprite].displayActive)
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
        << " disp=" << int(spriteUnits[sprite].displayActive)
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

    return "none";
}

const char* Vic::busOwnerName(BusOwner owner) const
{
    switch(owner)
    {
        case Vic::BusOwner::BadLine:
            return "BADLINE";
        case Vic::BusOwner::CPU:
            return "CPU";
        case Vic::BusOwner::Idle:
            return "IDLE";
        case Vic::BusOwner::Refresh:
            return "REFRESH";
        case Vic::BusOwner::SpriteData:
            return "SPRITE DATA";
        case Vic::BusOwner::SpritePointer:
            return "SPRITE POINTER";
        default:
            return "?";
    }
}

bool Vic::fetchKindIsSpritePointer(Vic::FetchKind kind) const
{
    switch (kind)
    {
        case Vic::FetchKind::SpritePtr0:
        case Vic::FetchKind::SpritePtr1:
        case Vic::FetchKind::SpritePtr2:
        case Vic::FetchKind::SpritePtr3:
        case Vic::FetchKind::SpritePtr4:
        case Vic::FetchKind::SpritePtr5:
        case Vic::FetchKind::SpritePtr6:
        case Vic::FetchKind::SpritePtr7:
            return true;

        default:
            return false;
    }
}

bool Vic::fetchKindIsSpriteData(Vic::FetchKind kind) const
{
    switch (kind)
    {
        case Vic::FetchKind::SpriteData0:
        case Vic::FetchKind::SpriteData1:
        case Vic::FetchKind::SpriteData2:
        case Vic::FetchKind::SpriteData3:
        case Vic::FetchKind::SpriteData4:
        case Vic::FetchKind::SpriteData5:
        case Vic::FetchKind::SpriteData6:
        case Vic::FetchKind::SpriteData7:
            return true;

        default:
            return false;
    }
}
