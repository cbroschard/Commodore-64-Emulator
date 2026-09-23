// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Bus.h"
#include "CIA2.h"
#include "Vic.h"

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

    s.badLine = (raster == registers.raster) ? vicState.badLineLatchedAt14 : isBadLine(raster);

    s.denAtRaster = (d011_per_raster[raster] & 0x10) != 0;
    s.denSeenOn30 = denSeenOn30;

    s.liveVc = vicState.vc;
    s.liveBadLine = vicState.badLineCondition;
    s.badLineDmaStartCycle = vicState.badLineDmaStartCycle;
    s.badLineFetchIndex = vicState.badLineFetchIndex;

    s.cAccessScreenLatch = cAccessScreenLatch;
    s.cAccessColorLatch = cAccessColorLatch;
    s.cAccessLatchValid = cAccessLatchValid;
    s.cAccessLatchIndex = cAccessLatchIndex;

    // The monitor breakpoint samples after beginCycle() but before
    // the current cycle's fetch phase has completed. Therefore VMLI - 1
    // identifies the most recently completed g-access.
    const int lastGraphicsColumn = static_cast<int>(vicState.vmliFetchIndex) - 1;

    if (lastGraphicsColumn >= 0 && lastGraphicsColumn < BACKGROUND_MATRIX_COLUMNS)
    {
        const BackgroundGraphicsLatch& latch = backgroundGraphicsLatches[lastGraphicsColumn];

        s.graphicsLatchIndex = lastGraphicsColumn;
        s.graphicsLatchValid = latch.valid;

        if (latch.valid)
        {
            s.graphicsLatchAddress = latch.graphicsAddress;
            s.graphicsLatchD011 = latch.d011;
            s.graphicsLatchD016 = latch.d016;
            s.graphicsLatchD018 = latch.d018;
        }
    }

    if (s.slot.graphicsFetch)
    {
        const int fetchColumn = s.slot.graphicsFetchIndex;

        if (fetchColumn >= 0 && fetchColumn < BACKGROUND_MATRIX_COLUMNS)
        {
            const int registerSampleX = rasterEventPixelX(cycle);

            const uint8_t d016 = d016ForRasterPixelX(raster, registerSampleX, false);
            const int xScroll = static_cast<int>(d016 & 0x07);

            s.graphicsReloadColumn = fetchColumn;
            s.graphicsReloadX = cycleFramebufferX(cycle) + xScroll;
        }
    }

    s.liveVcBase = vicState.vcBase;
    s.liveVmliFetchIndex = vicState.vmliFetchIndex;
    s.liveRc = vicState.rc;
    s.liveDisplayRow = currentCharacterRow();

    s.liveRefreshCounter = vicState.refreshCounter;

    if (s.slot.refresh)
        s.refreshAddress = static_cast<uint16_t>(0x3F00 | vicState.refreshCounter);

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
        d.yExpandFlipFlop = s.yExpandFlipFlop;

        d.mc = s.mc;
        d.mcBase = s.mcBase;

        d.row = spriteRowFromMCBase(i);
        d.currentRow = s.currentRow;

        d.pointerByte = s.pointerByte;
        d.dataBase = s.dataBase;

        d.shift0 = s.shift0;
        d.shift1 = s.shift1;
        d.shift2 = s.shift2;

        d.lastFetchAddr0 = s.lastFetchAddr0;
        d.lastFetchAddr1 = s.lastFetchAddr1;
        d.lastFetchAddr2 = s.lastFetchAddr2;

        d.rowPrepared = s.rowPrepared;

        d.outputXStart = s.outputXStart;
        d.outputWidth = s.outputWidth;
        d.outputBit = s.outputBit;
        d.outputRepeat = s.outputRepeat;

        const int sampleX = std::clamp(s.outputXStart, 0, VISIBLE_WIDTH - 1);

        d.multicolorAtX = spriteMulticolorAtPixel(i, sampleX);
        d.xExpandedAtX = spriteXExpandedAtPixel(i, sampleX);
        d.enabledAtX = spriteEnabledAtPixel(i, sampleX);
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
    s.rasterIrqCompareMatched = rasterIrqCompareMatched;
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
    s.latchedDD00 = cia2 ? cia2->getCurrentVICBank() : 0;

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

    s.badLine = vicState.badLineCondition;
    s.badLineSampled = vicState.badLineLatchedAt14;

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


uint8_t Vic::vicReadForDebug(uint16_t address, int raster) const
{
    return bus ? bus->vicRead(address) : 0;
}

uint8_t Vic::vicReadColorForDebug(uint16_t address) const
{
    return bus ? bus->vicReadColor(address) : 0x0F;
}

bool Vic::isBadLineForDebug(int raster) const
{
    if (raster < 0 || raster >= getMaxRasterLinesForDebug())
        return false;

    return raster == static_cast<int>(registers.raster) ? vicState.badLineLatchedAt14 : isBadLine(raster);
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
    s.badLine = vicState.badLineCondition;
    s.badLineSampled = vicState.badLineLatchedAt14;

    s.d011 = latchedD011ForRaster(raster);
    s.d016 = latchedD016ForRaster(raster);
    s.d018 = latchedD018ForRaster(raster);
}

std::string Vic::decodeModeName() const
{
    const uint8_t d011 = effectiveD011ForRaster(registers.raster);
    const uint8_t d016 = effectiveD016ForRaster(registers.raster);

    const bool ecm = (d011 & 0x40) != 0;
    const bool bmm = (d011 & 0x20) != 0;
    const bool mcm = (d016 & 0x10) != 0;

    if (!ecm && !bmm && !mcm) return "Text";
    if (!ecm && !bmm &&  mcm) return "Multicolor Text";
    if (!ecm &&  bmm && !mcm) return "Bitmap";
    if (!ecm &&  bmm &&  mcm) return "Multicolor Bitmap";
    if ( ecm && !bmm && !mcm) return "ECM (Extended Color Mode)";
    if ( ecm && !bmm &&  mcm) return "Illegal Text";
    if ( ecm &&  bmm && !mcm) return "Illegal Bitmap";
    return "Illegal Multicolor Bitmap";
}

std::string Vic::getVICBanks() const
{
    std::stringstream out;
    out << std::hex << std::uppercase << std::setfill('0');

    const int raster = std::clamp<int>(static_cast<int>(registers.raster), 0, static_cast<int>(cfg_->maxRasterLines - 1));
    const uint16_t bankBase = cia2 ? cia2->getCurrentVICBank() : 0;

    // Representative display X for monitor reporting.
    // Actual rendering remains pixel-aware across the whole raster.
    const int samplePx = BACKGROUND_40COL_X0;

    const uint16_t charOffset = charBaseForRasterPixelX(raster, samplePx);
    const uint16_t screenOffset = screenBaseForRasterPixelX(raster, samplePx);
    const uint16_t bitmapOffset = bitmapBaseForRasterPixelX(raster, samplePx);

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

void Vic::updateMonitorCaches(int raster)
{
    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        raster = registers.raster;

    if (raster < 0 || raster >= static_cast<int>(cfg_->maxRasterLines))
        raster = 0;

    const uint16_t currentVICBank = cia2 ? cia2->getCurrentVICBank() : 0;

    // Use a representative visible display X for monitor/debug cache reporting.
    // Rendering itself remains pixel-aware through charBaseForRasterPixelX(),
    // screenBaseForRasterPixelX(), and bitmapBaseForRasterPixelX().
    const int samplePx = BACKGROUND_40COL_X0;

    charBaseCache = static_cast<uint16_t>(charBaseForRasterPixelX(raster, samplePx) + currentVICBank);

    screenBaseCache = static_cast<uint16_t>(screenBaseForRasterPixelX(raster, samplePx) + currentVICBank);

    bitmapBaseCache = static_cast<uint16_t>(bitmapBaseForRasterPixelX(raster, samplePx) + currentVICBank);
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

    if (raster < static_cast<int>(rasterPixelStates.size()) &&  rasterPixelStates[raster].valid)
    {
        snap = &rasterPixelStates[raster];
        snapSource = "current frame";
    }
    else if (raster < static_cast<int>(lastFrameRasterPixelStates.size()) && lastFrameRasterPixelStates[raster].valid)
    {
        snap = &lastFrameRasterPixelStates[raster];
        snapSource = "previous frame";
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
