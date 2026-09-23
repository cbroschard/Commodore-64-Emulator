// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Vic.h"

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

void Vic::updateHorizontalBorderStateAtPixel(int raster, int px)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return;

    if (px < 0 || px >= VISIBLE_WIDTH)
        return;

    const uint8_t d016 = d016ForRasterPixelX(raster, px, false);
    const bool csel40 = d016CSEL(d016);

    const int openX = horizontalBorderOpenCompareX(csel40);
    const int closeX = horizontalBorderCloseCompareX(csel40);

    if (vicState.horizontalBorder)
    {
        if (px == openX)
            vicState.horizontalBorder = false;
    }
    else
    {
        if (px == closeX)
            vicState.horizontalBorder = true;
    }

    borderMaskLine[px] = (vicState.verticalBorder || vicState.horizontalBorder) ? 1 : 0;
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
