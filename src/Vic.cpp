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
    registers.rasterInterruptLine = cfg_->maxRasterLines + 1;
    registers.undefined = 0xFF; // Undefined always returns 0xFF

    // AEC
    currentCycle = 0;
    AEC = true;

    // Raster IRQ
    rasterIrqSampledThisLine = false;

    // Internal VIC state
    vicState.vcBase = 0;
    vicState.rc = 0;

    vicState.displayEnabled = false;
    vicState.displayEnabledNext = false;
    vicState.badLine = false;

    vicState.verticalBorder = true;
    vicState.leftBorder = true;
    vicState.rightBorder = true;

    vicState.leftBorderOpenX = 0;
    vicState.rightBorderCloseX = VISIBLE_WIDTH;

    vicState.topBorderOpenRaster = 0;
    vicState.bottomBorderCloseRaster = 0;

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
    }

    std::fill(std::begin(sprPtrBase), std::end(sprPtrBase), 0);
    for (auto& line : spriteOpaqueLine) line.fill(0);
    for (auto& line : spriteColorLine)  line.fill(0);

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

    borderMaskLine.fill(1);
    finalColorLine.fill(0);

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
    wrtr.writeU8(vicState.rc);

    wrtr.writeBool(vicState.displayEnabled);
    wrtr.writeBool(vicState.badLine);

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

        wrtr.writeI32(s.outputXStart);
        wrtr.writeI32(s.outputWidth);
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

        const uint16_t disabled = uint16_t(cfg_->maxRasterLines + 1);
        if (registers.rasterInterruptLine != disabled)
        {
            if (registers.rasterInterruptLine >= cfg_->maxRasterLines)
                registers.rasterInterruptLine %= cfg_->maxRasterLines;
        }

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
        if (!rdr.readU8(vicState.rc))                           { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.displayEnabled))             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.badLine))                    { rdr.exitChunkPayload(chunk); return false; }

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

            if (!rdr.readI32(s.outputXStart))                   { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(s.outputWidth))                    { rdr.exitChunkPayload(chunk); return false; }
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
            const uint8_t highBit = (registers.raster >> 8) & 0x01;

            // Bits:
            // 7 = raster high bit (defined)
            // 6 = ECM
            // 5 = BMM
            // 4 = DEN
            // 3 = RSEL
            // 2-0 = Y scroll
            const uint8_t value = (registers.control & 0x7F) | (highBit << 7);

            return latchOpenBusMasked(value, 0xFF); // keep for now (safe step)
        }

        case 0xD012:
            return latchOpenBus(registers.raster & 0xFF);

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
    // Handle SpriteX and SpriteY registers with helper
    if (address >= 0xD000 && address <= 0xD00F)
    {
        int index = getSpriteIndex(address);

        if (isSpriteX(address))
        {
            const uint8_t oldValue = registers.spriteX[index];
            registers.spriteX[index] = value;
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
        registers.spriteColors[index] = value & 0x0F; // Mask to 4 bits
        traceVicRegWrite(address, oldValue, registers.spriteColors[index]);
        return;
    }

    switch (address)
    {
        case 0xD010:
        {
            const uint8_t oldValue = registers.spriteX_MSB;
            registers.spriteX_MSB = value;
            traceVicRegWrite(address, oldValue, registers.spriteX_MSB);
            break;
        }

        case 0xD011:
        {
            const uint8_t oldValue = registers.control;
            const uint16_t oldLine = registers.rasterInterruptLine;
            const uint16_t newLine = (oldLine & 0x00FF) | ((value & 0x80) << 1);

            registers.rasterInterruptLine = newLine;
            registers.control = value & 0x7F;

            const int raster = registers.raster;
            d011_per_raster[raster] = registers.control;
            updateGraphicsMode(raster);

            updateVerticalBorderState(raster);
            borderVertical_per_raster[raster] = vicState.verticalBorder ? 1 : 0;

            traceVicRegWrite(address, oldValue, registers.control);
            checkRasterIRQCompareTransition(oldLine, newLine);
            break;
        }

        case 0xD012:
        {
            const uint8_t oldValue = static_cast<uint8_t>(registers.rasterInterruptLine & 0x00FF);
            const uint16_t oldLine = registers.rasterInterruptLine;
            const uint16_t newLine = (oldLine & 0xFF00) | value;

            registers.rasterInterruptLine = newLine;

            traceVicRegWrite(address, oldValue, static_cast<uint8_t>(registers.rasterInterruptLine & 0x00FF));
            checkRasterIRQCompareTransition(oldLine, newLine);
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
            traceVicRegWrite(address, oldValue, registers.spriteEnabled);
            break;
        }

        case 0xD016:
        {
            const uint8_t oldValue = registers.control2;
            registers.control2 = value;

            const int raster = registers.raster;
            updateHorizontalBorderState(raster);
            borderLeftOpenX_per_raster[raster] = static_cast<int16_t>(vicState.leftBorderOpenX);
            borderRightCloseX_per_raster[raster] = static_cast<int16_t>(vicState.rightBorderCloseX);
            d016_per_raster[raster] = registers.control2;
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
            const int raster = registers.raster;
            d018_per_raster[raster] = registers.memory_pointer;
            traceVicRegWrite(address, oldValue, registers.memory_pointer);
            break;
        }

        case 0xD019:
        {
            const uint8_t oldValue = registers.interruptStatus & 0x0F;
            value &= 0x0F;
            registers.interruptStatus &= ~value;
            traceVicRegWrite(address, oldValue, static_cast<uint8_t>(registers.interruptStatus & 0x0F));
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
            traceVicRegWrite(address, oldValue, registers.spritePriority);
            break;
        }

        case 0xD01C:
        {
            const uint8_t oldValue = registers.spriteMultiColor;
            registers.spriteMultiColor = value;
            traceVicRegWrite(address, oldValue, registers.spriteMultiColor);
            break;
        }

        case 0xD01D:
        {
            const uint8_t oldValue = registers.spriteXExpansion;
            registers.spriteXExpansion = value;
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

        // Per-cycle timing-sensitive decisions happen at their own checkpoints.
        runCycleDecisionPhase();

        // Existing fetch ownership stays unchanged for now.
        runFetchPhase();

        advanceCycleAndFinalizeLineIfNeeded();

        updatePerCycleState();
    }
}

void Vic::beginFrameIfNeeded()
{
    // Clear DEN latch at frame start
    if (currentCycle == 0 && registers.raster == 0)
    {
        firstBadlineY = -1;
        denSeenOn30 = false;

        // Reset VIC-style display counters at frame start
        vicState.vcBase = 0;
        vicState.rc = 0;
        vicState.badLine = false;
        vicState.displayEnabled = false;
    }

    if (registers.raster == 0x30)
    {
        if (d011_per_raster[0x30] & 0x10)
            denSeenOn30 = true;
    }
}

void Vic::runCycleDecisionPhase()
{
    if (currentCycle == RASTER_IRQ_COMPARE_CYCLE && !rasterIrqSampledThisLine)
    {
        const bool matched = rasterCompareMatchesNow();
        traceVicCycleCheckpoint("raster-irq-sample", registers.raster, currentCycle);
        traceVicRasterRetargetTest("normal-sample", registers.rasterInterruptLine, registers.rasterInterruptLine, rasterIrqSampledThisLine, matched);

        rasterIrqSampledThisLine = true;
        triggerRasterIRQIfMatched();
    }

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

    updateVerticalBorderState(raster);
    updateHorizontalBorderState(raster);

    borderVertical_per_raster[raster] = vicState.verticalBorder ? 1 : 0;
    borderLeftOpenX_per_raster[raster] = static_cast<int16_t>(vicState.leftBorderOpenX);
    borderRightCloseX_per_raster[raster] = static_cast<int16_t>(vicState.rightBorderCloseX);

    traceVicCycleCheckpoint("cycle-0", raster, currentCycle);
}

void Vic::handleCycle14Decisions()
{
    const int raster = registers.raster;
    const bool badNow = isBadLine(raster);

    // Bad-line condition becomes authoritative here.
    vicState.badLine = badNow;

    traceVicCycleCheckpoint("cycle-14", raster, currentCycle);

    if (badNow)
    {
        vicState.rc = 0;
    }
}

void Vic::handleCycle15Decisions()
{
    const int raster = registers.raster;

    if (vicState.badLine)
    {
        initializeFirstBadLineIfNeeded();
        traceVicBadLineStart(raster, currentCycle, vicState.vcBase, vicState.rc,
                             (registers.control & 0x10) != 0);
        beginBadLineFetch();
    }
}

void Vic::handleSpriteDmaStartDecisions()
{
    updateSpriteDMAStartForCurrentLine();
}

void Vic::handleDmaStartCycleDecisions()
{
    const int raster = registers.raster;
    const uint16_t nextRaster = (raster + 1) % cfg_->maxRasterLines;

    // Keep current latch timing behavior for now.
    d011_per_raster[nextRaster] = registers.control & 0x7F;
    d016_per_raster[nextRaster] = registers.control2;
    d018_per_raster[nextRaster] = registers.memory_pointer;
    dd00_per_raster[nextRaster] = cia2object ? cia2object->getCurrentVICBank() : 0;
    updateMonitorCaches(nextRaster);
}

void Vic::handleCycle58Decisions()
{
    traceVicCycleCheckpoint("cycle-58", registers.raster, currentCycle);

    const int raster = registers.raster;
    const bool den = (effectiveD011ForRaster(raster) & 0x10) != 0;
    const int visibleRows = getRSEL(raster) ? 25 : 24;

    if (!denSeenOn30 || firstBadlineY < 0 || !den)
    {
        vicState.displayEnabledNext = false;
        return;
    }

    const int screenRowBefore = currentCharacterRow();
    vicState.displayEnabledNext =
        (screenRowBefore >= 0 && screenRowBefore < visibleRows);
}

void Vic::runFetchPhase()
{
    const int raster = registers.raster;
    const int cycle  = currentCycle;

    switch (getFetchKindForCycle(raster, cycle))
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

uint16_t Vic::spritePointerAddressForRaster(int sprite, int raster) const
{
    return static_cast<uint16_t>(getScreenBase(raster) + 0x03F8 + sprite);
}

void Vic::performBadLineFetchesForCurrentCycle()
{
    if (!vicState.badLine)
        return;

    if (currentCycle < 15 || currentCycle > 54)
        return;

    const int raster = registers.raster;
    const int fetchIndex = currentCycle - 15;
    fetchBadLineMatrixByte(fetchIndex, raster);
}

void Vic::initializeFirstBadLineIfNeeded()
{
    if (firstBadlineY >= 0)
        return;

    firstBadlineY = registers.raster;

    // First visible character row starts at VCBASE = 0.
    vicState.vcBase = 0;
    vicState.rc = 0;
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
    // Prepare sprite row/output state for this raster
    prepareSpriteOutputForRaster(curRaster);

    // Raster-progressive sprite output into line buffers
    beginSpriteRasterOutput(curRaster);

    int sx0, sx1;
    spriteVisibleXRange(sx0, sx1);
    for (int px = sx0; px < sx1; ++px)
    {
        stepSpriteSequencersAtX(curRaster, px);
    }

    // Render and collisions for this line
    renderLine(curRaster);
    detectSpriteToSpriteCollision(curRaster);
    detectSpriteToBackgroundCollision(curRaster);

    // Advance/finish sprite DMA state
    updateSpriteDMAEndOfLine(curRaster);

    // Advance VIC-style display counters
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

void Vic::syncSpriteCompatAddress(int sprite)
{
    sprPtrBase[sprite] = spriteUnits[sprite].dataBase;
}

uint32_t Vic::getLatchedSpriteBits(int sprite) const
{
    return  (uint32_t(spriteUnits[sprite].shift0) << 16)
          | (uint32_t(spriteUnits[sprite].shift1) << 8)
          |  uint32_t(spriteUnits[sprite].shift2);
}

void Vic::fetchSpritePointer(int sprite, int raster)
{
    if (!mem)
        return;

    const uint16_t ptrLoc = spritePointerAddressForRaster(sprite, raster);
    const uint8_t ptr = mem->vicRead(ptrLoc, raster);

    traceVicSpritePtrFetch(sprite, raster, ptrLoc, ptr);

    spriteUnits[sprite].pointerByte = ptr;
    spriteUnits[sprite].dataBase = static_cast<uint16_t>(ptr) << 6;

    syncSpriteCompatAddress(sprite);
    traceVicSpriteSlotEvent(sprite, "ptr", raster, currentCycle);
}

void Vic::prepareSpriteOutputForRaster(int raster)
{
    (void)raster;

    for (int i = 0; i < 8; ++i)
    {
        spriteUnits[i].rowPrepared = false;
        spriteUnits[i].outputBit = 0;
        spriteUnits[i].outputRepeat = 0;
        spriteUnits[i].outputXStart = 0;
        spriteUnits[i].outputWidth = 0;

        if (!(registers.spriteEnabled & (1 << i)))
        {
            spriteUnits[i].shift0 = 0;
            spriteUnits[i].shift1 = 0;
            spriteUnits[i].shift2 = 0;
            continue;
        }

        if (!spriteUnits[i].dmaActive)
{
    spriteUnits[i].shift0 = 0;
    spriteUnits[i].shift1 = 0;
    spriteUnits[i].shift2 = 0;
    continue;
}

        beginSpriteLineOutput(i, raster);
    }
}

int Vic::spritePreparedOutputWidth(int sprIndex) const
{
    const bool expandX = (registers.spriteXExpansion & (1 << sprIndex)) != 0;
    return expandX ? 48 : 24;
}

void Vic::beginSpriteLineOutput(int spr, int raster)
{
    int rowInSprite = 0;
    int fbLine = 0;

    spriteUnits[spr].rowPrepared = false;
    spriteUnits[spr].outputBit = 0;
    spriteUnits[spr].outputRepeat = 0;
    spriteUnits[spr].outputXStart = 0;
    spriteUnits[spr].outputWidth = 0;

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

void Vic::advanceSpriteOutputState(int sprIndex)
{
    const bool expandX = (registers.spriteXExpansion & (1 << sprIndex)) != 0;
    const bool multClr = (registers.spriteMultiColor & (1 << sprIndex)) != 0;

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

bool Vic::currentSpriteSequencerPixel(int sprIndex, uint8_t& outColor, bool& opaque) const
{
    outColor = 0;
    opaque = false;

    if (!spriteUnits[sprIndex].rowPrepared)
        return false;

    const bool multClr = (registers.spriteMultiColor & (1 << sprIndex)) != 0;
    const uint32_t rowBits = getLatchedSpriteBits(sprIndex);

    if (!multClr)
    {
        const int srcBit = spriteUnits[sprIndex].outputBit;
        if (srcBit < 0 || srcBit >= 24)
            return false;

        if (((rowBits >> (23 - srcBit)) & 0x01) == 0)
            return false;

        outColor = registers.spriteColors[sprIndex] & 0x0F;
        opaque = true;
        return true;
    }
    else
    {
        const int srcPair = spriteUnits[sprIndex].outputBit;
        if (srcPair < 0 || srcPair >= 12)
            return false;

        const uint8_t bits = (rowBits >> (22 - srcPair * 2)) & 0x03;
        if (bits == 0)
            return false;

        const uint8_t mc1 = registers.spriteMultiColor1 & 0x0F;
        const uint8_t mc2 = registers.spriteMultiColor2 & 0x0F;
        const uint8_t col = registers.spriteColors[sprIndex] & 0x0F;

        outColor =
            (bits == 1) ? mc1 :
            (bits == 2) ? col :
                          mc2;

        opaque = true;
        return true;
    }
}

void Vic::clearSpriteLineBuffers()
{
    for (auto& line : spriteOpaqueLine) line.fill(0);
    for (auto& line : spriteColorLine)  line.fill(0);
}

void Vic::beginSpriteRasterOutput(int raster)
{
    clearSpriteLineBuffers();

    for (int spr = 0; spr < 8; ++spr)
    {
        if (!spriteUnits[spr].rowPrepared)
        {
            spriteUnits[spr].displayActive = false;
            continue;
        }

        spriteUnits[spr].displayActive = true;
        resetSpriteLineSequencer(spr, raster);
        traceVicSpriteSlotEvent(spr, "display-begin", raster, currentCycle);
    }
}

void Vic::stepSpriteSequencersAtX(int raster, int px)
{
    (void)raster;

    for (int spr = 0; spr < 8; ++spr)
    {
        if (!spriteUnits[spr].rowPrepared)
            continue;

        const int startX = spriteUnits[spr].outputXStart;
        const int endX   = startX + spriteUnits[spr].outputWidth;

        if (px < startX || px >= endX)
            continue;

        uint8_t color = 0;
        bool opaque = false;

        if (currentSpriteSequencerPixel(spr, color, opaque) && opaque)
        {
            spriteOpaqueLine[spr][px] = 1;
            spriteColorLine[spr][px]  = color;
        }

        advanceSpriteOutputState(spr);
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
            spriteUnits[s].currentRow = spriteRowFromMCBase(s) * 2;
        else
            spriteUnits[s].currentRow = spriteRowFromMCBase(s);

        traceVicSpriteSlotEvent(s, "eol-after", raster, currentCycle);

        if (isSpriteDMAComplete(s))
        {
            traceVicSpriteSlotEvent(s, "dma-stop", raster, currentCycle);
            resetSpriteDMAState(s);
        }
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

    return (spriteUnits[spr].currentRow & 1) != 0;
}

bool Vic::isSpriteDMAComplete(int spr) const
{
    return spriteUnits[spr].mcBase >= 63;
}

void Vic::resetSpriteDMAState(int spr)
{
    spriteUnits[spr].dmaActive = false;
    spriteUnits[spr].displayActive = false;

    spriteUnits[spr].currentRow = 0;
    spriteUnits[spr].mc = 0;
    spriteUnits[spr].mcBase = 0;

    spriteUnits[spr].rowPrepared = false;
    spriteUnits[spr].outputBit = 0;
    spriteUnits[spr].outputRepeat = 0;
    spriteUnits[spr].outputXStart = 0;
    spriteUnits[spr].outputWidth = 0;

    spriteUnits[spr].shift0 = 0;
    spriteUnits[spr].shift1 = 0;
    spriteUnits[spr].shift2 = 0;
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
}

bool Vic::isSpritePointerFetchCycle(int sprite, int cycle) const
{
    return cycle == spriteFetchSlotStart(sprite);
}

void Vic::updateSpriteDMAStartForCurrentLine()
{
    for (int s = 0; s < 8; ++s)
    {
        const bool enabled = ((registers.spriteEnabled >> s) & 0x01) != 0;
        const bool yExp = ((registers.spriteYExpansion >> s) & 0x01) != 0;
        const bool rasterMatch = (registers.raster == registers.spriteY[s]);

        // Keep this aligned with your real start condition.
        const bool willStart = enabled && rasterMatch;

        traceVicSpriteStartCheck(s, registers.raster, registers.spriteY[s], enabled, yExp, rasterMatch, willStart);

        if (!willStart)
            continue;

        spriteUnits[s].dmaActive = true;
        spriteUnits[s].displayActive = true;
        spriteUnits[s].yExpandLatch = yExp;
        spriteUnits[s].currentRow = 0;
        spriteUnits[s].mc = 0;
        spriteUnits[s].mcBase = 0;
        spriteUnits[s].startY = registers.spriteY[s];

        spriteUnits[s].outputBit = 0;
        spriteUnits[s].outputRepeat = 0;
        spriteUnits[s].rowPrepared = false;
        spriteUnits[s].outputXStart = 0;
        spriteUnits[s].outputWidth = 0;
        spriteUnits[s].shift0 = 0;
        spriteUnits[s].shift1 = 0;
        spriteUnits[s].shift2 = 0;

        traceVicSpriteDmaStart(s);
        traceVicSpriteSlotEvent(s, "dma-start", registers.raster, currentCycle);
    }
}

void Vic::updateBusArbitration()
{
    const int raster = registers.raster;
    const int cycle  = currentCycle;

    const bool badLineNow = vicState.badLine;

    const bool baLow  = shouldBALow(raster, cycle);
    const bool aecLow = shouldAECLow(raster, cycle);

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

bool Vic::isBadLineBusWarningCycle(int raster, int cycle) const
{
    if (!vicState.badLine)
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
    if (!vicState.badLine)
        return false;

    return cycle >= cfg_->DMAStartCycle &&
           cycle <= cfg_->DMAEndCycle;
}

bool Vic::isSpriteBusWarningCycle(int raster, int cycle) const
{
    (void)raster;

    const int lineCycles = cfg_->cyclesPerLine;

    for (int s = 0; s < 8; ++s)
    {
        if (!spriteUnits[s].dmaActive)
            continue;

        const int slot = spriteFetchSlotStart(s);

        const int warn0 = (slot - 3 + lineCycles) % lineCycles;
        const int warn1 = (slot - 2 + lineCycles) % lineCycles;
        const int warn2 = (slot - 1 + lineCycles) % lineCycles;

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

        if (isSpritePointerFetchCycle(s, cycle))
            return true;

        if (isSpriteDMAFetchCycle(s, cycle))
            return true;
    }

    return false;
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
           isSpriteBusStealCycle(raster, cycle);
}

bool Vic::isBadLine(int raster) const
{
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return false;

    const uint8_t d011 = effectiveD011ForRaster(raster);

    if (!denSeenOn30) return false;
    if (!(d011 & 0x10)) return false; // DEN

    const bool rsel = getRSEL(raster);                   // $D011 bit 3
    const int last = rsel ? 0xF7 : 0xEF;                 // 25 rows vs 24 rows

    if (raster < 0x30 || raster > last) return false;
    if ((raster & 0x07) != fineYScroll(raster)) return false;

    return true;
}

void Vic::beginBadLineFetch()
{
    vicState.rc = 0;
}

void Vic::fetchBadLineMatrixByte(int fetchIndex, int raster)
{
    const uint16_t vc = static_cast<uint16_t>(vicState.vcBase + fetchIndex);
    const int row = static_cast<int>(vc / 40);
    const int col = static_cast<int>(vc % 40);

    const uint8_t screenByte = fetchScreenByte(row, col, raster);
    const uint8_t colorByte  = fetchColorByte(row, col, raster) & 0x0F;

    charPtrFIFO[fetchIndex]  = screenByte;
    colorPtrFIFO[fetchIndex] = colorByte;

    traceVicBadLineFetch(raster, currentCycle, fetchIndex, vc, row, col, screenByte, colorByte);
}

void Vic::renderLine(int raster)
{
    if (!IO_adapter || !mem) return;

    updateGraphicsMode(raster);

    generateBackgroundLine(raster);
    buildBorderMaskLine(raster);
    composeFinalRasterLine(raster);
    emitRasterLineInOrder(raster);
}

void Vic::renderTextLine(int raster, int xScroll)
{
    int rows = getRSEL(raster) ? 25 : 24;
    int cols = getCSEL(raster) ? 40 : 38;

    int charRow = currentCharacterRow();
    if (charRow < 0 || charRow >= rows) return;

    int yInChar = static_cast<int>(vicState.rc & 0x07);
    int fine    = xScroll & 7;
    int fetchCols = cols + (fine ? 1 : 0);

    const int x0 = std::max(0, vicState.leftBorderOpenX);
    const int x1 = std::min(VISIBLE_WIDTH, vicState.rightBorderCloseX);

    int xStart = x0 - fine;

    int py = fbY(raster);

    for (int col = 0; col < fetchCols; ++col)
    {
        int px = xStart + col * 8;
        if (px >= x1) break;
        if (px + 8 <= x0) continue;

        if (col > 40) break;

        const int displayCol = (col < 40) ? col : 39;
        const uint8_t scrByte   = resolveDisplayScreenByte(displayCol, raster);
        const uint8_t colorByte = resolveDisplayColorByte(displayCol, raster);

        uint8_t bgColor = registers.backgroundColor0;

        const bool mcGlobal = (effectiveD016ForRaster(raster) & 0x10) != 0;
        const bool mcCell   = (colorByte & 0x08) != 0;     // bit3
        const bool mcMode   = mcGlobal && mcCell;

        if (!mcMode)
            renderChar(scrByte, px, py, colorByte, bgColor, yInChar, raster, x0, x1);
        else
            renderCharMultiColor(scrByte, px, py, colorByte & 0x07, bgColor, yInChar, raster, x0, x1);
    }
}

void Vic::renderBitmapLine(int raster, int xScroll)
{
    int rows = getRSEL(raster) ? 25 : 24;
    int cols = getCSEL(raster) ? 40 : 38;
    int firstVis = cfg_->firstVisibleLine;

    int charRow = currentCharacterRow();
    if (charRow < 0 || charRow >= rows) return;

    int bitmapY = raster - firstVis;
    if (bitmapY < 0 || bitmapY >= rows * 8) return;

    const uint16_t bitmapBase = getBitmapBase(raster);

    const int fine = xScroll & 7;

    const int fetchCols = cols;

    const int x0 = std::max(0, vicState.leftBorderOpenX);
    const int x1 = std::min(VISIBLE_WIDTH, vicState.rightBorderCloseX);

    int xStart = x0 - fine;

    const int py = fbY(raster);

    for (int col = 0; col < fetchCols; ++col)
    {
        const uint16_t byteOffset =
            (uint16_t)((bitmapY & 7) + (col * 8) + ((bitmapY >> 3) * 320));

        const uint8_t byte = mem->vicRead(bitmapBase + byteOffset, raster);

        const uint8_t scr = resolveDisplayScreenByte(col, raster);
        const uint8_t fgColor = (scr >> 4) & 0x0F;
        const uint8_t bgColor = scr & 0x0F;

        const int cellLeft = xStart + col * 8;
        if (cellLeft >= x1) break;
        if (cellLeft + 8 <= x0) continue;

        for (int bit = 0; bit < 8; ++bit)
        {
            const bool pixelOn = ((byte >> (7 - bit)) & 0x01) != 0;
            const uint8_t color = pixelOn ? fgColor : bgColor;

            const int pxRaw = cellLeft + bit;
            if (pxRaw < x0 || pxRaw >= x1) continue;

            bgColorLine[pxRaw] = color & 0x0F;

            if (pixelOn) markBGOpaque(py, pxRaw);
        }
    }
}

void Vic::renderBitmapMulticolorLine(int raster, int xScroll)
{
    int rows = getRSEL(raster) ? 25 : 24;
    int cols = getCSEL(raster) ? 40 : 38;
    int firstVis = cfg_->firstVisibleLine;

    int charRow = currentCharacterRow();
    if (charRow < 0 || charRow >= rows) return;

    int bitmapY = raster - firstVis;
    if (bitmapY < 0 || bitmapY >= rows * 8) return;

    const uint16_t bitmapBase = getBitmapBase(raster);

    const int fine = xScroll & 7;

    const int fetchCols = cols;

    const int x0 = std::max(0, vicState.leftBorderOpenX);
    const int x1 = std::min(VISIBLE_WIDTH, vicState.rightBorderCloseX);

    int xStart = x0 - fine;

    const int py = fbY(raster);

    for (int col = 0; col < fetchCols; ++col)
    {
        const uint16_t byteOffset =
            (uint16_t)((bitmapY & 7) + (col * 8) + ((bitmapY >> 3) * 320));

        const uint8_t byte = mem->vicRead(bitmapBase + byteOffset, raster);

        const uint8_t scr = resolveDisplayScreenByte(col, raster);
        const uint8_t colNib = resolveDisplayColorByte(col, raster);
        const uint8_t bg0 = registers.backgroundColor0 & 0x0F;

        const int cellLeft = xStart + col * 8;
        if (cellLeft >= x1) break;
        if (cellLeft + 8 <= x0) continue;

        for (int pair = 0; pair < 4; ++pair)
        {
            const uint8_t bits = (byte >> (6 - pair * 2)) & 0x03;

            const uint8_t color =
                (bits == 0) ? bg0 :
                (bits == 1) ? ((scr >> 4) & 0x0F) :
                (bits == 2) ? (scr & 0x0F) :
                              colNib;

            const int pxRaw = cellLeft + pair * 2;
            if (pxRaw < x0 || pxRaw + 1 >= x1) continue;

            bgColorLine[pxRaw] = color & 0x0F;
            bgColorLine[pxRaw + 1] = color & 0x0F;

            if (bits != 0)
            {
                markBGOpaque(py, pxRaw);
                markBGOpaque(py, pxRaw + 1);
            }
        }
    }
}

void Vic::renderECMLine(int raster, int xScroll)
{
    int rows = getRSEL(raster) ? 25 : 24;
    int cols = getCSEL(raster) ? 40 : 38;

    int charRow = currentCharacterRow();
    if (charRow < 0 || charRow >= rows) return;

    int yInChar = static_cast<int>(vicState.rc & 0x07);
    int fine = xScroll & 7;
    int fetchCols = cols + (fine ? 1 : 0);

    const int x0 = std::max(0, vicState.leftBorderOpenX);
    const int x1 = std::min(VISIBLE_WIDTH, vicState.rightBorderCloseX);

    int xStart = x0 - fine;

    int py = fbY(raster);

    for (int col = 0; col < fetchCols; ++col)
    {
        if (col > 40) break;

        const int displayCol = (col < 40) ? col : 39;
        const uint8_t scrByte   = resolveDisplayScreenByte(displayCol, raster);
        const uint8_t colorByte = resolveDisplayColorByte(displayCol, raster);

        uint8_t charIndex = scrByte & 0x3F;
        uint8_t bgSel = (scrByte >> 6) & 0x03;
        uint8_t bgColor =
            (bgSel == 0) ? registers.backgroundColor0 :
            (bgSel == 1) ? getBackgroundColor(0) :
            (bgSel == 2) ? getBackgroundColor(1) :
                           getBackgroundColor(2);

        uint8_t fgColor = colorByte & 0x0F;

        int pxCell = xStart + col * 8;
        if (pxCell >= x1) break;
        if (pxCell + 8 <= x0) continue;

        uint16_t addr = getCHARBase(raster) + charIndex * 8;
        uint8_t row = mem->vicRead(addr + yInChar, raster);

        for (int bit = 0; bit < 8; ++bit)
        {
            bool pixelOn = (row >> (7 - bit)) & 0x01;
            uint8_t color = pixelOn ? fgColor : bgColor;

            int pxRaw = pxCell + bit;
            if (pxRaw < x0 || pxRaw >= x1) continue;

            bgColorLine[pxRaw] = color & 0x0F;

            if (pixelOn)
                markBGOpaque(py, pxRaw);
        }
    }
}

void Vic::clearBackgroundLineBuffers()
{
    bgColorLine.fill(registers.borderColor & 0x0F);
    bgOpaqueLine.fill(0);
}

void Vic::generateBackgroundLine(int raster)
{
    clearBackgroundLineBuffers();

    const int screenY = fbY(raster);
    const bool DEN = (effectiveD011ForRaster(raster) & 0x10) != 0;

    const int leftInner  = std::max(0, int(borderLeftOpenX_per_raster[raster]));
    const int rightInner = std::min(VISIBLE_WIDTH, int(borderRightCloseX_per_raster[raster]));

    // If display is effectively closed, leave border-filled line buffer.
    if (!DEN || borderVertical_per_raster[raster] != 0)
    {
        return;
    }

    // Fill the interior with background color first for non-bitmap modes.
    if (!(currentMode == graphicsMode::bitmap || currentMode == graphicsMode::multiColorBitmap))
    {
        const uint8_t bg = registers.backgroundColor0 & 0x0F;
        for (int px = leftInner; px < rightInner && px < 512; ++px)
            bgColorLine[px] = bg;
    }

    const int lineXScroll = fineXScroll(raster);

    switch (currentMode)
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

    // Mirror the generated opacity into the frame-sized background opacity map.
    if (screenY >= 0 && screenY < (int)bgOpaque.size())
    {
        for (int px = 0; px < 512; ++px)
            bgOpaque[screenY][px] = bgOpaqueLine[px];
    }
}

void Vic::emitRasterLineInOrder(int raster)
{
    const int xStart = rasterVisibleStartX(raster);
    const int xEnd   = rasterVisibleEndX(raster);

    int currentRasterX = xStart;

    while (currentRasterX < xEnd)
    {
        emitRasterPixel(raster, currentRasterX);
        ++currentRasterX;
    }
}

void Vic::emitRasterPixel(int raster, int px)
{
    if (!IO_adapter)
        return;

    const int screenY = fbY(raster);
    IO_adapter->setPixel(px, screenY, produceRasterPixel(raster, px));
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
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return false;

    const int openX  = std::max(0, int(borderLeftOpenX_per_raster[raster]));
    const int closeX = std::min(VISIBLE_WIDTH, int(borderRightCloseX_per_raster[raster]));

    return px >= openX && px < closeX;
}

void Vic::buildBorderMaskLine(int raster)
{
    std::fill(borderMaskLine.begin(), borderMaskLine.begin() + VISIBLE_WIDTH, 1);

    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return;

    if (borderVertical_per_raster[raster] != 0)
    return;

    const int openX  = std::max(0, int(borderLeftOpenX_per_raster[raster]));
    const int closeX = std::min(VISIBLE_WIDTH, int(borderRightCloseX_per_raster[raster]));

    if (openX >= closeX)
        return;

    bool borderOn = true;

    for (int px = 0; px < VISIBLE_WIDTH; ++px)
    {
        if (borderOn && px >= openX)
            borderOn = false;

        if (!borderOn && px >= closeX)
            borderOn = true;

        borderMaskLine[px] = borderOn ? 1 : 0;
    }
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
    const uint8_t border = registers.borderColor & 0x0F;

    uint8_t color = border;

    const bool borderPixel = borderActiveAtPixel(raster, px);

    if (!borderPixel)
    {
        color = bgColorLine[px] & 0x0F;
    }

    // Sprites behind background:
    // only visible if background is not opaque at this pixel.
    for (int spr = 0; spr < 8; ++spr)
    {
        const bool behind = (registers.spritePriority & (1 << spr)) != 0;
        if (!behind)
            continue;

        if (!spriteOpaqueLine[spr][px])
            continue;

        if (!bgOpaqueLine[px])
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

void Vic::updateIRQLine()
{
    // Low 4 bits only
    const uint8_t pending = (registers.interruptStatus & registers.interruptEnable) & 0x0F;
    const bool any = pending != 0;

    if (!IRQ) return;
    if (any)  IRQ->raiseIRQ(IRQLine::VICII);
    else      IRQ->clearIRQ(IRQLine::VICII);

    if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::VIC))
    {
        TraceManager::Stamp stamp = traceMgr->makeStamp(processor ? processor->getTotalCycles() : 0, registers.raster, (currentCycle * 8));
        traceMgr->recordVicIrq(any, stamp);
    }
}

void Vic::triggerRasterIRQIfMatched()
{
    if (!rasterCompareMatchesNow())
        return;

    if ((registers.interruptStatus & 0x01) == 0)
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

    registers.interruptStatus |= newlySet;
    updateIRQLine();
}

void Vic::checkRasterIRQCompareTransition(uint16_t oldLine, uint16_t newLine)
{
    if (oldLine == newLine)
        return;

    const uint16_t cur = registers.raster;

    const bool oldMatch = (cur == oldLine);
    const bool newMatch = (cur == newLine);

    if (oldMatch || !newMatch)
        return;

    // Too late: this line's decision point has already passed.
    if (currentCycle > RASTER_IRQ_COMPARE_CYCLE)
        return;

    // If we're exactly on the decision cycle and have already sampled it,
    // do not generate it again from the register write path.
    if (currentCycle == RASTER_IRQ_COMPARE_CYCLE && rasterIrqSampledThisLine)
        return;

    // Source already latched.
    if ((registers.interruptStatus & 0x01) != 0)
        return;

    raiseVicIRQSource(0x01);
}

void Vic::detectSpriteToSpriteCollision(int raster)
{
    uint8_t old = registers.spriteCollision;

    for (int i = 0; i < 8; ++i)
    {
        if (!(registers.spriteEnabled & (1 << i))) continue;
        for (int j = i + 1; j < 8; ++j)
        {
            if (!(registers.spriteEnabled & (1 << j))) continue;
            if (checkSpriteSpriteOverlapOnLine(i, j, raster))
                registers.spriteCollision |= (1 << i) | (1 << j);
        }
    }

    if (registers.spriteCollision & ~old) //&& (registers.interruptEnable & 0x02) && IRQ)
        raiseVicIRQSource(0x02);
}

bool Vic::checkSpriteSpriteOverlapOnLine(int A, int B, int raster)
{
    int ra, rb, fbLine;
    if (!spriteDisplayCoversRaster(A, raster, ra, fbLine)) return false;
    if (!spriteDisplayCoversRaster(B, raster, rb, fbLine)) return false;

    if (!spriteUnits[A].rowPrepared || !spriteUnits[B].rowPrepared)
        return false;

    const int a0 = spriteUnits[A].outputXStart;
    const int a1 = a0 + spriteUnits[A].outputWidth;
    const int b0 = spriteUnits[B].outputXStart;
    const int b1 = b0 + spriteUnits[B].outputWidth;

    const int startX = std::max(a0, b0);
    const int endX   = std::min(a1, b1);

    if (startX >= endX)
        return false;

    for (int px = startX; px < endX; ++px)
    {
        if (px >= 0 && px < 512 &&
            spriteOpaqueLine[A][px] &&
            spriteOpaqueLine[B][px])
        {
            return true;
        }
    }

    return false;
}

void Vic::detectSpriteToBackgroundCollision(int raster)
{
    uint8_t old = registers.spriteDataCollision;

    for (int i = 0; i < 8; ++i)
    {
        if (!(registers.spriteEnabled & (1 << i))) continue;
        if (checkSpriteBackgroundOverlap(i, raster)) registers.spriteDataCollision |= (1 << i);
    }

    if (registers.spriteDataCollision & ~old) //&& (registers.interruptEnable & 0x04) && IRQ)
        raiseVicIRQSource(0x04);
}

bool Vic::checkSpriteBackgroundOverlap(int spriteIndex, int raster)
{
    int rowInSprite, fbLine;
    if (!spriteDisplayCoversRaster(spriteIndex, raster, rowInSprite, fbLine))
        return false;

    if (!spriteUnits[spriteIndex].rowPrepared)
        return false;

    const int startX = spriteUnits[spriteIndex].outputXStart;
    const int endX   = startX + spriteUnits[spriteIndex].outputWidth;

    for (int px = startX; px < endX; ++px)
    {
        if (px >= 0 && px < 512 &&
            spriteOpaqueLine[spriteIndex][px] &&
            isBackgroundPixelOpaque(px, fbLine))
        {
            return true;
        }
    }

    return false;
}

int Vic::spriteScreenXFor(int sprIndex, int raster) const
{
    int x = registers.spriteX[sprIndex];
    if (registers.spriteX_MSB & (1 << sprIndex))
        x += 256;

    // Apply VIC-II hardware offset + border
    return (x - cfg_->hardware_X) + BORDER_SIZE;
}

bool Vic::spriteDisplayCoversRaster(int sprIndex, int raster, int &rowInSprite, int &fbLine) const
{
    fbLine = fbY(raster);

    if (!(registers.spriteEnabled & (1 << sprIndex)))
        return false;

    if (!spriteUnits[sprIndex].displayActive)
        return false;

    const int startY = spriteUnits[sprIndex].startY;
    const bool yExp  = spriteUnits[sprIndex].yExpandLatch;

    int rasterDelta = raster - startY;
    if (rasterDelta < 0)
        rasterDelta += cfg_->maxRasterLines;

    const int spriteHeight = yExp ? 42 : 21;
    if (rasterDelta < 0 || rasterDelta >= spriteHeight)
        return false;

    rowInSprite = yExp ? (rasterDelta / 2) : rasterDelta;

    return rowInSprite >= 0 && rowInSprite < 21;
}

bool Vic::isBackgroundPixelOpaque(int x, int y)
{
    if (y < 0 || y >= (int)bgOpaque.size()) return false;
    if (x < 0 || x >= (int)bgOpaque[y].size()) return false;
    return bgOpaque[y][x] != 0;
}

void Vic::updateGraphicsMode(int raster)
{
    const uint8_t d011 = effectiveD011ForRaster(raster);
    const uint8_t d016 = effectiveD016ForRaster(raster);

    const bool MCM = (d016 & 0x10) != 0;
    const bool BMM = (d011 & 0x20) != 0;
    const bool ECM = (d011 & 0x40) != 0;

    if (!BMM && !MCM && !ECM)
    {
        currentMode = graphicsMode::standard;
    }
    else if (!BMM && MCM && !ECM)
    {
        currentMode = graphicsMode::multiColor;
    }
    else if (!BMM && !MCM && ECM)
    {
        currentMode = graphicsMode::extendedColorText;
    }
    else if (BMM && !MCM)
    {
        currentMode = graphicsMode::bitmap;
    }
    else if (BMM && MCM)
    {
        currentMode = graphicsMode::multiColorBitmap;
    }
    else
    {
        currentMode = graphicsMode::invalid;
    }
}

void Vic::innerWindowForRaster(int raster, int& x0, int& x1) const
{
    const int cols = getCSEL(raster) ? 40 : 38;

    // Center the active text window: 40 cols = 320 px, 38 cols = 304 px.
    // The 38-column mode should shrink by 8 px on each side, not 4 on one side.
    const int innerWidth = cols * 8;
    const int fullWidth  = 40 * 8;
    const int inset      = (fullWidth - innerWidth) / 2;

    x0 = BORDER_SIZE + inset;
    x1 = x0 + innerWidth;
}

void Vic::getInnerDisplayBounds(int raster, int& leftInner, int& rightInner) const
{
    innerWindowForRaster(raster, leftInner, rightInner);
}

void Vic::renderChar(uint8_t c, int x, int y, uint8_t fg, uint8_t bg, int yInChar, int raster, int x0, int x1)
{
    uint16_t address = getCHARBase(raster) + c * 8;
    uint8_t row = mem->vicRead(address + yInChar, raster);

    for (int col = 0; col < 8; ++col)
    {
        int pxRaw = x + col;
        if (pxRaw < x0 || pxRaw >= x1) continue;

        bool bit = (row >> (7 - col)) & 0x01;
        uint8_t color = bit ? (fg & 0x0F) : (bg & 0x0F);

        bgColorLine[pxRaw] = color & 0x0F;

        if (bit)
            markBGOpaque(y, pxRaw);
    }
}

void Vic::renderCharMultiColor(uint8_t c, int x, int y, uint8_t cellCol, uint8_t bg, int yInChar, int raster, int x0, int x1)
{
    uint16_t address = getCHARBase(raster) + c * 8;
    uint8_t  row  = mem->vicRead(address + yInChar, raster);

    uint8_t bg1 = registers.backgroundColor[0] & 0x0F;
    uint8_t bg2 = registers.backgroundColor[1] & 0x0F;
    uint8_t colRAM = (uint8_t)(cellCol) & 0x07;

    for (int pair = 0; pair < 4; ++pair)
    {
        uint8_t bits = (row >> ((3 - pair) * 2)) & 0x03;
        uint8_t col  =
            (bits == 0) ? (bg & 0x0F) :
            (bits == 1) ? bg1 :
            (bits == 2) ? bg2 :
                          colRAM;

        const int p0 = x + pair * 2;
        const int p1 = p0 + 1;

        if (p0 >= x1) break;
        if (p1 <  x0) continue;

        // Per-pixel clipping + marking
        if (p0 >= x0 && p0 < x1)
        {
            bgColorLine[p0] = col & 0x0F;
            if (bits != 0) markBGOpaque(y, p0);
        }
        if (p1 >= x0 && p1 < x1)
        {
            bgColorLine[p1] = col & 0x0F;
            if (bits != 0) markBGOpaque(y, p1);
        }
    }
}

uint8_t Vic::fetchScreenByte(int row, int col, int raster) const
{
    const uint16_t address = getScreenBase(raster) + row * 40 + col;
    return mem->vicRead(address, raster);
}

uint8_t Vic::fetchColorByte(int row, int col, int raster) const
{
    const uint16_t address = COLOR_MEMORY_START + row * 40 + col;
    return mem->vicReadColor(address);
}

int Vic::currentDisplayRowBase() const
{
    return static_cast<int>(vicState.vcBase);
}

uint8_t Vic::fetchDisplayScreenByte(int col, int raster) const
{
    int row = 0;
    int c = 0;
    currentDisplayRowCol(col, row, c);
    return fetchScreenByte(row, c, raster);
}

uint8_t Vic::fetchDisplayColorByte(int col, int raster) const
{
    int row = 0;
    int c = 0;
    currentDisplayRowCol(col, row, c);
    return fetchColorByte(row, c, raster) & 0x0F;
}

uint8_t Vic::resolveDisplayScreenByte(int displayCol, int raster) const
{
    if (displayCol >= 0 && displayCol < 40)
        return charPtrFIFO[displayCol];

    return fetchDisplayScreenByte(displayCol, raster);
}

uint8_t Vic::resolveDisplayColorByte(int displayCol, int raster) const
{
    if (displayCol >= 0 && displayCol < 40)
        return colorPtrFIFO[displayCol] & 0x0F;

    return fetchDisplayColorByte(displayCol, raster);
}

void Vic::advanceVideoCountersEndOfLine(int raster)
{
    vicState.displayEnabled = vicState.displayEnabledNext;

    if (!vicState.displayEnabled)
        return;

    vicState.rc = static_cast<uint8_t>((vicState.rc + 1) & 0x07);

    if (vicState.rc == 0)
        vicState.vcBase = static_cast<uint16_t>(vicState.vcBase + 40);

    const int visibleRows = getRSEL(raster) ? 25 : 24;
    const int screenRowAfter = currentCharacterRow();

    if (screenRowAfter < 0 || screenRowAfter >= visibleRows)
        vicState.displayEnabled = false;
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
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return false;

    return borderVertical_per_raster[raster] == 0;
}

bool Vic::horizontalBorderLatchedAtPixel(int raster, int px) const
{
    if (px < 0 || px >= VISIBLE_WIDTH)
        return true;

    return !isInnerDisplayPixel(raster, px);
}

void Vic::updateVerticalBorderState(int raster)
{
    const bool den = (effectiveD011ForRaster(raster) & 0x10) != 0;
    if (!den || !denSeenOn30 || firstBadlineY < 0)
    {
        vicState.topBorderOpenRaster = 0;
        vicState.bottomBorderCloseRaster = 0;
        vicState.verticalBorder = true;
        return;
    }

    const int visibleRows = getRSEL(raster) ? 25 : 24;
    const int topOpen = firstBadlineY;
    const int bottomClose = topOpen + visibleRows * 8;

    vicState.topBorderOpenRaster = topOpen;
    vicState.bottomBorderCloseRaster = bottomClose;

    vicState.verticalBorder =
        !(raster >= vicState.topBorderOpenRaster &&
          raster <  vicState.bottomBorderCloseRaster);
}

void Vic::updateHorizontalBorderState(int raster)
{
    const bool csel40 = getCSEL(raster);

    if (csel40)
    {
        vicState.leftBorderOpenX = 31;
        vicState.rightBorderCloseX = 351;
    }
    else
    {
        vicState.leftBorderOpenX = 38;
        vicState.rightBorderCloseX = 344;
    }

    vicState.leftBorder  = false;
    vicState.rightBorder = false;

    if (vicState.leftBorderOpenX >= vicState.rightBorderCloseX)
    {
        vicState.leftBorder  = true;
        vicState.rightBorder = true;
    }
}

bool Vic::borderActiveAtPixel(int raster, int px) const
{
    (void)raster;

    if (px < 0 || px >= VISIBLE_WIDTH)
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

uint8_t Vic::d019Read() const
{
    const uint8_t src = registers.interruptStatus & 0x0F;
    const uint8_t irq = ((src & registers.interruptEnable & 0x0F) != 0) ? 0x80 : 0x00;
    return irq | src;
}

std::string Vic::decodeModeName() const
{
    bool ecm = d011_per_raster[registers.raster] & 0x40;
    bool bmm = d011_per_raster[registers.raster] & 0x20;
    bool mcm = d016_per_raster[registers.raster] & 0x10;

    if (!bmm && !mcm && !ecm) return "Text";
    if (ecm && !bmm && !mcm) return "ECM (Extended Color Mode)";
    if (!bmm && mcm) return "Multicolor Text";
    if (bmm && !mcm) return "Bitmap";
    if (bmm && mcm)  return "Multicolor Bitmap";
    return "Unknown";
}

std::string Vic::getVICBanks() const
{
    std::stringstream out;
    out << std::hex << std::uppercase << std::setfill('0');

    // Current VIC bank base (CIA2 16 KB window)
    uint16_t bankBase = dd00_per_raster[registers.raster];

    out << "Active VIC Bank = " << (bankBase >> 14)
        << " ($" << std::setw(4) << bankBase
        << "-$" << std::setw(4) << (bankBase + 0x3FFF) << ")\n\n";

    uint16_t charOffset   = getCHARBase(registers.raster);
    uint16_t screenOffset = getScreenBase(registers.raster);
    uint16_t bitmapOffset = getBitmapBase(registers.raster);

    out << "CHAR Base   = offset $"   << std::setw(4) << charOffset
        << "  ->  address $" << std::setw(4) << (bankBase + charOffset) << "\n";

    out << "Screen Base = offset $" << std::setw(4) << screenOffset
        << "  ->  address $" << std::setw(4) << (bankBase + screenOffset) << "\n";

    out << "Bitmap Base = offset $" << std::setw(4) << bitmapOffset
        << "  ->  address $" << std::setw(4) << (bankBase + bitmapOffset) << "\n";

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

        out << "D019 = $" << std::setw(2) << int(d019Status) << "   (Pending: Raster=" << ((pending & 0x01) ? "Y" : "N")
        << ", SprSpr=" << ((pending & 0x02) ? "Y" : "N") << ", SprBg="  << ((pending & 0x04) ? "Y" : "N") << ", LightPen="
        << ((pending & 0x08) ? "Y" : "N") << ", IRQ line=" << (irqLine ? "Low" : "High") << ")\n";

        out << "D01A = $" << std::setw(2) << int(registers.interruptEnable) << "   (Enable: Raster=" << ((enabled & 0x01) ? "Y" : "N")
        << ", SprSpr=" << ((enabled & 0x02) ? "Y" : "N") << ", SprBg="  << ((enabled & 0x04) ? "Y" : "N") << ", LightPen="
        << ((enabled & 0x08) ? "Y" : "N") << ")\n";
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
    uint16_t currentVICBank = dd00_per_raster[raster];

    // Build the full address
    charBaseCache = getCHARBase(raster) + currentVICBank;
    screenBaseCache = getScreenBase(raster) + currentVICBank;
    bitmapBaseCache = getBitmapBase(raster) + currentVICBank;
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

Vic::FetchKind Vic::getFetchKindForCycle(int raster, int cycle) const
{
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return FetchKind::None;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return FetchKind::None;

    if (isBadLine(raster) &&
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

    const bool badLine = isBadLine(raster);
    const FetchKind fk = getFetchKindForCycle(raster, cycle);

    out << "VIC Cycle Debug\n\n";
    out << "Raster: " << raster << "\n";
    out << "Cycle : " << cycle << "\n";
    out << "Fetch : " << fetchKindName(fk) << "\n";
    out << "Badline: " << (badLine ? "Yes" : "No") << "\n";
    out << "VCBASE: " << vicState.vcBase << "\n";
    out << "RC    : " << int(vicState.rc) << "\n";
    out << "BA    : " << (vicState.ba ? "High" : "Low") << "\n";
    out << "AEC   : " << (vicState.aec ? "High" : "Low") << "\n";
    out << "DEN@raster: " << (((d011_per_raster[raster] & 0x10) != 0) ? "On" : "Off") << "\n";
    out << "DisplayRow: " << currentCharacterRow() << "\n";
    out << "FineY: " << int(fineYScroll(raster)) << "\n";
    out << "FineX: " << int(fineXScroll(raster)) << "\n";

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
    out << "Badline: " << (isBadLine(raster) ? "Yes" : "No") << "\n\n";

    for (int c = 0; c < cfg_->cyclesPerLine; ++c)
    {
        out << std::setw(2) << c << ": "
            << fetchKindName(getFetchKindForCycle(raster, c))
            << "\n";
    }

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

void Vic::traceVicSpriteStartCheck(int sprite, int raster, uint8_t spriteY, bool enabled, bool yExpanded, bool rasterMatch,
    bool willStart) const
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
        << " en=" << std::dec << (enabled ? 1 : 0)
        << " yexp=" << (yExpanded ? 1 : 0)
        << " match=" << (rasterMatch ? 1 : 0)
        << " start=" << (willStart ? 1 : 0)
        << " dma=" << (spriteUnits[sprite].dmaActive ? 1 : 0)
        << " disp=" << (spriteUnits[sprite].displayActive ? 1 : 0);

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
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
