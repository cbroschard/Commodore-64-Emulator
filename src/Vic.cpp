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
    vicState.badLine = false;

    vicState.verticalBorder = true;

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

    bgColorLine.fill(0);
    bgOpaqueLine.fill(0);

    finalColorLine.fill(0);

    // Fill in DD00
    uint16_t currentVICBank = cia2object ? cia2object->getCurrentVICBank() : 0;
    std::fill(std::begin(dd00_per_raster), std::end(dd00_per_raster), currentVICBank);

    // Initialize bgOpaque
    bgOpaque.resize(cfg_->visibleLines + 2*BORDER_SIZE);
    for (auto &row : bgOpaque) row.fill(0);

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
            return latchOpenBus(registers.spriteX_MSB);

        case 0xD011:
        {
            const uint8_t highBit = (registers.raster >> 8) & 0x01;
            const uint8_t value = (registers.control & 0x7F) | (highBit << 7);
            return latchOpenBusMasked(value, 0xFF);
        }

        case 0xD012:
            return latchOpenBus(registers.raster & 0xFF);

        case 0xD013:
            return latchOpenBus(registers.light_pen_X);

        case 0xD014:
            return latchOpenBus(registers.light_pen_Y);

        case 0xD015:
            return latchOpenBus(registers.spriteEnabled);

        case 0xD016:
            return latchOpenBusMasked(registers.control2, 0xFF);

        case 0xD017:
            return latchOpenBus(registers.spriteYExpansion);

        case 0xD018:
            return latchOpenBusMasked(registers.memory_pointer, 0xFF);

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
            return latchOpenBus(registers.spriteMultiColor);

        case 0xD01D:
            return latchOpenBus(registers.spriteXExpansion);

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
            registers.spriteX[index] = value;
        }
        else
        {
            registers.spriteY[index] = value;
        }
        return;
    }
    // Handle multicolor registers with helper
    else if (address >= 0xD022 && address <= 0xD024)
    {
        registers.backgroundColor[address - 0xD022] = value & 0x0F;
        return;
    }
    // Handle Sprite Color registers with helper
    else if (address >= 0xD027 && address <= 0xD02E)
    {
        int index = getSpriteColorIndex(address);
        registers.spriteColors[index] = value & 0x0F; // Mask to 4 bits
        return;
    }
    switch(address)
    {
        case 0xD010:
        {
            registers.spriteX_MSB = value;
            break;
        }
        case 0xD011:
        {
            const uint16_t oldLine = registers.rasterInterruptLine;
            const uint16_t newLine = (oldLine & 0x00FF) | ((value & 0x80) << 1);

            registers.rasterInterruptLine = newLine;
            registers.control = value & 0x7F;

            checkRasterIRQCompareTransition(oldLine, newLine);
            break;
        }
        case 0xD012:
        {
            const uint16_t oldLine = registers.rasterInterruptLine;
            const uint16_t newLine = (oldLine & 0xFF00) | value;

            registers.rasterInterruptLine = newLine;

            checkRasterIRQCompareTransition(oldLine, newLine);
            break;
        }
        case 0xD013:
        {
            registers.light_pen_X = value;
            break;
        }
        case 0xD014:
        {
            registers.light_pen_Y = value;
            break;
        }
        case 0xD015:
        {
            registers.spriteEnabled = value;
            break;
        }
        case 0xD016:
        {
            registers.control2 = value;
            break;
        }
        case 0xD017:
        {
            registers.spriteYExpansion = value;
            break;
        }
        case 0xD018:
        {
            registers.memory_pointer = value & 0xFE;
            break;
        }
        case 0xD019:
        {
            value &= 0x0F;
            registers.interruptStatus &= ~value;
            updateIRQLine();
            break;
        }
        case 0xD01A:
        {
            const uint8_t mask = value & 0x0F;

            if (value & 0x80)
                registers.interruptEnable |= mask;   // set bits
            else
                registers.interruptEnable &= ~mask;  // clear bits

            updateIRQLine();
            break;
        }
        case 0xD01B:
        {
            registers.spritePriority = value;
            break;
        }
        case 0xD01C:
        {
            registers.spriteMultiColor = value;
            break;
        }
        case 0xD01D:
        {
            registers.spriteXExpansion = value;
            break;
        }
        case 0xD01E:
        case 0xD01F:
            break;
        case 0xD020:
        {
            registers.borderColor = value & 0x0F;
            break;
        }
        case 0xD021:
        {
            registers.backgroundColor0 = value & 0x0F;
            break;
        }
        case 0xD025:
        {
            registers.spriteMultiColor1 = value & 0x0F;
            break;
        }
        case 0xD026:
        {
            registers.spriteMultiColor2 = value & 0x0F;
            break;
        }
        case 0xD02F:
        {
            break;
        }
        case 0xD030:
        {
            break;
        }
        default:
        {
            if (logger && setLogging)
            {
                logger->WriteLog("Attempt to write to unhandled vic area address = " + std::to_string (static_cast<int>(address)));
                break;
            }
        }
    }
}

void Vic::tick(int cycles)
{
    while (cycles-- > 0)
    {
        beginFrameIfNeeded();
        beginRasterIfNeeded();

        performRasterNMinus1Latch();
        performSpritePointerFetches();
        performSpriteDataFetches();
        performBadLineCycle();

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

void Vic::beginRasterIfNeeded()
{
    if (currentCycle == RASTER_IRQ_COMPARE_CYCLE && !rasterIrqSampledThisLine)
    {
        rasterIrqSampledThisLine = true;
        triggerRasterIRQIfMatched();
    }
}

void Vic::performRasterNMinus1Latch()
{
    // N-1 latching
    uint16_t nextRaster = (registers.raster + 1) % cfg_->maxRasterLines;

    if (currentCycle == cfg_->DMAStartCycle)
    {
        d011_per_raster[nextRaster] = registers.control & 0x7F;
        d016_per_raster[nextRaster] = registers.control2;
        d018_per_raster[nextRaster] = registers.memory_pointer;
        dd00_per_raster[nextRaster] = cia2object ? cia2object->getCurrentVICBank() : 0;
        updateMonitorCaches(nextRaster);

        // Sprite DMA becomes state-driven.
        updateSpriteDMAStartForCurrentLine();
    }
}

void Vic::performSpritePointerFetches()
{
    // Fetch sprite pointers in each sprite's pointer slot
    for (int i = 0; i < 8; ++i)
    {
        if (spriteUnits[i].dmaActive && currentCycle == spriteFetchSlotStart(i))
        {
            fetchSpritePointer(i, registers.raster);
        }
    }
}

void Vic::performBadLineCycle()
{
    const int raster = registers.raster;
    const int cycle  = currentCycle;

    if (!isBadLine(raster))
    {
        vicState.badLine = false;
        return;
    }

    initializeFirstBadLineIfNeeded();
    startBadLineIfNeeded(raster, cycle);
    runBadLineFetchCycle(raster, cycle);
}

void Vic::initializeFirstBadLineIfNeeded()
{
    if (firstBadlineY >= 0)
        return;

    firstBadlineY = registers.raster;

    // First visible character row starts at VCBASE = 0.
    vicState.vcBase = 0;
    vicState.rc = 0;
    vicState.displayEnabled = true;
}

void Vic::startBadLineIfNeeded(int raster, int cycle)
{
    (void)raster;

    if (cycle == cfg_->DMAStartCycle)
    {
        beginBadLineFetch();
    }
}

void Vic::runBadLineFetchCycle(int raster, int cycle)
{
    const int fetchIndex = cycle - cfg_->DMAStartCycle;

    if (fetchIndex >= 0 && fetchIndex < 40)
    {
        fetchBadLineMatrixByte(fetchIndex, raster);
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

    // Update border state for this raster
    updateVerticalBorderState(curRaster);

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
    if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::VIC))
    {
        TraceManager::Stamp stamp =
            traceMgr->makeStamp(processor ? processor->getTotalCycles() : 0,
                                registers.raster,
                                (currentCycle * 8));

        // Record raster state at end of line
        traceMgr->recordVicRaster(registers.raster, currentCycle,
                                  (registers.interruptStatus & 0x01) != 0,
                                  registers.control,
                                  registers.rasterInterruptLine & 0xFF,
                                  stamp);
    }
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

    const uint16_t ptrLoc = getScreenBase(raster) + 0x03F8 + sprite;
    const uint8_t ptr = mem->vicRead(ptrLoc, raster);

    spriteUnits[sprite].pointerByte = ptr;
    spriteUnits[sprite].dataBase = static_cast<uint16_t>(ptr) << 6;

    syncSpriteCompatAddress(sprite);
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

        if (!spriteUnits[i].displayActive)
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
            continue;
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
    (void)raster;

    for (int s = 0; s < 8; ++s)
    {
        if (!spriteUnits[s].dmaActive)
            continue;

        // Keep currentRow as a compatibility/debug mirror for now.
        spriteUnits[s].currentRow++;

        // Advance sprite data progression only when this line consumes a new
        // logical sprite row.
        if (shouldAdvanceSpriteMCBaseThisLine(s))
        {
            spriteUnits[s].mcBase += 3;
        }

        // Mirror mc from mcBase for now.
        spriteUnits[s].mc = spriteUnits[s].mcBase;

        // Keep currentRow consistent with mcBase as we transition away from
        // line-count-driven behavior.
        if (spriteUnits[s].yExpandLatch)
            spriteUnits[s].currentRow = spriteRowFromMCBase(s) * 2;
        else
            spriteUnits[s].currentRow = spriteRowFromMCBase(s);

        if (isSpriteDMAComplete(s))
        {
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
        const bool enabled = (registers.spriteEnabled & (1 << s)) != 0;
        if (!enabled)
        {
            spriteUnits[s].dmaActive = false;
            spriteUnits[s].displayActive = false;
            spriteUnits[s].currentRow = 0;
            spriteUnits[s].rowPrepared = false;
            spriteUnits[s].outputBit = 0;
            spriteUnits[s].outputRepeat = 0;
            spriteUnits[s].mc = 0;
            spriteUnits[s].mcBase = 0;
            spriteUnits[s].outputXStart = 0;
            spriteUnits[s].outputWidth = 0;
            spriteUnits[s].shift0 = 0;
            spriteUnits[s].shift1 = 0;
            spriteUnits[s].shift2 = 0;
            continue;
        }

        const bool yExp = (registers.spriteYExpansion & (1 << s)) != 0;

        if (registers.raster == registers.spriteY[s])
        {
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
        }
    }
}

void Vic::updateBusArbitration()
{
    const int raster = registers.raster;
    const int cycle  = currentCycle;

    const bool badLineNow = isBadLine(raster);
    const bool baLow      = shouldBALow(raster, cycle);
    const bool aecLow     = shouldAECLow(raster, cycle);

    vicState.badLine = badLineNow;
    vicState.ba      = !baLow;
    vicState.aec     = !aecLow;

    AEC = vicState.aec;

    if (processor)
    {
        processor->setBAHold(!vicState.ba);
    }
}

bool Vic::isBadLineBusWarningCycle(int raster, int cycle) const
{
    if (!isBadLine(raster))
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
    if (!isBadLine(raster))
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
    vicState.badLine = true;
    vicState.rc = 0;
}

void Vic::fetchBadLineMatrixByte(int fetchIndex, int raster)
{
    const uint16_t vc = static_cast<uint16_t>(vicState.vcBase + fetchIndex);
    const int row = static_cast<int>(vc / 40);
    const int col = static_cast<int>(vc % 40);

    charPtrFIFO[fetchIndex]  = fetchScreenByte(row, col, raster);
    colorPtrFIFO[fetchIndex] = fetchColorByte(row, col, raster) & 0x0F;
}

void Vic::renderLine(int raster)
{
    if (!IO_adapter || !mem) return;

    updateGraphicsMode(raster);

    generateBackgroundLine(raster);
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

    int x0, x1;
    getInnerDisplayBounds(raster, x0, x1);

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

        const bool mcGlobal = (d016_per_raster[raster] & 0x10) != 0;
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

    int x0, x1;
    getInnerDisplayBounds(raster, x0, x1);
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

    int x0, x1;
    getInnerDisplayBounds(raster, x0, x1);
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
    int x0, x1;
    getInnerDisplayBounds(raster, x0, x1);
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
    const bool DEN = (d011_per_raster[raster] & 0x10) != 0;

    int leftInner, rightInner;
    getInnerDisplayBounds(raster, leftInner, rightInner);

    // If display is effectively closed, leave border-filled line buffer.
    if (!DEN || !verticalDisplayOpenForRaster(raster))
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
    int leftInner, rightInner;
    getInnerDisplayBounds(raster, leftInner, rightInner);
    return px >= leftInner && px < rightInner;
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

    registers.interruptStatus |= masked;
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

    // Too late: this line's compare point has already passed.
    if (currentCycle > RASTER_IRQ_COMPARE_CYCLE)
        return;

    // If we're exactly on the compare cycle and have already sampled it,
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
    bool MCM = (d016_per_raster[raster] & 0x10) != 0;
    bool BMM = (d011_per_raster[raster] & 0x20) != 0;
    bool ECM = (d011_per_raster[raster] & 0x40) != 0;

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
    const int vc = currentDisplayRowBase() + col;
    const int row = vc / 40;
    const int c   = vc % 40;
    return fetchScreenByte(row, c, raster);
}

uint8_t Vic::fetchDisplayColorByte(int col, int raster) const
{
    const int vc = currentDisplayRowBase() + col;
    const int row = vc / 40;
    const int c   = vc % 40;
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
    const bool den = (d011_per_raster[raster] & 0x10) != 0;
    const int visibleRows = getRSEL(raster) ? 25 : 24;

    // No display progression until DEN has been seen and badline regime has started.
    if (!den || firstBadlineY < 0)
    {
        vicState.displayEnabled = false;
        return;
    }

    const int screenRow = currentCharacterRow();
    if (screenRow < 0 || screenRow >= visibleRows)
    {
        vicState.displayEnabled = false;
        return;
    }

    vicState.displayEnabled = true;

    // VIC-style row counter is authoritative.
    vicState.rc = static_cast<uint8_t>((vicState.rc + 1) & 0x07);

    // When RC wraps, advance to the next 40-column character row.
    if (vicState.rc == 0)
    {
        vicState.vcBase = static_cast<uint16_t>(vicState.vcBase + 40);
    }

    // If we just advanced past the last visible character row, close display.
    if (currentCharacterRow() >= visibleRows)
    {
        vicState.displayEnabled = false;
    }
}

int Vic::currentCharacterRow() const
{
    return static_cast<int>(vicState.vcBase / 40);
}

bool Vic::verticalDisplayOpenForRaster(int raster) const
{
    const bool den = (d011_per_raster[raster] & 0x10) != 0;
    if (!den)
        return false;

    if (!denSeenOn30 || firstBadlineY < 0)
        return false;

    return vicState.displayEnabled;
}

bool Vic::horizontalBorderLatchedAtPixel(int raster, int px) const
{
    if (px < 0 || px >= VISIBLE_WIDTH)
        return true;

    int x0, x1;
    innerWindowForRaster(raster, x0, x1);

    // Start each raster in border state, then "open" once the left inner edge
    // is reached, and "close" again at the right inner edge.
    bool borderLatched = true;

    if (px >= x0)
        borderLatched = false;

    if (px >= x1)
        borderLatched = true;

    return borderLatched;
}

void Vic::updateVerticalBorderState(int raster)
{
    vicState.verticalBorder = !verticalDisplayOpenForRaster(raster);
}

bool Vic::borderActiveAtPixel(int raster, int px) const
{
    if (px < 0 || px >= VISIBLE_WIDTH)
        return true;

    if (horizontalBorderLatchedAtPixel(raster, px))
        return true;

    return !verticalDisplayOpenForRaster(raster);
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

inline uint8_t Vic::effectiveD011ForRaster(int raster) const
{
    if (raster == registers.raster)
        return registers.control & 0x7F;   // live current-raster value
    return d011_per_raster[raster] & 0x7F; // latched for other rasters
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
