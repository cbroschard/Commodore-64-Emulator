// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Bus.h"
#include "CPU.h"
#include "DataBusLatch.h"
#include "Vic.h"

void Vic::updateBusArbitration()
{
    const bool oldBA = vicState.ba;
    const bool oldAEC = vicState.aec;

    vicState.ba = !currentCycleSlot.baLow;
    vicState.aec = !currentCycleSlot.cpuBusStolen;

    if (cpu)
    {
        cpu->setRDY(vicState.ba);
        cpu->setAEC(vicState.aec);
    }

    if (oldBA != vicState.ba || oldAEC != vicState.aec)
        traceVicBusArb(oldBA, oldAEC, vicState.ba, vicState.aec, vicState.badLineLatchedAt14, currentCycleSlot.baLow,
                       currentCycleSlot.cpuBusStolen);
}

bool Vic::isBadLineCandidateForBusWarning(int raster) const
{
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return false;

    if (!denSeenOn30)
        return false;

    const uint8_t d011 = effectiveD011ForRaster(raster);
    const int yScroll = d011 & 0x07;

    if (raster < 0x30 || raster > 0xF7)
        return false;

    return (raster & 0x07) == yScroll;
}

bool Vic::isBadLineBusWarningCycle(int raster, int cycle) const
{
    if (!isBadLineCandidateForBusWarning(raster))
        return false;

    if (vicState.badLineDmaStartCycle < 0)
        return false;

    const int lineCycles = cfg_->cyclesPerLine;
    const int slot = vicState.badLineDmaStartCycle;

    const int warn0 = (slot - 3 + lineCycles) % lineCycles;
    const int warn1 = (slot - 2 + lineCycles) % lineCycles;
    const int warn2 = (slot - 1 + lineCycles) % lineCycles;

    return cycle == warn0 || cycle == warn1 || cycle == warn2;
}

bool Vic::isBadLineBusStealCycle(int raster, int cycle) const
{
    if (raster != registers.raster)
        return false;

    if (!vicState.cAccessActive)
        return false;

    if (vicState.badLineDmaStartCycle < 0)
        return false;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    return cycle >= vicState.badLineDmaStartCycle && cycle <= cfg_->DMAEndCycle;
}

bool Vic::isBadLineBAHoldCycle(int raster, int cycle) const
{
    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    return isBadLineBusWarningCycle(raster, cycle) || isBadLineBusStealCycle(raster, cycle);
}

bool Vic::isRefreshCycle(int cycle) const
{
    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    const int c0 = cfg_->refreshStartCycle;
    const int c1 = (c0 + 1) % cfg_->cyclesPerLine;
    const int c2 = (c0 + 2) % cfg_->cyclesPerLine;
    const int c3 = (c0 + 3) % cfg_->cyclesPerLine;
    const int c4 = (c0 + 4) % cfg_->cyclesPerLine;

    return cycle == c0 ||
           cycle == c1 ||
           cycle == c2 ||
           cycle == c3 ||
           cycle == c4;
}

void Vic::performRefreshFetchForCurrentCycle()
{
    if (!bus)
        return;

    // Refresh addresses are $3F00-$3FFF within the current
    // 16 KB VIC bank. The low byte comes from REF.
    const uint16_t address = static_cast<uint16_t>(0x3F00 | vicState.refreshCounter);

    const uint8_t value = bus->vicRead(address);

    updateOpenBus(value);

    // REF decrements after the refresh access.
    --vicState.refreshCounter;
}

bool Vic::isSpriteBusWarningCycle(int raster, int cycle) const
{
    (void)raster;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    const int lineCycles = cfg_->cyclesPerLine;

    for (int s = 0; s < 8; ++s)
    {
        if (!spriteUnits[s].dmaActive)
            continue;

        const int firstCpuStealCycle = firstSpriteCpuStealCycle(s);

        if (firstCpuStealCycle < 0)
            continue;

        const int warn0 = (firstCpuStealCycle - 3 + lineCycles) % lineCycles;
        const int warn1 = (firstCpuStealCycle - 2 + lineCycles) % lineCycles;
        const int warn2 = (firstCpuStealCycle - 1 + lineCycles) % lineCycles;

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

        // Pointer fetches are tracked as fetch events, but they should not
        // be modeled as full CPU-steal cycles.
        if (cycle == cfg_->spriteFetchTiming[s].pointerCycle)
            continue;

        if (isSpriteDataCpuStealCycle(s, cycle))
            return true;
    }

    return false;
}

bool Vic::isSpriteBusAECStealCycle(int raster, int cycle) const
{
    (void)raster;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    for (int sprite = 0; sprite < 8; ++sprite)
    {
        if (!spriteUnits[sprite].dmaActive)
            continue;

        if (isSpriteDataCpuStealCycle(sprite, cycle))
            return true;
    }

    return false;
}

bool Vic::isSpriteDataCpuStealCycle(int sprite, int cycle) const
{
    if (sprite < 0 || sprite >= 8)
        return false;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    if (!spriteUnits[sprite].dmaActive)
        return false;

    const SpriteFetchPhase phi2Phase = spriteFetchPhaseForCycle(sprite, cycle, VicBusPhase::Phi2);

    return spriteFetchPhaseStealsCpu(phi2Phase);
}

bool Vic::isSpriteBusBAHoldCycle(int raster, int cycle) const
{
    (void)raster;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return false;

    for (int sprite = 0; sprite < 8; ++sprite)
    {
        if (!spriteUnits[sprite].dmaActive)
            continue;

        if (isSpriteDataCpuStealCycle(sprite, cycle))
            return true;
    }

    return false;
}

uint8_t Vic::latchOpenBus(uint8_t value)
{
    const uint64_t cycle = cpu ? cpu->getTotalCycles() : 0;

    if (dataBus)
        dataBus->drive(value, DataBusLatch::Driver::VIC, cycle);

    return value;
}

uint8_t Vic::getOpenBus() const
{
    const uint64_t cycle = cpu ? cpu->getTotalCycles() : 0;
    return dataBus ? dataBus->sample(cycle) : 0xFF;
}

uint8_t Vic::latchOpenBusMasked(uint8_t definedBits, uint8_t definedMask)
{
     const uint64_t cycle = cpu ? cpu->getTotalCycles() : 0;

    if (!dataBus)
        return static_cast<uint8_t>(definedBits & definedMask);

    dataBus->drive(definedBits, definedMask, DataBusLatch::Driver::VIC, cycle);

    return dataBus->sample(cycle);
}

void Vic::updateOpenBus(uint8_t value)
{
    const uint64_t cycle = cpu ? cpu->getTotalCycles() : 0;

    if (dataBus)
        dataBus->drive(value, DataBusLatch::Driver::VIC, cycle);
}

void Vic::performIdleFetchForCurrentCycle()
{
    if (!bus)
        return;

    const uint16_t addr = idleFetchAddressForCurrentCycle();
    const uint8_t value = bus->vicRead(addr);

    updateOpenBus(value);
}

uint16_t Vic::idleFetchAddressForCurrentCycle() const
{
    const int sampleX = rasterEventPixelX(currentCycle);

    const uint8_t d011 = d011ForRasterPixelX(registers.raster, sampleX, false);

    // VIC-II idle g-access:
    //   ECM=0 -> $3FFF
    //   ECM=1 -> A9/A10 forced low -> $39FF
    return (d011 & 0x40) ? 0x39FF : 0x3FFF;
}

Vic::VicCycleSlot Vic::cycleSlotFor(int raster, int cycle) const
{
    VicCycleSlot slot {};

    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return slot;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return slot;

    slot.fetchKind = getFetchKindForCycle(raster, cycle);

    const int graphicsFetchStartCycle = cfg_->bgFetchStartCycle + 1;
    const int graphicsFetchEndCycle = graphicsFetchStartCycle + BACKGROUND_MATRIX_COLUMNS - 1;

    if (cycle >= graphicsFetchStartCycle && cycle <= graphicsFetchEndCycle)
    {
        const int index = cycle - graphicsFetchStartCycle;

        if (index >= 0 && index < BACKGROUND_MATRIX_COLUMNS)
        {
            slot.graphicsFetch = true;
            slot.graphicsFetchIndex = index;
        }
    }

    switch (slot.fetchKind)
    {
        case FetchKind::SpritePtr0:
        case FetchKind::SpritePtr1:
        case FetchKind::SpritePtr2:
        case FetchKind::SpritePtr3:
        case FetchKind::SpritePtr4:
        case FetchKind::SpritePtr5:
        case FetchKind::SpritePtr6:
        case FetchKind::SpritePtr7:
        {
            slot.spriteIndex = spritePointerFetchSpriteForKind(slot.fetchKind);
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
            slot.spriteIndex = spriteDataFetchSpriteForKind(slot.fetchKind);

            if (slot.spriteIndex >= 0)
            {
                int byteIndex = spriteDataByteForCyclePhase(slot.spriteIndex, cycle, VicBusPhase::Phi1);

                if (byteIndex < 0)
                    byteIndex = spriteDataByteForCyclePhase(slot.spriteIndex, cycle, VicBusPhase::Phi2);

                slot.spriteByteIndex = byteIndex;
            }

            break;
        }

        case FetchKind::None:
        default:
            break;
    }

    if (slot.spriteIndex >= 0)
    {
        SpriteFetchPhase expectedPhase = SpriteFetchPhase::None;

        switch (slot.fetchKind)
        {
            case FetchKind::SpritePtr0:
            case FetchKind::SpritePtr1:
            case FetchKind::SpritePtr2:
            case FetchKind::SpritePtr3:
            case FetchKind::SpritePtr4:
            case FetchKind::SpritePtr5:
            case FetchKind::SpritePtr6:
            case FetchKind::SpritePtr7:
                expectedPhase = SpriteFetchPhase::Pointer;
                break;

            case FetchKind::SpriteData0:
            case FetchKind::SpriteData1:
            case FetchKind::SpriteData2:
            case FetchKind::SpriteData3:
            case FetchKind::SpriteData4:
            case FetchKind::SpriteData5:
            case FetchKind::SpriteData6:
            case FetchKind::SpriteData7:
            {
                switch (slot.spriteByteIndex)
                {
                    case 0:
                        expectedPhase = SpriteFetchPhase::Data0;
                        break;

                    case 1:
                        expectedPhase = SpriteFetchPhase::Data1;
                        break;

                    case 2:
                        expectedPhase = SpriteFetchPhase::Data2;
                        break;

                    default:
                        break;
                }

                break;
            }

            default:
                break;
        }

        if (expectedPhase != SpriteFetchPhase::None)
        {
            const VicBusPhase busPhase = spriteBusPhaseForFetch(slot.spriteIndex, expectedPhase);

            slot.spriteFetchPhase = spriteFetchPhaseForCycle(slot.spriteIndex, cycle, busPhase);

            if (slot.spriteFetchPhase != SpriteFetchPhase::None)
            {
                slot.spriteBusPhase = busPhase;
                slot.spriteBusPhaseValid = true;
            }
        }
    }

    slot.badlineWarning = isBadLineBusWarningCycle(raster, cycle);
    slot.badlineSteal = isBadLineBusStealCycle(raster, cycle);
    slot.badlineBAHold = isBadLineBAHoldCycle(raster, cycle);
    slot.spriteWarning = isSpriteBusWarningCycle(raster, cycle);
    slot.spriteBAHold = isSpriteBusBAHoldCycle(raster, cycle);
    slot.spriteAECSteal = isSpriteBusAECStealCycle(raster, cycle);
    slot.refresh = isRefreshCycle(cycle);

    //
    // Phi1 bus ownership
    //

    slot.phi1BusOwner = BusOwner::Idle;

    if (slot.graphicsFetch)
        slot.phi1BusOwner = BusOwner::Graphics;

    if (slot.refresh)
        slot.phi1BusOwner = BusOwner::Refresh;

    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const auto& timing = cfg_->spriteFetchTiming[sprite];

        if (cycle == timing.pointerCycle && timing.pointerPhase == VicBusPhase::Phi1)
        {
            slot.phi1BusOwner = BusOwner::SpritePointer;
            break;
        }

        if (spriteUnits[sprite].dmaActive)
        {
            const int byteIndex = spriteDataByteForCyclePhase(sprite, cycle, VicBusPhase::Phi1);

            if (byteIndex >= 0)
            {
                slot.phi1BusOwner = BusOwner::SpriteData;
                break;
            }
        }
    }

    //
    // BA / AEC state
    //

    slot.baLow = slot.badlineBAHold || slot.spriteWarning || slot.spriteBAHold;
    slot.cpuBusStolen = slot.badlineSteal || slot.spriteAECSteal;
    slot.aecLow = slot.cpuBusStolen;
    slot.rasterIrqSample = isRasterIRQCompareCycle(cycle);
    slot.latchRasterState = cycle == 0;
    slot.sampleBadline = cycle == 14;
    slot.startSpriteDmaCheck = cycle == cfg_->spriteDmaCheckCycle1 || cycle == cfg_->spriteDmaCheckCycle2;
    slot.transferDisplayState = cycle == 58;
    slot.startBadlineFetch = cycle == cfg_->DMAStartCycle;

    //
    // Bad-line c-access state
    //

    const bool cAccessForThisRaster = (raster == registers.raster) ? vicState.cAccessActive : isBadLine(raster);

    if (cAccessForThisRaster && cycle >= cfg_->bgFetchStartCycle && cycle <= cfg_->bgFetchEndCycle)
    {
        const int index = cycle - cfg_->bgFetchStartCycle;

        slot.matrixFetchIndex = index >= 0 && index < BACKGROUND_MATRIX_COLUMNS ? index : -1;
    }

    //
    // Phi2 bus ownership
    //

    if (cAccessForThisRaster && slot.matrixFetchIndex >= 0)
        slot.phi2BusOwner = BusOwner::BadLine;

    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const auto& timing = cfg_->spriteFetchTiming[sprite];

        if (cycle == timing.pointerCycle && timing.pointerPhase == VicBusPhase::Phi2)
        {
            slot.phi2BusOwner = BusOwner::SpritePointer;
            break;
        }

        if (spriteUnits[sprite].dmaActive)
        {
            const int byteIndex = spriteDataByteForCyclePhase(sprite, cycle, VicBusPhase::Phi2);

            if (byteIndex >= 0)
            {
                slot.phi2BusOwner = BusOwner::SpriteData;
                break;
            }
        }
    }

    return slot;
}

Vic::FetchKind Vic::getFetchKindForCycle(int raster, int cycle) const
{
    if (raster < 0 || raster >= cfg_->maxRasterLines)
        return FetchKind::None;

    if (cycle < 0 || cycle >= cfg_->cyclesPerLine)
        return FetchKind::None;

    for (int s = 0; s < 8; ++s)
    {
        const auto& timing = cfg_->spriteFetchTiming[s];

        if (cycle == timing.pointerCycle)
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

    if (isRefreshCycle(cycle))
        return "refresh";

    return "none";
}

const char* Vic::busPhaseName(VicBusPhase phase) const
{
    switch (phase)
    {
        case VicBusPhase::Phi1:
            return "Phi1";

        case VicBusPhase::Phi2:
            return "Phi2";

        default:
            return "?";
    }
}

std::string Vic::dumpBusArbitrationLine(int raster) const
{
    std::ostringstream out;

    if (raster < 0 || raster >= cfg_->maxRasterLines)
    {
        out << "Invalid raster\n";
        return out.str();
    }

    out << "VIC bus arbitration - raster " << raster << "\n";
    out << "Cy BA AEC BW BS BH SW SH SS Fetch\n";
    out << "----------------------------------\n";

    for (int cycle = 0; cycle < cfg_->cyclesPerLine; ++cycle)
    {
        const VicCycleSlot slot = cycleSlotFor(raster, cycle);

        out << std::setw(2) << cycle << " "
            << (slot.baLow ? 'L' : 'H') << "  "
            << (slot.aecLow ? 'L' : 'H') << "   "
            << (slot.badlineWarning ? '1' : '0') << "  "
            << (slot.badlineSteal ? '1' : '0') << "  "
            << (slot.badlineBAHold ? '1' : '0') << "  "
            << (slot.spriteWarning ? '1' : '0') << "  "
            << (slot.spriteBAHold ? '1' : '0') << "  "
            << (slot.spriteAECSteal ? '1' : '0') << "  "
            << static_cast<int>(slot.fetchKind)
            << "\n";
    }

    return out.str();
}
