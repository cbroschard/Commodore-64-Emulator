// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CPU.h"
#include "Vic.h"

bool Vic::vicTraceOn(TraceManager::TraceDetail d) const
{
    return traceMgr && traceMgr->vicDetailOn(d);
}

TraceManager::Stamp Vic::makeVicStamp() const
{
    if (!traceMgr)
        return TraceManager::Stamp{0, 0xFFFF, 0xFFFF};

    return traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, registers.raster, static_cast<uint16_t>(currentCycle * 8));
}

void Vic::traceBackgroundGraphicsFetch(int raster, int cycle, int column, int fetchPixelX, int outputX) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BUS))
        return;

    std::ostringstream out;

    out << "[VIC:GACCESS] "
        << "raster=" << raster
        << " cycle=" << cycle
        << " phase=" << busPhaseName(currentBusPhase)
        << " col=" << column
        << " liveVC=$"
        << std::hex << std::uppercase
        << std::setw(4) << std::setfill('0')
        << vicState.vc
        << " VMLI=" << std::dec
        << int(vicState.vmliFetchIndex)
        << " fetchX=" << fetchPixelX
        << " displayX="
        << (BACKGROUND_40COL_X0 +
            (d016ForRasterPixelX(
                raster,
                fetchPixelX,
                false) & 0x07) +
            column * 8)
        << " outputX=" << outputX;

    traceVicBusEvent(out.str());
}

void Vic::traceRasterEnd()
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_RASTER))
        return;

    TraceManager::Stamp stamp = traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, registers.raster, (currentCycle * 8));
    traceMgr->recordVicRaster(registers.raster, currentCycle, (registers.interruptStatus & 0x01) != 0, registers.control,
                              registers.rasterInterruptLine & 0xFF, stamp);
}

void Vic::traceVicCycleCheckpoint(const char* phase, int raster, int cycle) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BADLINE))
        return;

    const bool den = (effectiveD011ForRaster(raster) & 0x10) != 0;
    const int row = currentCharacterRow();

    std::ostringstream out;
    out << "[VIC:CYCLE] "
        << phase
        << " raster=" << std::dec << raster
        << " cycle=" << cycle
        << " vcBase=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
        << vicState.vcBase
        << " rc=" << std::dec << int(vicState.rc)
        << " bad=" << (vicState.badLineCondition ? 1 : 0)
        << " disp=" << (vicState.displayEnabled ? 1 : 0)
        << " DEN=" << (den ? 1 : 0)
        << " row=" << row;

    traceMgr->recordVicBadline(out.str(), makeVicStamp());
}

void Vic::traceVicBusEvent(const std::string& text) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BUS))
        return;

    traceMgr->recordVicBus(text, makeVicStamp());
}

void Vic::traceVicRasterIrqEvent(const char* phase, uint16_t oldLine, uint16_t newLine, bool matched) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_IRQ))
        return;

    std::ostringstream out;
    out << "[VIC:IRQ] "
        << phase
        << " old=$" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << oldLine
        << " new=$" << std::setw(3) << newLine
        << " cur=$" << std::setw(3) << registers.raster
        << " match=" << std::dec << (matched ? 1 : 0)
        << " ISR=$" << std::hex << std::uppercase << std::setw(2) << int(registers.interruptStatus & 0x0F)
        << " IER=$" << std::setw(2) << int(registers.interruptEnable & 0x0F);

    traceMgr->recordVicIrqEvent(out.str(), makeVicStamp());
}


void Vic::traceVicRasterRetargetTest(const char* reason, uint16_t oldLine, uint16_t newLine, bool matchedBefore,
    bool matchedAfter, bool triggeredBefore, bool triggeredAfter, uint8_t isrBefore, uint8_t isrAfter, bool irqBefore,
    bool irqAfter) const
{
    if (!traceMgr || !vicTraceOn(TraceManager::TraceDetail::VIC_IRQ))
        return;

    std::ostringstream out;

    const uint16_t visibleRaster = visibleRasterForIRQCompare();

    out << "[VIC:IRQTEST] "
        << (reason ? reason : "target-write")

        << " phase=Phi2"

        << " curRaster=$"
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

        << " old=$"
        << std::hex
        << std::setw(3)
        << oldLine

        << " new=$"
        << std::setw(3)
        << newLine

        << " match="
        << std::dec
        << (matchedBefore ? 1 : 0)
        << "->"
        << (matchedAfter ? 1 : 0)

        << " triggered="
        << (triggeredBefore ? 1 : 0)
        << "->"
        << (triggeredAfter ? 1 : 0)

        << " ISR=$"
        << std::hex
        << std::setw(2)
        << std::setfill('0')
        << int(isrBefore)

        << "->$"
        << std::setw(2)
        << int(isrAfter)

        << " IER=$"
        << std::setw(2)
        << int(registers.interruptEnable & 0x0F)

        << " IRQ="
        << std::dec
        << (irqBefore ? 1 : 0)
        << "->"
        << (irqAfter ? 1 : 0);

    traceMgr->recordVicIrqEvent(out.str(), makeVicStamp());
}

void Vic::traceVicRegWrite(uint16_t address, uint8_t oldValue, uint8_t newValue) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_REG))
        return;

    std::ostringstream out;
    out << "[VIC:REG] $"
        << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << address
        << " old=$" << std::setw(2) << int(oldValue)
        << " new=$" << std::setw(2) << int(newValue);

    traceMgr->recordVicRegister(out.str(), makeVicStamp());
}

void Vic::traceVicBadLineStart(int raster, int cycle, uint16_t vcBase, uint8_t rc, bool den) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BADLINE))
        return;

    std::ostringstream out;
    out << "[VIC:BADLINE] start"
        << " raster=" << std::dec << raster
        << " cycle=" << cycle
        << " vcBase=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << vcBase
        << " rc=" << std::dec << int(rc)
        << " DEN=" << (den ? 1 : 0);

    traceMgr->recordVicBadline(out.str(), makeVicStamp());
}

void Vic::traceVicBadLineFetch(int raster, int cycle, int fetchIndex, uint16_t vc, int row, int col,
                               uint8_t screenByte, uint8_t colorByte) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BUS))
        return;

    std::ostringstream out;

    out << "[VIC:CACCESS] "
        << " raster=" << std::dec << raster
        << " cycle=" << cycle
        << " idx=" << fetchIndex
        << " addrVC=$"
        << std::hex << std::uppercase
        << std::setw(4) << std::setfill('0')
        << vc
        << " liveVC=$"
        << std::setw(4)
        << vicState.vc
        << " VMLI=" << std::dec
        << int(vicState.vmliFetchIndex)
        << " row=" << row
        << " col=" << col
        << " screen=$"
        << std::hex << std::uppercase
        << std::setw(2)
        << int(screenByte)
        << " color=$"
        << std::setw(2)
        << int(colorByte);

    traceVicBusEvent(out.str());
}

void Vic::traceVicSpriteDmaStart(int sprite) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    if (sprite < 0 || sprite >= 8)
        return;

    std::ostringstream out;
    out << "[VIC:SPRITE] DMA start"
        << " spr=" << sprite
        << " raster=" << std::dec << registers.raster
        << " y=$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << int(registers.spriteY[sprite]);

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
}

void Vic::traceVicSpritePtrFetch(int sprite, int raster, uint16_t ptrLoc, uint8_t ptr) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    if (sprite < 0 || sprite >= 8)
        return;

    std::ostringstream out;
    out << "[VIC:SPRITE] ptr fetch"
        << " spr=" << std::dec << sprite
        << " raster=" << raster
        << " cycle=" << currentCycle
        << " busPhase=" << busPhaseName(currentBusPhase)
        << " addr=$"
        << std::hex << std::uppercase
        << std::setw(4) << std::setfill('0') << ptrLoc
        << " ptr=$" << std::setw(2) << int(ptr)
        << " dataBase=$" << std::setw(4)
        << (uint16_t(ptr) << 6);

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
}

void Vic::traceVicSpriteDataFetch(int sprite, int raster, int byteIndex, uint16_t addr, uint8_t value) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    if (sprite < 0 || sprite >= 8)
        return;

    std::ostringstream out;
    out << "[VIC:SPRITE] data fetch"
        << " spr=" << std::dec << sprite
        << " raster=" << raster
        << " cycle=" << currentCycle
        << " busPhase=" << busPhaseName(currentBusPhase)
        << " expectedPhase="
        << (spriteDataFetchUsesPhi2(byteIndex) ? "Phi2" : "Phi1")
        << " byte=" << byteIndex
        << " addr=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << addr
        << " value=$" << std::setw(2) << int(value);

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
}

void Vic::traceVicSpriteSlotEvent(int sprite, const char* phase, int raster, int cycle, int byteIndex) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    if (sprite < 0 || sprite >= 8)
        return;

    const SpriteUnit& su = spriteUnits[sprite];

    const bool yExpanded = (registers.spriteYExpansion & (1u << sprite)) != 0;

    std::ostringstream out;
    out << "[VIC:SPR] "
        << "s=" << sprite
        << " phase=" << phase
        << " ras=$" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << raster
        << " cyc=$" << std::setw(2) << cycle
        << " dot=" << std::dec << (cycle * 8)
        << " ptr=$" << std::hex << std::uppercase << std::setw(2)
        << cfg_->spriteFetchTiming[sprite].pointerCycle
        << " dma=" << std::dec << (su.dmaActive ? 1 : 0)
        << " rowlat=" << (su.rowDataLatched ? 1 : 0)
        << " yexp=" << (yExpanded ? 1 : 0) << " yff=" << (su.yExpandFlipFlop ? 1 : 0)
        << " mc=" << int(su.mc)
        << " mcbase=" << int(su.mcBase)
        << " row=" << su.currentRow
        << " ptr=$" << std::hex << std::uppercase << std::setw(2) << int(su.pointerByte)
        << " base=$" << std::setw(4) << su.dataBase;

    if (byteIndex >= 0)
        out << " byte=" << std::dec << byteIndex;

    out << " f0=$" << std::hex << std::uppercase << std::setw(2) << int(su.fetched0)
        << " f1=$" << std::setw(2) << int(su.fetched1)
        << " f2=$" << std::setw(2) << int(su.fetched2);

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
}

void Vic::traceVicSpriteEolState(int sprite, int raster) const
{
    traceVicSpriteSlotEvent(sprite, "eol", raster, currentCycle, -1);
}

void Vic::traceVicSpriteAdvanceDecision(int sprite, int raster, bool willAdvance) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    if (sprite < 0 || sprite >= 8)
        return;

    std::ostringstream out;
    out << "[VIC:SPR] "
        << "s=" << sprite
        << " phase=advance-check"
        << " ras=$" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << raster
        << " cyc=$" << std::setw(2) << currentCycle
        << " dot=" << std::dec << (currentCycle * 8)
        << " willAdvance=" << (willAdvance ? 1 : 0)
        << " dma=" << (spriteUnits[sprite].dmaActive ? 1 : 0)
        << " rowlat=" << (spriteUnits[sprite].rowDataLatched ? 1 : 0)
        << " yexp=" << ((registers.spriteYExpansion & (1u << sprite)) ? 1 : 0) << " yff="
        << (spriteUnits[sprite].yExpandFlipFlop ? 1 : 0)
        << " mc=" << int(spriteUnits[sprite].mc)
        << " mcbase=" << int(spriteUnits[sprite].mcBase)
        << " row=" << spriteUnits[sprite].currentRow;

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
}

void Vic::traceVicSpriteStartCheck(int sprite, int raster, uint8_t spriteY, bool enabled, bool yExpanded,
    bool rasterMatch, bool willStart) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    if (sprite < 0 || sprite >= 8)
        return;

    std::ostringstream out;
    out << "[VIC:SPR] "
        << "s=" << sprite
        << " phase=start-check"
        << " ras=$" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << raster
        << " cyc=$" << std::setw(2) << currentCycle
        << " dot=" << std::dec << (currentCycle * 8)
        << " sprY=$" << std::hex << std::uppercase << std::setw(2) << int(spriteY)
        << std::dec
        << " en=" << int(enabled)
        << " yexp=" << int(yExpanded)
        << " match=" << int(rasterMatch)
        << " start=" << int(willStart)
        << " dma=" << int(spriteUnits[sprite].dmaActive)
        << " rowlat=" << int(spriteUnits[sprite].rowDataLatched)
        << " row=" << spriteUnits[sprite].currentRow
        << " mc=" << int(spriteUnits[sprite].mc)
        << " mcbase=" << int(spriteUnits[sprite].mcBase)
        << " startY=" << spriteUnits[sprite].startY;

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
}

void Vic::traceVicSpriteRowMismatch(int sprite, int raster, int computedRow) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_SPRITE))
        return;

    if (sprite < 0 || sprite >= 8)
        return;

    std::ostringstream out;
    out << "[VIC:SPR] row-mismatch"
        << " spr=" << sprite
        << " raster=" << raster
        << " computed=" << computedRow
        << " current=" << spriteUnits[sprite].currentRow
        << " mcBase=" << int(spriteUnits[sprite].mcBase)
        << " dma=" << int(spriteUnits[sprite].dmaActive)
        << " rowlat=" << int(spriteUnits[sprite].rowDataLatched)
        << " yFF=" << int(spriteUnits[sprite].yExpandFlipFlop)
        << " startY=" << spriteUnits[sprite].startY;

    traceMgr->recordVicSprite(out.str(), makeVicStamp());
}

void Vic::traceVicBusArb(bool oldBA, bool oldAEC, bool newBA, bool newAEC, bool badLineNow, bool baLow, bool aecLow) const
{
    if (!vicTraceOn(TraceManager::TraceDetail::VIC_BUS))
        return;

    std::ostringstream out;
    out << "[VIC:BUS] "
        << "reason=" << busArbReason(registers.raster, currentCycle)
        << " bad=" << (badLineNow ? 1 : 0)
        << " BA " << (oldBA ? 'H' : 'L') << "->" << (newBA ? 'H' : 'L')
        << " AEC " << (oldAEC ? 'H' : 'L') << "->" << (newAEC ? 'H' : 'L')
        << " balow=" << (baLow ? 1 : 0)
        << " aeclow=" << (aecLow ? 1 : 0);

    traceVicBusEvent(out.str());
}

void Vic::tracePhi1BusCollision() const
{
    int requestCount = 0;

    bool wantsGAccess = currentCycleSlot.graphicsFetch;
    bool wantsRefresh = currentCycleSlot.refresh;

    int pointerSprite = -1;
    int dataSprite = -1;
    int dataByte = -1;

    if (wantsGAccess)
        ++requestCount;

    if (wantsRefresh)
        ++requestCount;

    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const auto& timing = cfg_->spriteFetchTiming[sprite];

        if (currentCycle == timing.pointerCycle &&
            timing.pointerPhase == VicBusPhase::Phi1)
        {
            pointerSprite = sprite;
            ++requestCount;
        }

        if (spriteUnits[sprite].dmaActive)
        {
            const int byteIndex = spriteDataByteForCyclePhase(sprite, currentCycle, VicBusPhase::Phi1);

            if (byteIndex >= 0)
            {
                dataSprite = sprite;
                dataByte = byteIndex;
                ++requestCount;
            }
        }
    }

    if (requestCount <= 1)
        return;

    std::ostringstream ss;

    ss << "Phi1 collision"
       << " raster=" << registers.raster
       << " cycle=" << currentCycle
       << " requests=" << requestCount;

    if (wantsGAccess)
        ss << " G";

    if (wantsRefresh)
        ss << " REFRESH";

    if (pointerSprite >= 0)
        ss << " SPR" << pointerSprite << "_PTR";

    if (dataSprite >= 0)
    {
        ss << " SPR" << dataSprite
           << "_DATA" << dataByte;
    }

    traceVicBusEvent(ss.str());
}

void Vic::tracePhi2BusCollision() const
{
    int requestCount = 0;

    const bool wantsCharMatrix = vicState.cAccessActive && currentCycleSlot.matrixFetchIndex >= 0;

    int pointerSprite = -1;
    int dataSprite = -1;
    int dataByte = -1;

    if (wantsCharMatrix)
        ++requestCount;

    for (int sprite = 0; sprite < 8; ++sprite)
    {
        const auto& timing = cfg_->spriteFetchTiming[sprite];

        if (currentCycle == timing.pointerCycle &&
            timing.pointerPhase == VicBusPhase::Phi2)
        {
            pointerSprite = sprite;
            ++requestCount;
        }

        if (spriteUnits[sprite].dmaActive)
        {
            const int byteIndex = spriteDataByteForCyclePhase(sprite, currentCycle, VicBusPhase::Phi2);

            if (byteIndex >= 0)
            {
                dataSprite = sprite;
                dataByte = byteIndex;
                ++requestCount;
            }
        }
    }

    if (requestCount <= 1)
        return;

    std::ostringstream ss;

    ss << "Phi2 collision"
       << " raster=" << registers.raster
       << " cycle=" << currentCycle
       << " requests=" << requestCount;

    if (wantsCharMatrix)
        ss << " C";

    if (pointerSprite >= 0)
        ss << " SPR" << pointerSprite << "_PTR";

    if (dataSprite >= 0)
    {
        ss << " SPR" << dataSprite
           << "_DATA" << dataByte;
    }

    traceVicBusEvent(ss.str());
}
