// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "IVideoSink.h"
#include "Vic.h"

void Vic::runPixelOutputPhase()
{
    const int raster = registers.raster;

    if (currentCycle == 0)
    {
        dotColorLine.fill(registers.borderColor & 0x0F);

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

    const int baseX =
        cycleFramebufferX(currentCycle);

    for (int dot = 0; dot < 8; ++dot)
    {
        const int x = baseX + dot;

        if (x < 0 || x >= VISIBLE_WIDTH)
            continue;

        outputDot(raster, dot, x);
    }
}

uint8_t Vic::compositeDot(int raster, int x, const BackgroundPixel& bgPixel, const std::array<SpritePixel, 8>& spritePixels) const
{
    // VIC-II border is in front of both background graphics and sprites.
    if (borderActiveAtPixel(raster, x))
        return registers.borderColor & 0x0F;

    // Lower-numbered sprites have priority over higher-numbered sprites.
    for (int spr = 0; spr < 8; ++spr)
    {
        const SpritePixel& spritePixel = spritePixels[spr];

        if (!spritePixel.opaque)
            continue;

        const bool behind = spriteBehindBackgroundAtPixel(spr, x);

        if (behind && bgPixel.opaque)
            return bgPixel.color & 0x0F;

        return spritePixel.color & 0x0F;
    }

    return bgPixel.color & 0x0F;
}

void Vic::outputDot(int raster, int dot, int x)
{
    (void)dot;

    updateVerticalBorderStateAtLeftCompare(raster, x);
    updateHorizontalBorderStateAtPixel(raster, x);

    const BackgroundPixel bgPixel = outputPixel(raster, x);
    const std::array<SpritePixel, 8> spritePixels = stepSpriteSequencersAtX(raster, x);
    const uint8_t color = compositeDot(raster, x, bgPixel, spritePixels);

    dotColorLine[x] = static_cast<uint8_t>(color & 0x0F);
}

Vic::BackgroundPixel Vic::outputPixel(int raster, int x)
{
    BackgroundPixel pixel {};

    if (raster < 0 || raster >= static_cast<int>(rasterPixelStates.size()))
        return pixel;

    if (x < 0 || x >= VISIBLE_WIDTH)
        return pixel;

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
        return pixel;

    const int expectedX = activeBgPixel.pxBase + activeBgPixel.phase;

    if (x != expectedX)
    {
        resetActiveBackgroundPixelState();
        return pixel;
    }

    const graphicsMode outputMode = activeBgPixel.mode;

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
    {
        pixel.color = 0x00;
    }

    // Keep the existing line-buffer renderer working for now.
    stampBackgroundPixelSource(x, activeBgPixel.py, pixel.color, pixel.opaque,  pixel.source);

    if (activeBgPixel.phase >= 8)
        activeBgPixel.valid = false;

    return pixel;
}

void Vic::emitDotRasterLine(int raster)
{
    if (!sink)
        return;

    const int screenY = fbY(raster);

    const int xStart = rasterVisibleStartX(raster);
    const int xEnd   = rasterVisibleEndX(raster);

    for (int px = xStart; px < xEnd; ++px)
        sink->setPixel(px, screenY, static_cast<uint8_t>(dotColorLine[px] & 0x0F));
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
