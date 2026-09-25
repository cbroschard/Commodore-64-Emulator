// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Bus.h"
#include "Vic.h"

void Vic::advanceCharacterSequencerAtCycle58()
{
    // At cycle 58, RC=7 completes the current character row.
    // VC already points one entry beyond the final g-access, so it
    // becomes the base for the next matrix row.
    if (vicState.rc == 7)
    {
        vicState.vcBase = static_cast<uint16_t>(vicState.vc & 0x03FF);

        // RC=7 normally moves the video logic into idle state.
        vicState.displayEnabledNext = false;
    }

    // A late Bad Line Condition keeps/puts the video logic
    // in display state for the cycle-58 decision.
    if (vicState.displayStateHoldForCycle58)
        vicState.displayEnabledNext = true;

    // RC increments if the video logic is in display state
    // after the cycle-58 state transition has been resolved.
    if (vicState.displayEnabledNext)
        vicState.rc = static_cast<uint8_t>((vicState.rc + 1) & 0x07);

    // Commit the display state for subsequent cycles/lines.
    vicState.displayEnabled = vicState.displayEnabledNext;
}

void Vic::reloadCharacterSequencerAtCycle14(bool badLineAt14)
{
    // VIC-II cycle 14:
    // VCBASE is copied into VC and VMLI restarts for the line.
    vicState.vc = static_cast<uint16_t>(vicState.vcBase & 0x03FF);
    vicState.vmliFetchIndex = 0;

    // Only a Bad Line Condition sampled at cycle 14
    // resets the row counter.
    if (badLineAt14)
        vicState.rc = 0;
}

void Vic::advanceCharacterSequencerAfterGAccess()
{
    if (vicState.vmliFetchIndex >= BACKGROUND_MATRIX_COLUMNS)
        return;

    ++vicState.vmliFetchIndex;

    vicState.vc = static_cast<uint16_t>((vicState.vc + 1) & 0x03FF);
}

void Vic::performBadLineFetchesForCurrentCycle()
{
    if (!vicState.cAccessActive)
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

        cAccessScreenLatch = 0xFF;
        cAccessColorLatch = static_cast<uint8_t>(cpuBusValue & 0x0F);

        cAccessLatchValid = true;
        cAccessLatchIndex = fetchIndex;

        if (activeMatrixRow.valid && activeMatrixRow.vcBase == vicState.vmliBase)
        {
            // An invalid late c-access still loads the matrix latch,
            // but with the values seen on the VIC data bus.
            activeMatrixRow.screen[fetchIndex] = cAccessScreenLatch;

            activeMatrixRow.color[fetchIndex] = cAccessColorLatch;

            activeMatrixRow.fetched[fetchIndex] = 1;

            // Preserve diagnostics indicating that this was not
            // a normal successful c-access.
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

    const bool badNow = isBadLine(raster);

    const bool beforeCycle14 = currentCycle < 14;
    const bool beforeOrAtCycle14Sample = currentCycle <= 14;

    // c-access/BA sequencing only applies through cycle 54.
    if (currentCycle < 12 || currentCycle > 54)
        return;

    if (!badNow)
    {
        if (vicState.badLineCondition)
        {
            vicState.badLineCondition = false;

            // A late-created Bad Line Condition no longer needs to
            // preserve display state through cycle 58.
            vicState.displayStateHoldForCycle58 = false;

            // The cycle-14 sample occurs after this live-condition update,
            // so a condition removed during cycle 14 can still cancel the
            // pending c-access sequence.
            if (beforeOrAtCycle14Sample)
            {
                vicState.cAccessActive = false;
                vicState.badLineDmaStartCycle = -1;
                vicState.badLineFetchIndex = 0;
            }
        }

        return;
    }

    if (!vicState.badLineCondition)
    {
        vicState.badLineCondition = true;
        vicState.cAccessActive = true;

        // BA must be low for three cycles before Phi2 can be taken
        // from the CPU. A normal bad line detected by cycle 12 can
        // therefore begin c-access at the configured DMA start.
        if (beforeCycle14)
        {
            vicState.badLineDmaStartCycle = cfg_->DMAStartCycle;
        }
        else
        {
            // Late-created Bad Line Condition: takeover cannot occur
            // until three cycles after BA is asserted.
            const int takeoverCycle = currentCycle + 3;

            if (takeoverCycle <= cfg_->DMAEndCycle)
            {
                vicState.badLineDmaStartCycle = takeoverCycle;

                // Preserve display state through the cycle-58 transition.
                vicState.displayStateHoldForCycle58 = true;
            }
            else
            {
                // Too late in the raster for a valid c-access takeover.
                vicState.badLineDmaStartCycle = -1;
                vicState.cAccessActive = false;
                vicState.displayStateHoldForCycle58 = false;
            }
        }

        if (vicState.cAccessActive)
        {
            vicState.displayEnabledNext = true;
            initializeMatrixFetchStateForRaster();
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

void Vic::initializeMatrixFetchStateForRaster()
{
    if (vicState.matrixFetchInitializedThisRaster)
        return;

    vicState.matrixFetchInitializedThisRaster = true;

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

void Vic::beginBadLineFetch()
{
    // A bad line resets the row counter.
    vicState.rc = 0;

    // A valid bad line starts/continues display state.
    vicState.displayEnabled = true;
    vicState.displayEnabledNext = true;

    initializeMatrixFetchStateForRaster();
}

void Vic::fetchBadLineMatrixByte(int fetchIndex, int raster)
{
    if (fetchIndex < 0 || fetchIndex >= BACKGROUND_MATRIX_COLUMNS)
        return;

    if (!bus)
        return;

    // VC for this actual matrix fetch.
    const uint16_t vc = static_cast<uint16_t>((vicState.vmliBase + fetchIndex) & 0x03FF);

    // Use the register state that is active at this exact c-access.
    const int fetchX = rasterEventPixelX(currentCycle);

    const uint16_t screenBase = screenBaseForRasterPixelX(raster, fetchX);

    const uint16_t screenAddress = static_cast<uint16_t>(screenBase + vc);

    const uint8_t screenByte = bus->vicRead(screenAddress);

    // Color RAM is selected independently of D018.
    const uint16_t colorAddress = static_cast<uint16_t>(COLOR_MEMORY_START + vc);

    const uint8_t colorByte = static_cast<uint8_t>(bus->vicReadColor(colorAddress) & 0x0F);

    if (vicTraceOn(TraceManager::TraceDetail::VIC_BUS))
    {
        std::ostringstream out;

        out << "[VIC:CACCESS] "
            << "raster=" << raster
            << " cycle=" << currentCycle
            << " phase=" << busPhaseName(currentBusPhase)
            << " index=" << fetchIndex
            << " VC=$"
            << std::hex << std::uppercase
            << std::setw(3) << std::setfill('0')
            << vc
            << " screen=$"
            << std::setw(4)
            << screenAddress
            << " color=$"
            << std::setw(4)
            << colorAddress;

        traceVicBusEvent(out.str());
    }

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

void Vic::loadActiveStandardTextPixelStateFromLatch(int raster, int column, int px)
{
    resetActiveBackgroundPixelState();

    if (column < 0 || column >= BACKGROUND_MATRIX_COLUMNS)
        return;

    const BackgroundGraphicsLatch& latch = backgroundGraphicsLatches[column];

    if (!latch.valid)
        return;

    activeBgPixel.valid = true;

    // Retain fetch-time mode for diagnostics/debugging.
    activeBgPixel.mode = latch.mode;

    // Raw data captured by the g-access.
    activeBgPixel.rowBits = latch.graphicsByte;
    activeBgPixel.shiftRegister = latch.graphicsByte;

    activeBgPixel.screenByte = latch.screenByte;
    activeBgPixel.colorByte = latch.colorByte;

    // Color RAM bit 3 is the character's multicolor attribute.
    activeBgPixel.multicolorText =
        (latch.colorByte & 0x08) != 0;

    activeBgPixel.pxBase = px;
    activeBgPixel.nextX = px;
    activeBgPixel.py = fbY(raster);
    activeBgPixel.phase = 0;

    activeBgPixel.dotsRemaining = 8;
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

void Vic::stampBackgroundPixelSource(int px, int py, uint8_t color, bool opaque, BackgroundSource source)
{
    (void)py;

    if (px < 0 || px >= 512)
        return;

    bgColorLine[px] = color & 0x0F;
    bgOpaqueLine[px] = opaque ? 1 : 0;
    bgSourceLine[px] = source;
}

Vic::BackgroundPixel Vic::sampleAndAdvanceActiveMulticolorTextPixel()
{
    BackgroundPixel out {};

    if (!activeBgPixel.valid)
        return out;

    if (activeBgPixel.dotsRemaining == 0)
        return out;

    const uint8_t value = static_cast<uint8_t>((activeBgPixel.shiftRegister >> 6) & 0x03);

    switch (value)
    {
        case 0:
            out.color = activeBgPixel.bg0;
            out.opaque = false;
            out.source = BackgroundSource::BG0;
            break;

        case 1:
            out.color = activeBgPixel.bg1;
            out.opaque = false;
            out.source = BackgroundSource::BG1;
            break;

        case 2:
            out.color = activeBgPixel.bg2;
            out.opaque = true;
            out.source = BackgroundSource::BG2;
            break;

        case 3:
            out.color = static_cast<uint8_t>(activeBgPixel.fg & 0x07);
            out.opaque = true;
            out.source = BackgroundSource::Foreground;
            break;
    }

    ++activeBgPixel.multicolorPairPhase;

    if (activeBgPixel.multicolorPairPhase >= 2)
    {
        activeBgPixel.shiftRegister = static_cast<uint8_t>(activeBgPixel.shiftRegister << 2);
        activeBgPixel.multicolorPairPhase = 0;
    }

    if (activeBgPixel.dotsRemaining > 0)
        --activeBgPixel.dotsRemaining;

    return out;
}

Vic::BackgroundPixel Vic::sampleAndAdvanceActiveStandardTextPixel()
{
    BackgroundPixel out {};

    out.color = activeBgPixel.bg0 & 0x0F;
    out.opaque = false;
    out.source = activeBgPixel.bg0Source;

    if (!activeBgPixel.valid)
        return out;

    if (activeBgPixel.dotsRemaining == 0)
        return out;

    const bool pixelOn = (activeBgPixel.shiftRegister & 0x80) != 0;

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

    activeBgPixel.shiftRegister = static_cast<uint8_t>(activeBgPixel.shiftRegister << 1);

    if (activeBgPixel.dotsRemaining > 0)
        --activeBgPixel.dotsRemaining;

    return out;
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

bool Vic::performGAccessForCurrentCycle()
{
    if (!currentCycleSlot.graphicsFetch)
        return false;

    const int column = currentCycleSlot.graphicsFetchIndex;

    if (column < 0 || column >= BACKGROUND_MATRIX_COLUMNS)
        return false;

    const int fetchPixelX = cyclePixelX(currentCycle);
    const int outputX = cycleFramebufferX(currentCycle);
    const int registerSampleX = rasterEventPixelX(currentCycle);

    const uint8_t d011 = d011ForRasterPixelX(registers.raster, registerSampleX, false);
    const uint8_t d016 = d016ForRasterPixelX(registers.raster, registerSampleX, false);
    const uint8_t d018 = d018ForRasterPixelX(registers.raster, registerSampleX, false) & 0xFE;

    if (!vicState.displayEnabled)
    {
        performIdleFetchForCurrentCycle();
        return false;
    }

    const graphicsMode mode = graphicsModeFromRegisters(d011, d016);

    if (mode != graphicsMode::standard &&
        mode != graphicsMode::multicolor &&
        mode != graphicsMode::bitmap &&
        mode != graphicsMode::multicolorBitmap &&
        mode != graphicsMode::extendedColorText &&
        mode != graphicsMode::illegalText &&
        mode != graphicsMode::illegalBitmap &&
        mode != graphicsMode::illegalMulticolorBitmap)
    {
        return false;
    }

    traceBackgroundGraphicsFetch(registers.raster, currentCycle, column, fetchPixelX, outputX);

    switch (mode)
    {
        case graphicsMode::standard:
        case graphicsMode::multicolor:
        case graphicsMode::extendedColorText:
        case graphicsMode::illegalText:
            fetchStandardTextGraphicsByte(registers.raster, column, d011, d016, d018);
            break;

        case graphicsMode::bitmap:
        case graphicsMode::multicolorBitmap:
        case graphicsMode::illegalBitmap:
        case graphicsMode::illegalMulticolorBitmap:
            fetchStandardBitmapGraphicsByte(registers.raster, column, d011, d016, d018);
            break;

        default:
            return false;
    }

    const BackgroundGraphicsLatch& latch = backgroundGraphicsLatches[column];

    if (latch.valid)
    {
        // A background reload belongs to exactly one g-access.
        // If an older one is still pending here, its reload window
        // should already have been consumed or expired by dot output.
        if (!pendingBgReload.valid)
        {
            pendingBgReload.valid = true;
            pendingBgReload.column = column;
            pendingBgReload.baseX = cycleFramebufferX(currentCycle);
        }
    }

    // We reached a valid graphics-access slot while the
    // VIC was in display state. The hardware g-access occurred
    // even if the renderer could not populate its software latch.
    return true;
}

void Vic::resetBackgroundGraphicsLatches()
{
    for (auto& latch : backgroundGraphicsLatches)
        latch = {};
}

void Vic::fetchStandardTextGraphicsByte(int raster, int column, uint8_t d011, uint8_t d016, uint8_t d018)
{
    if (column < 0 || column >= BACKGROUND_MATRIX_COLUMNS)
        return;

    BackgroundGraphicsLatch& latch = backgroundGraphicsLatches[column];
    latch = {};
    latch.column = column;

    uint8_t screenByte = 0;
    uint8_t colorByte = 0;

    const bool useCAccessLatch = vicState.cAccessActive && cAccessLatchValid && cAccessLatchIndex == column;

    if (useCAccessLatch)
    {
        screenByte = cAccessScreenLatch;
        colorByte = static_cast<uint8_t>(cAccessColorLatch & 0x0F);
    }
    else
    {
        if (!fetchedMatrixBytesForDisplayCol(column, raster, screenByte, colorByte))
        {
            return;
        }
    }

    const graphicsMode mode = graphicsModeFromRegisters(d011, d016);

    uint8_t charIndex = screenByte;

    if (mode == graphicsMode::extendedColorText || mode == graphicsMode::illegalText)
        charIndex &= 0x3F;

    const uint16_t charBase = static_cast<uint16_t>(((d018 >> 1) & 0x07) * 0x0800);
    const uint16_t charAddr = static_cast<uint16_t>(charBase + static_cast<uint16_t>(charIndex) * 8
                                + static_cast<uint16_t>(vicState.rc & 0x07));
    const uint8_t graphicsByte = bus ? bus->vicRead(charAddr) : 0x00;

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

void Vic::fetchStandardBitmapGraphicsByte(int raster, int column, uint8_t d011, uint8_t d016, uint8_t d018)
{
    if (column < 0 || column >= BACKGROUND_MATRIX_COLUMNS)
        return;

    BackgroundGraphicsLatch& latch = backgroundGraphicsLatches[column];
    latch = {};
    latch.column = column;

    uint8_t screenByte = 0;
    uint8_t colorByte = 0;

    const bool useCAccessLatch = vicState.cAccessActive && cAccessLatchValid && cAccessLatchIndex == column;

    if (useCAccessLatch)
    {
        screenByte = cAccessScreenLatch;
        colorByte = static_cast<uint8_t>(cAccessColorLatch & 0x0F);
    }
    else
    {
        if (!fetchedMatrixBytesForDisplayCol(column, raster, screenByte, colorByte))
        {
            return;
        }
    }

    const graphicsMode mode = graphicsModeFromRegisters(d011, d016);

    const uint16_t bitmapBase = static_cast<uint16_t>(((d018 >> 3) & 0x01) * 0x2000);
    const uint16_t vc = static_cast<uint16_t>(vicState.vc & 0x03FF);

    const uint8_t rc = static_cast<uint8_t>(vicState.rc & 0x07);

    uint16_t bitmapAddress = static_cast<uint16_t>(bitmapBase + ((vc & 0x03FF) << 3) + rc);

    if (mode == graphicsMode::illegalBitmap || mode == graphicsMode::illegalMulticolorBitmap)
    {
        // ECM forces VIC g-access address lines A9 and A10 low.
        bitmapAddress &= static_cast<uint16_t>(~0x0600);
    }

    const uint8_t graphicsByte = bus ? bus->vicRead(bitmapAddress) : 0x00;

    updateOpenBus(graphicsByte);

    latch.valid = true;

    latch.screenByte = screenByte;
    latch.colorByte = static_cast<uint8_t>(colorByte & 0x0F);

    latch.graphicsByte = graphicsByte;
    latch.graphicsAddress = bitmapAddress;

    latch.d011 = d011;
    latch.d016 = d016;
    latch.d018 = d018;
    latch.mode = mode;
}

void Vic::resetActiveBackgroundPixelState()
{
    activeBgPixel.valid = false;
    activeBgPixel.multicolorText = false;

    activeBgPixel.rowBits = 0;
    activeBgPixel.shiftRegister = 0;

    activeBgPixel.screenByte = 0;
    activeBgPixel.colorByte = 0;

    activeBgPixel.fg = 0;
    activeBgPixel.bg0 = 0;
    activeBgPixel.bg1 = 0;
    activeBgPixel.bg2 = 0;

    activeBgPixel.bg0Source = BackgroundSource::BG0;

    activeBgPixel.pxBase = 0;
    activeBgPixel.py = 0;
    activeBgPixel.nextX = 0;
    activeBgPixel.phase = 0;

    activeBgPixel.dotsRemaining = 0;
    activeBgPixel.multicolorPairPhase = 0;
}

void Vic::loadActiveStandardBitmapPixelStateFromLatch(int raster, int column, int px)
{
    resetActiveBackgroundPixelState();

    if (column < 0 || column >= BACKGROUND_MATRIX_COLUMNS)
        return;

    const BackgroundGraphicsLatch& latch = backgroundGraphicsLatches[column];

    if (!latch.valid)
        return;

    activeBgPixel.valid = true;

    // Retain fetch-time mode for diagnostics/debugging.
    activeBgPixel.mode = latch.mode;

    activeBgPixel.multicolorText = false;

    // Raw data captured by the g-access.
    activeBgPixel.rowBits = latch.graphicsByte;
    activeBgPixel.shiftRegister = latch.graphicsByte;

    activeBgPixel.screenByte = latch.screenByte;
    activeBgPixel.colorByte = latch.colorByte;

    activeBgPixel.pxBase = px;
    activeBgPixel.nextX = px;
    activeBgPixel.py = fbY(raster);
    activeBgPixel.phase = 0;

    activeBgPixel.dotsRemaining = 8;
}

Vic::BackgroundPixel Vic::sampleAndAdvanceActiveStandardBitmapPixel()
{
    BackgroundPixel out {};

    if (!activeBgPixel.valid)
        return out;

    if (activeBgPixel.dotsRemaining == 0)
        return out;

    const bool pixelOn = (activeBgPixel.shiftRegister & 0x80) != 0;

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
    activeBgPixel.shiftRegister = static_cast<uint8_t>(activeBgPixel.shiftRegister << 1);

    if (activeBgPixel.dotsRemaining > 0)
        --activeBgPixel.dotsRemaining;

    return out;
}

Vic::BackgroundPixel Vic::sampleAndAdvanceActiveMulticolorBitmapPixel()
{
    BackgroundPixel out {};

    if (!activeBgPixel.valid)
        return out;

    if (activeBgPixel.dotsRemaining == 0)
        return out;

    const uint8_t value = static_cast<uint8_t>((activeBgPixel.shiftRegister >> 6) & 0x03);

    switch (value)
    {
        case 0:
            out.color = static_cast<uint8_t>(activeBgPixel.bg0 & 0x0F);
            out.opaque = false;
            out.source = BackgroundSource::BG0;
            break;

        case 1:
            out.color = static_cast<uint8_t>(activeBgPixel.fg & 0x0F);
            out.opaque = false;
            out.source = BackgroundSource::Bitmap;
            break;

        case 2:
            out.color = static_cast<uint8_t>(activeBgPixel.bg1 & 0x0F);
            out.opaque = true;
            out.source = BackgroundSource::Bitmap;
            break;

        case 3:
            out.color = static_cast<uint8_t>(activeBgPixel.bg2 & 0x0F);
            out.opaque = true;
            out.source = BackgroundSource::Bitmap;
            break;
    }

    ++activeBgPixel.multicolorPairPhase;

    if (activeBgPixel.multicolorPairPhase >= 2)
    {
        activeBgPixel.shiftRegister = static_cast<uint8_t>(activeBgPixel.shiftRegister << 2);
        activeBgPixel.multicolorPairPhase = 0;
    }

    if (activeBgPixel.dotsRemaining > 0)
        --activeBgPixel.dotsRemaining;

    return out;
}

Vic::graphicsMode Vic::graphicsModeFromRegisters(uint8_t d011, uint8_t d016) const
{
    const bool ecm = (d011 & 0x40) != 0;
    const bool bmm = (d011 & 0x20) != 0;
    const bool mcm = d016MCM(d016);

    if (!ecm && !bmm && !mcm)
        return graphicsMode::standard;

    if (!ecm && !bmm && mcm)
        return graphicsMode::multicolor;

    if (!ecm && bmm && !mcm)
        return graphicsMode::bitmap;

    if (!ecm && bmm && mcm)
        return graphicsMode::multicolorBitmap;

    if (ecm && !bmm && !mcm)
        return graphicsMode::extendedColorText;

    if (ecm && !bmm && mcm)
        return graphicsMode::illegalText;

    if (ecm && bmm && !mcm)
        return graphicsMode::illegalBitmap;

    return graphicsMode::illegalMulticolorBitmap;
}

Vic::graphicsMode Vic::graphicsModeForRaster(int raster) const
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return currentMode;

    const uint8_t d011 = latchedD011ForRaster(raster);
    const uint8_t d016 = latchedD016ForRaster(raster);

    return graphicsModeFromRegisters(d011, d016);
}

bool Vic::isIllegalGraphicsMode(graphicsMode mode) const
{
    return mode == graphicsMode::illegalText ||
           mode == graphicsMode::illegalBitmap ||
           mode == graphicsMode::illegalMulticolorBitmap;
}

void Vic::updateGraphicsMode(int raster)
{
    currentMode = graphicsModeForRaster(raster);
}

uint8_t Vic::fetchColorByte(int row, int col, int raster) const
{
    if (!bus)
        return 0x00;

    (void)raster;

    row = std::clamp(row, 0, 24);
    col = std::clamp(col, 0, BACKGROUND_MATRIX_COLUMNS - 1);

    const uint16_t address =
        static_cast<uint16_t>(
            COLOR_MEMORY_START +
            static_cast<uint16_t>(row * BACKGROUND_MATRIX_COLUMNS + col)
        );

    return bus->vicReadColor(address);
}

int Vic::currentDisplayRowBase() const
{
    // When display is active, use the row latched at bad-line start.
    if (vicState.displayEnabled)
        return static_cast<int>(vicState.vmliBase);

    return static_cast<int>(vicState.vcBase);
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

uint8_t Vic::d011ForRasterPixelX(int raster, int px, bool preferPreviousFrame) const
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        return registers.control & 0x7F;

    uint8_t active = latchedD011ForRaster(raster) & 0x7F;

    const auto& eventTable = preferPreviousFrame ? lastFrameRasterEventsByRaster : rasterEventsByRaster;

    if (raster >= static_cast<int>(eventTable.size()))
        return active;

    const auto& events = eventTable[raster];

    for (const RasterEventRecord& e : events)
    {
        if (e.kind != RasterEventKind::Control)
            continue;

        const int eventX = rasterRegisterEventPixelX(e);

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

    const auto& eventTable = preferPreviousFrame ? lastFrameRasterEventsByRaster : rasterEventsByRaster;

    if (raster >= static_cast<int>(eventTable.size()))
        return active;

    const auto& events = eventTable[raster];

    for (const RasterEventRecord& e : events)
    {
        if (e.kind != RasterEventKind::Control2)
            continue;

        const int eventX = rasterRegisterEventPixelX(e);

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

    const auto& eventTable = preferPreviousFrame ? lastFrameRasterEventsByRaster : rasterEventsByRaster;

    if (raster >= static_cast<int>(eventTable.size()))
        return active;

    const auto& events = eventTable[raster];

    for (const RasterEventRecord& e : events)
    {
        if (e.kind != RasterEventKind::MemoryPointer)
            continue;

        const int eventX = rasterRegisterEventPixelX(e);

        if (px >= eventX)
            active = e.newValue & 0xFE;
    }

    return active;
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
