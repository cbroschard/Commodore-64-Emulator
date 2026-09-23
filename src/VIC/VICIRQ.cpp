// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CPU.h"
#include "IRQLine.h"
#include "Vic.h"

void Vic::updateIRQLine()
{
    const uint8_t pending = static_cast<uint8_t>((registers.interruptStatus & registers.interruptEnable) & 0x0F);
    const bool any = pending != 0;

    if (IRQ)
    {
        if (any)
            IRQ->raiseIRQ(IRQLine::VICII);
        else
            IRQ->clearIRQ(IRQLine::VICII);
    }
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

void Vic::evaluateRasterIRQCompare(const char* reason)
{
    const char* compareReason = reason ? reason : "raster-compare";

    const uint16_t visibleRaster = visibleRasterForIRQCompare();

    const uint16_t targetRaster = static_cast<uint16_t>(registers.rasterInterruptLine & 0x01FF);

    const bool targetInRange = targetRaster < cfg_->maxRasterLines;

    const bool matchNow = targetInRange && visibleRaster == targetRaster;

    const bool matchedBefore = rasterIrqCompareMatched;

    lastRasterIRQSample.valid = true;
    lastRasterIRQSample.raster = static_cast<int>(registers.raster);
    lastRasterIRQSample.cycle = currentCycle;
    lastRasterIRQSample.visibleRaster = visibleRaster;
    lastRasterIRQSample.targetRaster = targetRaster;
    lastRasterIRQSample.targetInRange = targetInRange;
    lastRasterIRQSample.matched = matchNow;
    lastRasterIRQSample.sampledBefore = matchedBefore;
    lastRasterIRQSample.reason = compareReason;

    traceVicCycleCheckpoint("raster-irq-compare", registers.raster, currentCycle);

    // Raster IRQ occurs when the comparator enters the matching state,
    // but the VIC-II permits only one raster IRQ trigger per raster line.
    if (matchNow && !matchedBefore)
    {
        const uint8_t isrBefore = static_cast<uint8_t>(registers.interruptStatus & 0x0F);
        const bool irqBefore = irqLineActive();

        const bool rasterIrqAlreadyPending = (registers.interruptStatus & 0x01) != 0;

        if (rasterIrqAlreadyPending)
            rasterIrqDeferredReassert = true;

        raiseVicIRQSource(0x01);
        rasterIrqTriggeredThisLine = true;

        const uint8_t isrAfter = static_cast<uint8_t>(registers.interruptStatus & 0x0F);
        const bool irqAfter = irqLineActive();

        if (traceMgr && vicTraceOn(TraceManager::TraceDetail::VIC_IRQ))
        {
            std::ostringstream out;

            out << "[RASTER-TRIGGER]"

                << " reason="
                << compareReason

                << " raster=$"
                << std::hex
                << std::uppercase
                << std::setw(3)
                << std::setfill('0')
                << registers.raster

                << " visible=$"
                << std::setw(3)
                << visibleRaster

                << " cycle="
                << std::dec
                << currentCycle

                << " target=$"
                << std::hex
                << std::setw(3)
                << targetRaster

                << " ISR=$"
                << std::setw(2)
                << int(isrBefore)

                << "->$"
                << std::setw(2)
                << int(isrAfter)

                << " IRQ="
                << std::dec
                << (irqBefore ? 1 : 0)
                << "->"
                << (irqAfter ? 1 : 0);

            traceMgr->recordVicIrqEvent(out.str(), makeVicStamp());
        }
    }

    // Comparator state is independent of the D019 interrupt latch.
    rasterIrqCompareMatched = matchNow;
}

void Vic::setRasterIRQTarget(uint16_t newLine, const char* reason, uint8_t writtenValue, bool highWrite)
{
    const uint16_t oldLine = static_cast<uint16_t>(registers.rasterInterruptLine & 0x01FF);
    const uint16_t storedNewLine = static_cast<uint16_t>(newLine & 0x01FF);

    const bool oldTargetCompareNow = oldLine < cfg_->maxRasterLines && oldLine == visibleRasterForIRQCompare() &&
        isRasterIRQCompareCycle(currentCycle);

    if (oldTargetCompareNow)
        raiseVicIRQSource(0x01);

    registers.rasterInterruptLine = storedNewLine;

    const bool rmwDummyWrite = cpu && cpu->isRMWDummyWriteCycle();
    const bool rmwWrite = cpu && cpu->isRMWWriteCycle();

    handleRasterIRQTargetWrite(oldLine, storedNewLine, highWrite, rmwWrite, rmwDummyWrite);

    (void)reason;
    (void)writtenValue;
}

void Vic::handleRasterIRQTargetWrite(uint16_t oldTarget, uint16_t newTarget, bool highWrite, bool rmwWrite, bool rmwDummyWrite)
{
    const uint16_t currentRaster = static_cast<uint16_t>(registers.raster);

    bool triggerIRQ = false;

    if (rmwWrite)
    {
        if (highWrite)
        {
            // RMW write to $D011 changes raster compare bit 8.
            //
            // Special boundary case:
            // At cycle 0, when the low raster byte has wrapped to $00,
            // the value involved in the RMW behavior corresponds to the
            // previous raster line.
            if (currentCycle == 0 && (currentRaster & 0x00FF) == 0)
            {
                const uint16_t previousRaster = (currentRaster == 0) ? static_cast<uint16_t>(cfg_->maxRasterLines - 1)
                        : static_cast<uint16_t>(currentRaster - 1);

                if (previousRaster != oldTarget && (oldTarget & 0x00FF) == (previousRaster & 0x00FF))
                {
                    triggerIRQ = true;
                }
            }
            else
            {
                if (currentRaster != oldTarget && (oldTarget & 0x00FF) == (currentRaster & 0x00FF))
                {
                    triggerIRQ = true;
                }
            }
        }
        else
        {
            // RMW write to $D012 changes raster compare bits 0-7.
            if (currentCycle == 0)
            {
                const uint16_t previousRaster = (currentRaster == 0) ? static_cast<uint16_t>(cfg_->maxRasterLines - 1)
                        : static_cast<uint16_t>(currentRaster - 1);

                if (previousRaster != oldTarget && (oldTarget & 0x0100) == (previousRaster & 0x0100))
                {
                    triggerIRQ = true;
                }
            }
            else
            {
                if (currentRaster != oldTarget && (oldTarget & 0x0100) == (currentRaster & 0x0100))
                {
                    triggerIRQ = true;
                }
            }
        }
    }

    // Normal retarget case:
    //
    // Changing D011/D012 so that the new programmed target
    // becomes the raster currently being displayed can itself
    // produce a raster interrupt.
    if (newTarget == currentRaster && currentRaster != oldTarget)
    {
        triggerIRQ = true;
    }

    if (triggerIRQ)
    {
        raiseVicIRQSource(0x01);
    }

    rasterIrqCompareMatched = newTarget == currentRaster;

    // We retain this parameter because the CPU exposes it and it
    // may be useful for diagnostics, but the VIC-II rule operates
    // on the active RMW sequence rather than only the dummy write.
    (void)rmwDummyWrite;
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
    // VIC-II raster line 0 performs the raster compare one cycle
    // later than the other raster lines.
    if (registers.raster == 0)
        return cycle == 1;

    return cycle == RASTER_IRQ_COMPARE_CYCLE;
}

uint16_t Vic::visibleRasterForIRQCompare() const
{
    if (registers.raster >= cfg_->maxRasterLines)
        return 0;

    // VIC-II special case:
    //
    // On raster 0, the raster counter reset occurs one cycle later
    // than the normal raster-line increment. Internally our raster
    // has already wrapped to 0 at this point, so during cycle 0 the
    // externally/comparator-visible counter must still represent
    // the final raster of the previous frame.
    if (registers.raster == 0 && currentCycle == 0)
        return static_cast<uint16_t>(cfg_->maxRasterLines - 1);

    return static_cast<uint16_t>(registers.raster);
}

uint16_t Vic::visibleRasterForRead() const
{
    if (registers.raster >= cfg_->maxRasterLines)
        return 0;

    // Same raster-0 delayed-counter-reset behavior as the IRQ
    // comparator. $D011/$D012 still expose the previous raster
    // during cycle 0 of our internally designated raster 0.
    if (registers.raster == 0 && currentCycle == 0)
        return static_cast<uint16_t>(cfg_->maxRasterLines - 1);

    return static_cast<uint16_t>(registers.raster);
}

uint8_t Vic::d019Read() const
{
    const uint8_t src = registers.interruptStatus & 0x0F;
    const uint8_t irq = ((src & registers.interruptEnable & 0x0F) != 0) ? 0x80 : 0x00;
    return irq | src;
}
