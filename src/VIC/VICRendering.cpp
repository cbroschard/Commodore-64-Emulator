// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "IVideoSink.h"
#include "Vic.h"

void Vic::runPixelOutputPhase(int firstDot, int lastDot)
{
    const int raster = registers.raster;

    // Per-raster output initialization happens before the first
    // four dots of cycle 0 are generated.
    if (currentCycle == 0 && firstDot == 0)
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

    const int baseX = cycleFramebufferX(currentCycle);

    for (int dot = firstDot; dot < lastDot; ++dot)
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
    else
    {
        pixel.color = backgroundColorForSource(pixel.source, pixel.color);
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

uint8_t Vic::backgroundColorForSource(BackgroundSource source, uint8_t fallbackColor) const
{
    switch (source)
    {
        case BackgroundSource::BG0:
            return static_cast<uint8_t>(registers.backgroundColor0 & 0x0F);

        case BackgroundSource::BG1:
            return static_cast<uint8_t>(registers.backgroundColor[0] & 0x0F);

        case BackgroundSource::BG2:
            return static_cast<uint8_t>(registers.backgroundColor[1] & 0x0F);

        case BackgroundSource::BG3:
            return static_cast<uint8_t>(registers.backgroundColor[2] & 0x0F);

        case BackgroundSource::Foreground:
        case BackgroundSource::Bitmap:
        case BackgroundSource::Border:
        case BackgroundSource::Unknown:
        default:
            return static_cast<uint8_t>(
                fallbackColor & 0x0F);
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
