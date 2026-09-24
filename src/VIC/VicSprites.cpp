// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Bus.h"
#include "Vic.h"

void Vic::updateSpriteYExpansionFlipFlops()
{
    const int raster = registers.raster;

    const int px = std::clamp(cfg_->hardware_X + (currentCycle * 8) + 4, 0, VISIBLE_WIDTH);

    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const bool yExpanded = spriteYExpandedAtPixel(sprite, raster, px);

        if (!yExpanded)
            spriteUnits[sprite].yExpandFlipFlop = true;
        else
            spriteUnits[sprite].yExpandFlipFlop = !spriteUnits[sprite].yExpandFlipFlop;
    }
}

void Vic::advanceSpriteMCBaseFirstStep()
{
    for (int sprite = 0; sprite < 8; ++sprite)
    {
        SpriteUnit& unit = spriteUnits[sprite];

        if (!unit.dmaActive)
            continue;

        if (!unit.yExpandFlipFlop)
            continue;

        unit.mcBase = static_cast<uint8_t>((unit.mcBase + 2) & 0x3F);
    }
}

void Vic::advanceSpriteMCBaseSecondStep()
{
    for (int sprite = 0; sprite < 8; ++sprite)
    {
        SpriteUnit& unit = spriteUnits[sprite];

        if (!unit.dmaActive)
            continue;

        if (unit.yCrunchPending)
        {
            const uint8_t mc = static_cast<uint8_t>(unit.mc & 0x3F);

            const uint8_t mcBase = static_cast<uint8_t>(unit.mcBase & 0x3F);
            unit.mcBase = static_cast<uint8_t>((((mc | mcBase) & 0x15) |((mc & mcBase) & 0x2A)) & 0x3F);

            unit.yCrunchPending = false;
        }
        else if (unit.yExpandFlipFlop)
            unit.mcBase = static_cast<uint8_t>((unit.mcBase + 1) & 0x3F);

        unit.mc = unit.mcBase;
        unit.currentRow = spriteRowFromMCBase(sprite);

        if (unit.mcBase == 63)
        {
            traceVicSpriteSlotEvent(sprite, "dma-stop", registers.raster, currentCycle);

            // Sprite DMA is complete, but the already-prepared output
            // sequencer must be allowed to finish the current raster.
            unit.dmaActive = false;
            unit.yCrunchPending = false;
        }
    }
}

bool Vic::isSpriteDMAFetchCycle(int sprite, int cycle) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    const auto& timing = cfg_->spriteFetchTiming[sprite];

    return cycle == timing.data0Cycle || cycle == timing.data1Cycle || cycle == timing.data2Cycle;
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

            const int eventX = rasterSpriteModeEventPixelX(e);

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

            const int eventX = rasterSpriteXExpansionEventPixelX(e);

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

            const int eventX = rasterSpriteEnableEventPixelX(e);

            if (eventX > px)
                continue;

            activeEnable = e.newValue;
        }
    }

    return (activeEnable & static_cast<uint8_t>(1u << sprite)) != 0;
}

void Vic::fetchSpritePointer(int sprite, int raster)
{
    if (!bus)
        return;

    const uint16_t ptrLoc = spritePointerAddressForRaster(sprite, raster, currentCycle);
    const uint8_t ptr = bus->vicRead(ptrLoc);

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

        // outputBit is always a bit position in the 24-bit sprite
        // shift register. Multicolor consumes two bits at a time.
        spriteUnits[sprIndex].outputBit += multClr ? 2 : 1;
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

        // This sequencer identifies opacity and the sprite color source.
        // The dot renderer resolves the live color from the source, while
        // the legacy line renderer assigns the color during event replay.
        outColor = 0;
        opaque = true;
        outSource = SpriteColorSource::SpriteOwnColor;
        return true;
    }

    const int srcBit = spriteUnits[sprIndex].outputBit;

    if (srcBit < 0 || srcBit > 22)
        return false;

    const int shift = 22 - srcBit;

    const uint8_t bits = static_cast<uint8_t>((rowBits >> shift) & 0x03);

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

    // This sequencer identifies opacity and the sprite color source.
    // The dot renderer resolves the live color from the source, while
    // the legacy line renderer assigns the color during event replay.
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

std::array<Vic::SpritePixel, 8> Vic::stepSpriteSequencersAtX(int raster, int px)
{
    std::array<SpritePixel, 8> pixels {};

    if (px < 0 || px >= VISIBLE_WIDTH)
        return pixels;

    for (int spr = 0; spr < 8; ++spr)
    {
        SpriteUnit& u = spriteUnits[spr];

        if (!u.rowPrepared)
            continue;

        if (px < u.outputXStart)
            continue;

        // The sprite is finished once all 24 source bits have been consumed.
        // X expansion affects how many output pixels each source bit occupies,
        // so a fixed outputWidth cannot correctly handle mid-sprite D01D writes.
        if (u.outputBit >= 24)
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
            // Dot-level sprite result.
            const uint8_t dotColor = spriteColorForSource(spr, source);

            pixels[spr].opaque = true;
            pixels[spr].color = dotColor;
            pixels[spr].source = source;

            // Keep the legacy line buffers populated for now.
            spriteOpaqueLine[spr][px] = 1;

            spriteColorLine[spr][px] = static_cast<uint8_t>(color & 0x0F);

            spriteColorSourceLine[spr][px] = source;

            // Keep existing sprite/background collision behavior.
            if (bgOpaqueLine[px])
            {
                const uint8_t bit = static_cast<uint8_t>(1u << spr);

                latchSpriteBackgroundCollision(bit, raster, px);
            }

            // Keep existing sprite/sprite collision behavior.
            for (int other = 0; other < spr; ++other)
            {
                if (!spriteOpaqueLine[other][px])
                    continue;

                const uint8_t bits = static_cast<uint8_t>((1u << spr) | (1u << other));

                latchSpriteSpriteCollision(bits, raster, px);
            }
        }

        advanceSpriteOutputState(spr, px);
    }

    return pixels;
}

uint8_t Vic::spriteColorForSource(int sprite, SpriteColorSource source) const
{
    switch (source)
    {
        case SpriteColorSource::SpriteOwnColor:
            if (sprite < 0 || sprite >= 8)
                return 0;

            return static_cast<uint8_t>(registers.spriteColors[sprite] & 0x0F);

        case SpriteColorSource::SpriteMultiColor1:
            return static_cast<uint8_t>(registers.spriteMultiColor1 & 0x0F);

        case SpriteColorSource::SpriteMultiColor2:
            return static_cast<uint8_t>(registers.spriteMultiColor2 & 0x0F);

        case SpriteColorSource::None:
        default:
            return 0;
    }
}

void Vic::updateSpriteDMAEndOfLine(int raster)
{
    for (int s = 0; s < 8; ++s)
    {
        if (!spriteUnits[s].dmaActive)
            continue;

        traceVicSpriteSlotEvent(s, "eol", raster, currentCycle);
    }
}

int Vic::spriteRowFromMCBase(int spr) const
{
    return spriteUnits[spr].mcBase / 3;
}

void Vic::resetSpriteDMAState(int spr)
{
    spriteUnits[spr].dmaActive = false;
    spriteUnits[spr].yExpandFlipFlop = true;
    spriteUnits[spr].yCrunchPending = false;

    spriteUnits[spr].currentRow = 0;
    spriteUnits[spr].mc = 0;
    spriteUnits[spr].mcBase = 0;
    spriteUnits[spr].startY = 0;

    resetSpriteLineOutputState(spr);
    clearSpriteFetchedRowState(spr);
}

void Vic::performSpriteDataFetchForSprite(int sprite, int byteIndex)
{
    if (sprite < 0 || sprite >= 8)
        return;

    if (!spriteUnits[sprite].dmaActive)
        return;

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
    if (!bus)
        return;

    SpriteUnit& unit = spriteUnits[sprite];
    const uint8_t mc = static_cast<uint8_t>(unit.mc & 0x3F);
    const uint16_t addr = static_cast<uint16_t>(unit.dataBase + mc);

    if (byteIndex == 0)
        unit.lastFetchAddr0 = addr;
    else if (byteIndex == 1)
        unit.lastFetchAddr1 = addr;
    else if (byteIndex == 2)
        unit.lastFetchAddr2 = addr;

    const uint8_t value = bus->vicRead(addr);

    updateOpenBus(value);

    traceVicSpriteDataFetch(sprite, raster, byteIndex, addr, value);

    if (byteIndex == 0)
        unit.fetched0 = value;
    else if (byteIndex == 1)
    {
        unit.fetched1 = value;
    }
    else if (byteIndex == 2)
    {
        unit.fetched2 = value;
        latchSpriteShiftersFromFetchedBytes(sprite);
    }

    unit.mc = static_cast<uint8_t>((unit.mc + 1) & 0x3F);

    traceVicSpriteSlotEvent(sprite, "data", raster, currentCycle, byteIndex);
}

void Vic::latchSpriteShiftersFromFetchedBytes(int sprite)
{
    spriteUnits[sprite].shift0 = spriteUnits[sprite].fetched0;
    spriteUnits[sprite].shift1 = spriteUnits[sprite].fetched1;
    spriteUnits[sprite].shift2 = spriteUnits[sprite].fetched2;
    spriteUnits[sprite].rowDataLatched = true;

    const int spriteX = spriteScreenXFor(sprite, registers.raster);
    const int currentX = cycleFramebufferX(currentCycle);

    if (!spriteUnits[sprite].rowPrepared && currentX < spriteX)
        beginSpriteLineOutput(sprite, registers.raster);

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
        const bool rasterMatches    = static_cast<uint8_t>(raster & 0xFF) == registers.spriteY[sprite];
        const bool alreadyActive    = spriteUnits[sprite].dmaActive;
        const bool shouldStart      = enabled && rasterMatches && !alreadyActive;

        traceVicSpriteStartCheck(sprite, raster, registers.spriteY[sprite], enabled, yExpanded, rasterMatches, shouldStart);

        if (!shouldStart)
            continue;

        SpriteUnit& unit = spriteUnits[sprite];

        unit.dmaActive = true;
        // When sprite DMA begins:
        // normal-height sprite -> expansion flip-flop is set
        // Y-expanded sprite   -> expansion flip-flop starts cleared
        unit.yExpandFlipFlop = !yExpanded;
        unit.yCrunchPending = false;

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
void Vic::recordRasterPriorityWrite(uint8_t oldValue, uint8_t newValue)
{
    RasterPriorityEvent e;
    e.raster = registers.raster;
    e.cycle = currentCycle;
    e.phase = VicBusPhase::Phi2;
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
    e.phase = VicBusPhase::Phi2;

    e.oldValue = oldValue;
    e.newValue = newValue;

    rasterSpriteModeEvents.push_back(e);

    recordRasterEventLog(RasterEventKind::SpriteMode, 0xD01C, oldValue, newValue);
}

void Vic::recordRasterSpriteXExpansionWrite(uint8_t oldValue, uint8_t newValue)
{
    RasterSpriteXExpansionEvent e;
    e.raster = registers.raster;
    e.phase = VicBusPhase::Phi2;

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
    e.phase = VicBusPhase::Phi2;

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

            const int eventX = rasterPriorityEventPixelX(e);

            if (eventX > px)
                continue;

            activePriority = e.newValue;
        }
    }

    return (activePriority & static_cast<uint8_t>(1u << sprite)) != 0;
}

Vic::SpriteFetchPhase Vic::spriteFetchPhaseForCycle(int sprite, int cycle, VicBusPhase busPhase) const
{
    if (sprite < 0 || sprite >= 8)
        return SpriteFetchPhase::None;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return SpriteFetchPhase::None;

    const auto& timing = cfg_->spriteFetchTiming[sprite];

    if (cycle == timing.pointerCycle && busPhase == timing.pointerPhase)
        return SpriteFetchPhase::Pointer;

    if (cycle == timing.data0Cycle && busPhase == timing.data0Phase)
        return SpriteFetchPhase::Data0;

    if (cycle == timing.data1Cycle && busPhase == timing.data1Phase)
        return SpriteFetchPhase::Data1;

    if (cycle == timing.data2Cycle && busPhase == timing.data2Phase)
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
    if (sprite < 0 || sprite >= 8)
        return -1;

    const auto& timing = cfg_->spriteFetchTiming[sprite];

    struct FetchEntry
    {
        SpriteFetchPhase phase;
        int cycle;
        VicBusPhase busPhase;
    };

    const std::array<FetchEntry, 4> fetches =
    {{
        { SpriteFetchPhase::Pointer, timing.pointerCycle, timing.pointerPhase },
        { SpriteFetchPhase::Data0,   timing.data0Cycle,   timing.data0Phase },
        { SpriteFetchPhase::Data1,   timing.data1Cycle,   timing.data1Phase },
        { SpriteFetchPhase::Data2,   timing.data2Cycle,   timing.data2Phase }
    }};

    for (const auto& fetch : fetches)
    {
        if (fetch.busPhase != VicBusPhase::Phi2)
            continue;

        if (spriteFetchPhaseStealsCpu(fetch.phase))
            return fetch.cycle;
    }

    return -1;
}

int Vic::rasterPriorityEventPixelX(const RasterPriorityEvent& e) const
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

int Vic::rasterSpriteModeEventPixelX(const RasterSpriteModeEvent& e) const
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

int Vic::rasterSpriteXExpansionEventPixelX(const RasterSpriteXExpansionEvent& e) const
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

int Vic::rasterSpriteEnableEventPixelX(const RasterSpriteEnableEvent& e) const
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

uint8_t Vic::spriteYExpansionForRasterPixelX(int raster, int px, bool preferPreviousFrame) const
{
    uint8_t value = registers.spriteYExpansion;

    const auto& eventsByRaster = preferPreviousFrame ? lastFrameRasterEventsByRaster : rasterEventsByRaster;

    if (raster < 0 || raster >= static_cast<int>(eventsByRaster.size()))
        return value;

    const auto& events = eventsByRaster[raster];

    for (const auto& e : events)
    {
        if (e.kind != RasterEventKind::SpriteYExpansion)
            continue;

        const int eventX = rasterRegisterEventPixelX(e);

        if (eventX > px)
            break;

        value = e.newValue;
    }

    return value;
}

bool Vic::spriteYExpandedAtPixel(int sprite, int raster, int px) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    const uint8_t d017 = spriteYExpansionForRasterPixelX(raster, px, false);

    return (d017 & (1u << sprite)) != 0;
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

    const int samplePx = 0;

    const int x = spriteRegisterXForRasterPixel(sprIndex, raster, samplePx);

    return (x - cfg_->hardware_X) + HORIZONTAL_BORDER_SIZE - 1;
}

bool Vic::spriteDisplayCoversRaster(int sprIndex, int raster, int& rowInSprite, int& fbLine) const
{
    rowInSprite = 0;
    fbLine = fbY(raster);

    if (sprIndex < 0 || sprIndex >= 8)
        return false;

    if (!spriteUnits[sprIndex].dmaActive)
        return false;

    const int currentRow = spriteUnits[sprIndex].currentRow;

    if (currentRow < 0 || currentRow >= 21)
        return false;

    rowInSprite = currentRow;

    return true;
}

void Vic::latchSpriteSpriteCollision(uint8_t bits, int raster, int firstX)
{
    bits &= 0xFF;
    if (bits == 0)
        return;

    const uint8_t old = registers.spriteCollision;

    registers.spriteCollision = static_cast<uint8_t>(registers.spriteCollision | bits);

    const uint8_t newlySet = static_cast<uint8_t>(registers.spriteCollision & ~old);

    if (newlySet == 0)
        return;

    lastSpriteSpriteCollision.valid = true;
    lastSpriteSpriteCollision.raster = raster;
    lastSpriteSpriteCollision.x = firstX;
    lastSpriteSpriteCollision.cycle = rasterPixelToCycle(firstX);
    lastSpriteSpriteCollision.bits = newlySet;

    raiseVicIRQSource(0x04);
}

void Vic::latchSpriteBackgroundCollision(uint8_t bits, int raster, int firstX)
{
    bits &= 0xFF;
    if (bits == 0)
        return;

    const uint8_t old = registers.spriteDataCollision;

    registers.spriteDataCollision = static_cast<uint8_t>(registers.spriteDataCollision | bits);

    const uint8_t newlySet = static_cast<uint8_t>(registers.spriteDataCollision & ~old);

    if (newlySet == 0)
        return;

    lastSpriteBackgroundCollision.valid = true;
    lastSpriteBackgroundCollision.raster = raster;
    lastSpriteBackgroundCollision.x = firstX;
    lastSpriteBackgroundCollision.cycle = rasterPixelToCycle(firstX);
    lastSpriteBackgroundCollision.bits = newlySet;

    raiseVicIRQSource(0x02);
}

bool Vic::spriteDataFetchUsesPhi2(int byteIndex) const
{
    switch (byteIndex)
    {
        case 0:
            return (cfg_->spriteCpuStealPhaseMask & SPRITE_STEAL_DATA0) != 0;

        case 1:
            return (cfg_->spriteCpuStealPhaseMask & SPRITE_STEAL_DATA1) != 0;

        case 2:
            return (cfg_->spriteCpuStealPhaseMask & SPRITE_STEAL_DATA2) != 0;

        default:
            return false;
    }
}

VicBusPhase Vic::spriteBusPhaseForFetch(int sprite, SpriteFetchPhase phase) const
{
    if (sprite < 0 || sprite >= 8)
        return VicBusPhase::Phi2;

    const auto& timing = cfg_->spriteFetchTiming[sprite];

    switch (phase)
    {
        case SpriteFetchPhase::Pointer:
            return timing.pointerPhase;

        case SpriteFetchPhase::Data0:
            return timing.data0Phase;

        case SpriteFetchPhase::Data1:
            return timing.data1Phase;

        case SpriteFetchPhase::Data2:
            return timing.data2Phase;

        case SpriteFetchPhase::None:
        default:
            return VicBusPhase::Phi2;
    }
}

int Vic::spriteDataByteForCyclePhase(int sprite, int cycle, VicBusPhase busPhase) const
{
    if (sprite < 0 || sprite >= 8)
        return -1;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return -1;

    const auto& timing = cfg_->spriteFetchTiming[sprite];

    if (cycle == timing.data0Cycle && busPhase == timing.data0Phase)
        return 0;

    if (cycle == timing.data1Cycle && busPhase == timing.data1Phase)
        return 1;

    if (cycle == timing.data2Cycle && busPhase == timing.data2Phase)
        return 2;

    return -1;
}
