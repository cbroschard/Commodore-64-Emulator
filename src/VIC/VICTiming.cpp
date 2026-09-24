// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "IVideoSink.h"
#include "Vic.h"

void Vic::beginCycle()
{
    currentBusPhase = VicBusPhase::Phi1;

    beginFrameIfNeeded();

    currentCycleSlot = cycleSlotFor(registers.raster, currentCycle);

    runCycleDecisionPhase();

    const uint8_t startedMask = currentCycleSlot.spriteDmaStartMask;

    currentCycleSlot = cycleSlotFor(registers.raster, currentCycle);
    currentCycleSlot.spriteDmaStartMask = startedMask;

    updateBusArbitration();
}

void Vic::endCycle()
{
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

        lastFrameRasterEventsByRaster = rasterEventsByRaster;

        // Preserve completed-frame diagnostics before clearing current-frame state.
        lastFrameRasterRowStates = rasterRowStates;
        lastFrameRasterPixelStates = rasterPixelStates;

        rasterEventLog.clear();

        for (auto& events : rasterEventsByRaster)
            events.clear();

        for (auto& s : rasterRowStates)
            s = {};

        for (auto& s : rasterPixelStates)
            s = {};

        rasterPriorityEvents.clear();
        rasterSpriteModeEvents.clear();
        rasterSpriteXExpansionEvents.clear();
        rasterSpriteXEvents.clear();

        firstBadlineY = -1;
        denSeenOn30 = false;

        vicState.vc = 0;
        vicState.vcBase = 0;
        vicState.vmliBase = 0;
        vicState.vmliFetchIndex = 0;
        vicState.rc = 0;

        vicState.refreshCounter = 0xFF;

        vicState.badLineCondition = false;
        vicState.badLineLatchedAt14 = false;
        vicState.cAccessActive = false;
        vicState.badLineDmaStartCycle = -1;
        vicState.badLineFetchIndex = 0;
        vicState.matrixFetchInitializedThisRaster = false;

        vicState.displayEnabled = false;
        vicState.displayEnabledNext = false;
        vicState.displayStateHoldForCycle58 = false;

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
        evaluateRasterIRQCompare("normal-compare");

    if (slot.latchRasterState)
        handleCycle0Decisions();

    updateLiveBadLineCondition();

    if (slot.sampleBadline)
        handleCycle14Decisions();

    if (currentCycle == cfg_->spriteYExpansionToggleCycle)
        updateSpriteYExpansionFlipFlops();

    if (currentCycle == cfg_->spriteMcBaseAdvanceCycle1)
        advanceSpriteMCBaseFirstStep();

    if (currentCycle == cfg_->DMAStartCycle)
         handleBadLineFetchStartDecisions();

    if (slot.startSpriteDmaCheck)
        currentCycleSlot.spriteDmaStartMask = updateSpriteDMAStartForCurrentLine(registers.raster);

    if (slot.transferDisplayState)
        handleCycle58Decisions();

    if (slot.startBadlineFetch)
        handleDmaStartCycleDecisions();
}

void Vic::handleCycle0Decisions()
{
    const int raster = registers.raster;

    if (raster == 0)
        vicState.refreshCounter = 0xFF;

    resetCAccessLatch();

    d011_per_raster[raster] = registers.control & 0x7F;
    d016_per_raster[raster] = registers.control2 & 0x1F;
    d018_per_raster[raster] = registers.memory_pointer & 0xFE;

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

    const bool badAtCycle14 = isBadLine(raster);

    reloadCharacterSequencerAtCycle14(badAtCycle14);

    vicState.badLineLatchedAt14 = badAtCycle14;

    traceVicCycleCheckpoint("cycle-14", raster, currentCycle);

    if (badAtCycle14)
    {
        const bool firstBadlineThisFrame = (firstBadlineY < 0);

        initializeFirstBadLineIfNeeded(raster);

        if (firstBadlineThisFrame)
            vicState.displayEnabledNext = true;
    }
}

void Vic::handleBadLineFetchStartDecisions()
{
    const int raster = registers.raster;

    if (!vicState.cAccessActive)
        return;

    traceVicBadLineStart(raster, currentCycle, vicState. vcBase, vicState.rc, true);

    beginBadLineFetch();
}

void Vic::handleDmaStartCycleDecisions()
{
    const int raster = registers.raster;

    const uint16_t nextRaster = (raster + 1) % cfg_->maxRasterLines;

    d011_per_raster[nextRaster] = registers.control & 0x7F;
    d016_per_raster[nextRaster] = registers.control2 & 0x1F;
    d018_per_raster[nextRaster] = registers.memory_pointer & 0xFE;
}

void Vic::handleCycle58Decisions()
{
    traceVicCycleCheckpoint("cycle-58", registers.raster, currentCycle);

    if (registers.raster >= 0 && registers.raster < static_cast<int>(rasterRowStates.size()))
        rasterRowStates[registers.raster].displayRc = vicState.rc;

    advanceCharacterSequencerAtCycle58();
}

void Vic::runPhi1Phase()
{
    currentBusPhase = VicBusPhase::Phi1;

    tracePhi1BusCollision();

    const bool gAccessOccurred = performGAccessForCurrentCycle();

    if (gAccessOccurred)
        advanceCharacterSequencerAfterGAccess();

    if (currentCycleSlot.refresh)
        performRefreshFetchForCurrentCycle();

    // Sprite pointer fetch scheduled for Phi1.
    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const auto& timing = cfg_->spriteFetchTiming[sprite];

        if (currentCycle == timing.pointerCycle && timing.pointerPhase == VicBusPhase::Phi1)
        {
            fetchSpritePointer(sprite, registers.raster);
            break;
        }
    }

    // Sprite data fetch scheduled for Phi1.
    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const int byteIndex = spriteDataByteForCyclePhase(sprite, currentCycle, VicBusPhase::Phi1);

        if (byteIndex >= 0)
        {
            performSpriteDataFetchForSprite(sprite, byteIndex);
            break;
        }
    }

    if (currentCycleSlot.phi1BusOwner == BusOwner::Idle)
        performIdleFetchForCurrentCycle();

    // First half of the VIC cycle is now visible before Phi2 CPU writes.
    runPixelOutputPhase(0, 4);
}

void Vic::runPhi2Phase()
{
    currentBusPhase = VicBusPhase::Phi2;

    tracePhi2BusCollision();

    // Sprite pointer fetch scheduled for Phi2.
    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const auto& timing = cfg_->spriteFetchTiming[sprite];

        if (currentCycle == timing.pointerCycle && timing.pointerPhase == VicBusPhase::Phi2)
        {
            fetchSpritePointer(sprite, registers.raster);
            break;
        }
    }

    // Sprite data fetch scheduled for Phi2.
    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const int byteIndex = spriteDataByteForCyclePhase(sprite, currentCycle, VicBusPhase::Phi2);

        if (byteIndex >= 0 && spriteUnits[sprite].dmaActive)
        {
            performSpriteDataFetchForSprite(sprite, byteIndex);
            break;
        }
    }

    // Bad-line c-access is handled on Phi2.
    performBadLineFetchesForCurrentCycle();

    if (currentCycle == cfg_->spriteMcBaseAdvanceCycle2)
        advanceSpriteMCBaseSecondStep();

    // Second half of the VIC cycle sees Phi2-visible register changes.
    runPixelOutputPhase(4, 8);
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
    emitDotRasterLine(curRaster);

    snapshotRasterPixelComposition(curRaster);
    snapshotRasterRowState(curRaster);

    updateSpriteDMAEndOfLine(curRaster);

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
            const int fbH = cfg_->visibleLines + 2 * VERTICAL_BORDER_SIZE;

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

    rasterIrqTriggeredThisLine = false;

    // Bad-line/DMA state is local to one raster line.
    vicState.badLineCondition = false;
    vicState.badLineLatchedAt14 = false;
    vicState.cAccessActive = false;
    vicState.badLineDmaStartCycle = -1;
    vicState.badLineFetchIndex = 0;
    vicState.matrixFetchInitializedThisRaster = false;
    vicState.displayStateHoldForCycle58 = false;
}

void Vic::recordRasterEventLog(RasterEventKind kind, uint16_t address, uint8_t oldValue, uint8_t newValue)
{
    if (oldValue == newValue)
        return;

    RasterEventRecord e;
    e.kind = kind;
    e.raster = registers.raster;
    e.cycle = currentCycle;
    e.phase = currentBusPhase;
    e.address = address;
    e.oldValue = oldValue;
    e.newValue = newValue;

    rasterEventLog.push_back(e);

    if (e.raster >= 0 && e.raster < static_cast<int>(rasterEventsByRaster.size()))
        rasterEventsByRaster[e.raster].push_back(e);
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

int Vic::rasterRegisterEventPixelX(const RasterEventRecord& e) const
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

int Vic::cyclePixelX(int cycle) const
{
    if (cycle < 0)
        cycle = 0;

    if (cycle >= cfg_->cyclesPerLine)
        cycle %= cfg_->cyclesPerLine;

    const int rasterWidth = cfg_->cyclesPerLine * 8;

    int hardwareX = cfg_->hardware_X + cycle * 8;
    hardwareX %= rasterWidth;

    int framebufferX = hardwareX - cfg_->hardware_X + HORIZONTAL_BORDER_SIZE;
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
