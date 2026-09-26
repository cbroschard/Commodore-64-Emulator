// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Vic.h"

void Vic::saveState(StateWriter& wrtr) const
{
    // VIC0 = "Core" and Registers
    wrtr.beginChunk("VIC0");
    wrtr.writeU32(1); // version

    // Dump Sprite Registers
    for (int i = 0; i < 8; ++i)
    {
        wrtr.writeU8(registers.spriteX[i]);
        wrtr.writeU8(registers.spriteY[i]);
    }

    wrtr.writeU8(registers.spriteX_MSB);
    wrtr.writeU8(registers.spriteEnabled);
    wrtr.writeU8(registers.spriteYExpansion);
    wrtr.writeU8(registers.spritePriority);
    wrtr.writeU8(registers.spriteMultiColor);
    wrtr.writeU8(registers.spriteXExpansion);

    // Dump Control registers
    wrtr.writeU8(registers.control);
    wrtr.writeU8(registers.control2);

    // Dump Memory Pointer
    wrtr.writeU8(registers.memory_pointer);

    // Dump Background/Border color registers
    wrtr.writeU8(registers.borderColor);
    wrtr.writeU8(registers.backgroundColor0);
    for (int i = 0; i < 3; ++i)
        wrtr.writeU8(registers.backgroundColor[i]);

    // Dump Sprite Color Registers
    wrtr.writeU8(registers.spriteMultiColor1);
    wrtr.writeU8(registers.spriteMultiColor2);
    for (int i = 0; i < 8; ++i)
        wrtr.writeU8(registers.spriteColors[i]);

    // Dump Raster
    wrtr.writeU16(registers.raster);

    // Dump Interrupt Control
    wrtr.writeU8(registers.interruptStatus);
    wrtr.writeU8(registers.interruptEnable);
    wrtr.writeU16(registers.rasterInterruptLine);

    // Dump Lightpen
    wrtr.writeU8(registers.light_pen_X);
    wrtr.writeU8(registers.light_pen_Y);

    // Dump Collision Latches
    wrtr.writeU8(registers.spriteCollision);
    wrtr.writeU8(registers.spriteDataCollision);

    // End the chunk
    wrtr.endChunk();

    // VICX = Runtime
    wrtr.beginChunk("VICX");
    wrtr.writeU32(10); // version

    // Dump video mode
    wrtr.writeU8(static_cast<uint8_t>(mode_));

    // Dump current cycle
    wrtr.writeI32(currentCycle);

    // Dump Sprite/FIFO
    for (int i=0;i<8;++i)  wrtr.writeU16(sprPtrBase[i]);
    for (int i=0;i<40;++i) wrtr.writeU8(charPtrFIFO[i]);
    for (int i=0;i<40;++i) wrtr.writeU8(colorPtrFIFO[i]);

    // Dump Misc
    wrtr.writeBool(denSeenOn30);
    wrtr.writeI32(firstBadlineY);

    // Dump State
    wrtr.writeU16(vicState.vc);
    wrtr.writeU16(vicState.vcBase);
    wrtr.writeU16(vicState.vmliBase);
    wrtr.writeU8(vicState.vmliFetchIndex);
    wrtr.writeU8(vicState.badLineFetchIndex);
    wrtr.writeI32(vicState.badLineDmaStartCycle);

    wrtr.writeU8(vicState.rc);

    wrtr.writeU8(vicState.refreshCounter);

    wrtr.writeBool(vicState.displayEnabled);
    wrtr.writeBool(vicState.displayEnabledNext);
    wrtr.writeBool(vicState.badLineCondition);
    wrtr.writeBool(vicState.badLineLatchedAt14);
    wrtr.writeBool(vicState.cAccessActive);
    wrtr.writeBool(vicState.matrixFetchInitializedThisRaster);
    wrtr.writeBool(vicState.displayStateHoldForCycle58);

    wrtr.writeBool(vicState.verticalBorder);
    wrtr.writeBool(vicState.horizontalBorder);

    wrtr.writeBool(vicState.leftBorder);
    wrtr.writeBool(vicState.rightBorder);

    wrtr.writeI32(vicState.leftBorderOpenX);
    wrtr.writeI32(vicState.rightBorderCloseX);

    wrtr.writeI32(vicState.topBorderOpenRaster);
    wrtr.writeI32(vicState.bottomBorderCloseRaster);

    wrtr.writeBool(vicState.ba);
    wrtr.writeBool(vicState.aec);

    wrtr.writeBool(vicState.lightPenLatchedThisFrame);

    wrtr.writeBool(rasterIrqCompareMatched);
    wrtr.writeBool(rasterIrqDeferredReassert);
    wrtr.writeBool(rasterIrqTriggeredThisLine);

    wrtr.writeBool(activeMatrixRow.valid);
    wrtr.writeU16(activeMatrixRow.vcBase);
    wrtr.writeI32(activeMatrixRow.row);

    for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
        wrtr.writeU8(activeMatrixRow.screen[i]);

    for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
        wrtr.writeU8(activeMatrixRow.color[i]);

    for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
        wrtr.writeU8(activeMatrixRow.fetched[i]);

    for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
        wrtr.writeU8(activeMatrixRow.invalid[i]);

    for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
        wrtr.writeU8(activeMatrixRow.invalidScreen[i]);

    for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
        wrtr.writeU8(activeMatrixRow.invalidColor[i]);

    for (const auto& s : spriteUnits)
    {
        wrtr.writeBool(s.dmaActive);
        wrtr.writeBool(s.yExpandFlipFlop);
        wrtr.writeBool(s.yCrunchPending);

        wrtr.writeU8(s.mc);
        wrtr.writeU8(s.mcBase);

        wrtr.writeU8(s.pointerByte);
        wrtr.writeU16(s.dataBase);

        wrtr.writeU8(s.shift0);
        wrtr.writeU8(s.shift1);
        wrtr.writeU8(s.shift2);

        wrtr.writeI32(s.currentRow);

        wrtr.writeI32(s.startY);

        wrtr.writeI32(s.outputBit);
        wrtr.writeI32(s.outputRepeat);
        wrtr.writeBool(s.rowPrepared);
        wrtr.writeBool(s.rowDataLatched);

        wrtr.writeI32(s.outputXStart);
        wrtr.writeI32(s.outputWidth);

        wrtr.writeU8(s.fetched0);
        wrtr.writeU8(s.fetched1);
        wrtr.writeU8(s.fetched2);
    }

    // Dump Latches
    wrtr.writeVectorU8(d011_per_raster);
    wrtr.writeVectorU8(d016_per_raster);
    wrtr.writeVectorU8(d018_per_raster);

    // Background graphics latches
    for (const auto& latch : backgroundGraphicsLatches)
    {
        wrtr.writeBool(latch.valid);

        wrtr.writeU8(latch.screenByte);
        wrtr.writeU8(latch.colorByte);
        wrtr.writeU8(latch.graphicsByte);
    }

    wrtr.writeU8(backgroundSequencer.shiftRegister);
    wrtr.writeU8(backgroundSequencer.attributes.screenByte);
    wrtr.writeU8(backgroundSequencer.attributes.colorByte);

    wrtr.writeI32(backgroundSequencer.nextX);

    wrtr.writeU8(backgroundSequencer.dotsRemaining);
    wrtr.writeU8(backgroundSequencer.multicolorPairValue);
    wrtr.writeU8(backgroundSequencer.multicolorPairPhase);

    // Pending background reload
    wrtr.writeBool(pendingBgReload.valid);
    wrtr.writeI32(pendingBgReload.column);
    wrtr.writeI32(pendingBgReload.baseX);

    // Dump frameDone
    wrtr.writeBool(frameDone);

    // End the chunk
    wrtr.endChunk();
}

bool Vic::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "VIC0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                  { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                           { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 8; ++i)
        {
            if (!rdr.readU8(registers.spriteX[i]))              { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(registers.spriteY[i]))              { rdr.exitChunkPayload(chunk); return false; }
        }

        if (!rdr.readU8(registers.spriteX_MSB))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spriteEnabled))               { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spriteYExpansion))            { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spritePriority))              { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spriteMultiColor))            { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spriteXExpansion))            { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.control))                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.control2))                    { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.memory_pointer))              { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.borderColor))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.backgroundColor0))            { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 3; ++i)
            if (!rdr.readU8(registers.backgroundColor[i]))      { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.spriteMultiColor1))           { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spriteMultiColor2))           { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 8; ++i)
            if (!rdr.readU8(registers.spriteColors[i]))         { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU16(registers.raster))                     { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.interruptStatus))             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.interruptEnable))             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU16(registers.rasterInterruptLine))        { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.light_pen_X))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.light_pen_Y))                 { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(registers.spriteCollision))             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(registers.spriteDataCollision))         { rdr.exitChunkPayload(chunk); return false; }

        postLoadState();

        rdr.exitChunkPayload(chunk);
        return true;
    }

    if (std::memcmp(chunk.tag, "VICX", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                          { rdr.exitChunkPayload(chunk); return false; }
        if (ver < 2 || ver > 10)                                        { rdr.exitChunkPayload(chunk); return false; }

        uint8_t m = 0;
        if (!rdr.readU8(m))                                             { rdr.exitChunkPayload(chunk); return false; }

        setMode(static_cast<VideoMode>(m));

        if (!rdr.readI32(currentCycle))                                 { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 8; ++i)
            if (!rdr.readU16(sprPtrBase[i]))                            { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 40; ++i)
            if (!rdr.readU8(charPtrFIFO[i]))                            { rdr.exitChunkPayload(chunk); return false; }

        for (int i = 0; i < 40; ++i)
            if (!rdr.readU8(colorPtrFIFO[i]))                           { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(denSeenOn30))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(firstBadlineY))                                { rdr.exitChunkPayload(chunk); return false; }

        if (ver >= 3)
            if (!rdr.readU16(vicState.vc))                              { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU16(vicState.vcBase))                              { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU16(vicState.vmliBase))                            { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(vicState.vmliFetchIndex))                       { rdr.exitChunkPayload(chunk); return false; }

        if (ver >= 7)
        {
            if (!rdr.readU8(vicState.badLineFetchIndex))                { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(vicState.badLineDmaStartCycle))            { rdr.exitChunkPayload(chunk); return false; }
        }
        else
        {
            vicState.badLineFetchIndex = 0;
            vicState.badLineDmaStartCycle = -1;
        }

        if (!rdr.readU8(vicState.rc))                                   { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(vicState.refreshCounter))                       { rdr.exitChunkPayload(chunk); return false; }

        // VICX versions 2-7 stored the obsolete matrixAdvancePending
        // boolean here. Consume it to preserve the legacy chunk layout.
        if (ver >= 2 && ver <= 7)
        {
            bool legacyMatrixAdvancePending = false;
            if (!rdr.readBool(legacyMatrixAdvancePending))             { rdr.exitChunkPayload(chunk); return false; }
        }

        if (!rdr.readBool(vicState.displayEnabled))                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.displayEnabledNext))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.badLineCondition))                   { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.badLineLatchedAt14))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.cAccessActive))                      { rdr.exitChunkPayload(chunk); return false; }

        if (ver >= 8)
        {
            if (!rdr.readBool(vicState.matrixFetchInitializedThisRaster))   { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(vicState.displayStateHoldForCycle58))         { rdr.exitChunkPayload(chunk); return false; }
        }
        else
        {
            vicState.matrixFetchInitializedThisRaster = false;
        }

        if (!rdr.readBool(vicState.verticalBorder))                 { rdr.exitChunkPayload(chunk); return false; }

        if (ver >= 6)
        {
            if (!rdr.readBool(vicState.horizontalBorder))           { rdr.exitChunkPayload(chunk); return false; }
        }
        else
        {
            vicState.horizontalBorder = true;
        }

        if (!rdr.readBool(vicState.leftBorder))                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.rightBorder))                    { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readI32(vicState.leftBorderOpenX))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(vicState.rightBorderCloseX))               { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readI32(vicState.topBorderOpenRaster))             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(vicState.bottomBorderCloseRaster))         { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(vicState.ba))                             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(vicState.aec))                            { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(vicState.lightPenLatchedThisFrame))       { rdr.exitChunkPayload(chunk); return false; }

        if (ver >= 9)
        {
            if (!rdr.readBool(rasterIrqCompareMatched))             { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(rasterIrqDeferredReassert))           { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(rasterIrqTriggeredThisLine))          { rdr.exitChunkPayload(chunk); return false; }
        }
        else
        {
            bool legacyRasterIrqSampledThisLine = false;

            if (!rdr.readBool(legacyRasterIrqSampledThisLine))      { rdr.exitChunkPayload(chunk); return false; }

            // Preserve the most sensible equivalent state from an old save.
            rasterIrqCompareMatched = legacyRasterIrqSampledThisLine && rasterIRQTargetMatchesVisibleRaster();
        }

        if (ver >= 8)
        {
            if (!rdr.readBool(activeMatrixRow.valid))               { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU16(activeMatrixRow.vcBase))               { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(activeMatrixRow.row))                  { rdr.exitChunkPayload(chunk); return false; }

            for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
            {
                if (!rdr.readU8(activeMatrixRow.screen[i]))         { rdr.exitChunkPayload(chunk); return false; }
            }

            for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
            {
                if (!rdr.readU8(activeMatrixRow.color[i]))          { rdr.exitChunkPayload(chunk); return false; }
            }

            for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
            {
                if (!rdr.readU8(activeMatrixRow.fetched[i]))        { rdr.exitChunkPayload(chunk); return false; }
            }

            for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
            {
                if (!rdr.readU8(activeMatrixRow.invalid[i]))        { rdr.exitChunkPayload(chunk); return false; }
            }

            for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
            {
                if (!rdr.readU8(activeMatrixRow.invalidScreen[i]))  { rdr.exitChunkPayload(chunk); return false; }
            }

            for (int i = 0; i < BACKGROUND_MATRIX_COLUMNS; ++i)
            {
                if (!rdr.readU8(activeMatrixRow.invalidColor[i]))   { rdr.exitChunkPayload(chunk); return false; }
            }
        }
        else
        {
            resetActiveMatrixRow();
            resetCAccessLatch();
        }

        for (auto& s : spriteUnits)
        {
            if (!rdr.readBool(s.dmaActive))                         { rdr.exitChunkPayload(chunk); return false; }

            if (ver >= 9)
            {
                if (!rdr.readBool(s.yExpandFlipFlop))               { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readBool(s.yCrunchPending))                { rdr.exitChunkPayload(chunk); return false; }
            }
            else
            {
                // VICX versions 1-8 stored the obsolete yExpandLatch
                // boolean in this position. Consume it to preserve the
                // old chunk layout, then derive a reasonable flip-flop state.
                bool legacyYExpandLatch = false;

                if (!rdr.readBool(legacyYExpandLatch))              { rdr.exitChunkPayload(chunk); return false; }

                s.yExpandFlipFlop = !legacyYExpandLatch;
            }

            if (!rdr.readU8(s.mc))                                  { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.mcBase))                              { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readU8(s.pointerByte))                         { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU16(s.dataBase))                           { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readU8(s.shift0))                              { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.shift1))                              { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.shift2))                              { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readI32(s.currentRow))                         { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readI32(s.startY))                             { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readI32(s.outputBit))                          { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(s.outputRepeat))                       { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(s.rowPrepared))                       { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(s.rowDataLatched))                    { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readI32(s.outputXStart))                       { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(s.outputWidth))                        { rdr.exitChunkPayload(chunk); return false; }

            if (!rdr.readU8(s.fetched0))                            { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.fetched1))                            { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readU8(s.fetched2))                            { rdr.exitChunkPayload(chunk); return false; }
        }

        if (!rdr.readVectorU8(d011_per_raster))                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU8(d016_per_raster))                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU8(d018_per_raster))                     { rdr.exitChunkPayload(chunk); return false; }

        // VICX versions 1-8 stored a per-raster DD00/VIC-bank vector.
        // VIC bank selection is no longer restored from this obsolete
        // raster-latched state, but the serialized data must still be
        // consumed to keep the old chunk aligned.
        if (ver <= 8)
        {
            std::vector<uint16_t> legacyDD00PerRaster;
            if (!rdr.readVectorU16(legacyDD00PerRaster))            { rdr.exitChunkPayload(chunk); return false; }
        }

        if (ver >= 2)
        {
            // Background graphics latches
            for (auto& latch : backgroundGraphicsLatches)
            {
                if (!rdr.readBool(latch.valid))                     { rdr.exitChunkPayload(chunk); return false; }

                if (ver <= 9)
                {
                    int legacyColumn = -1;
                    if (!rdr.readI32(legacyColumn))                 { rdr.exitChunkPayload(chunk); return false; }

                }

                if (!rdr.readU8(latch.screenByte))                  { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readU8(latch.colorByte))                   { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readU8(latch.graphicsByte))                { rdr.exitChunkPayload(chunk); return false; }

                if (ver >= 2 && ver <= 9)
                {
                    uint16_t legacyGraphicsAddress = 0;
                    if (!rdr.readU16(legacyGraphicsAddress))        { rdr.exitChunkPayload(chunk); return false; }
                }

                if (ver >= 6 && ver <= 9)
                {
                    uint8_t legacyD011 = 0;
                    uint8_t legacyD016 = 0;
                    uint8_t legacyD018 = 0;
                    uint8_t legacyMode = 0;

                    if (!rdr.readU8(legacyD011)) { rdr.exitChunkPayload(chunk); return false; }
                    if (!rdr.readU8(legacyD016)) { rdr.exitChunkPayload(chunk); return false; }
                    if (!rdr.readU8(legacyD018)) { rdr.exitChunkPayload(chunk); return false; }
                    if (!rdr.readU8(legacyMode)) { rdr.exitChunkPayload(chunk); return false; }
                }
            }

            bool legacyActiveBgValid = false;

            if (ver <= 9)
                if (!rdr.readBool(legacyActiveBgValid))             { rdr.exitChunkPayload(chunk); return false; }

            bool legacyMulticolorText = false;

            if (ver >= 4 && ver <= 9)
                if (!rdr.readBool(legacyMulticolorText))            {rdr.exitChunkPayload(chunk); return false; }

            graphicsMode legacyMode = graphicsMode::standard;

            if (ver >= 6 && ver <= 9)
            {
                uint8_t mode = 0;
                if (!rdr.readU8(mode))                              { rdr.exitChunkPayload(chunk); return false; }
                legacyMode = static_cast<graphicsMode>(mode);
            }

            uint8_t legacyRowBits = 0;

            if (ver <= 9)
                if (!rdr.readU8(legacyRowBits))                     { rdr.exitChunkPayload(chunk); return false; }

            if (ver <= 9)
            {
                uint8_t legacyFg = 0;
                uint8_t legacyBg0 = 0;
                uint8_t legacyBg1 = 0;
                uint8_t legacyBg2 = 0;
                uint8_t legacyBg0Source = 0;

                if (!rdr.readU8(legacyFg))                          { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readU8(legacyBg0))                         { rdr.exitChunkPayload(chunk); return false; }

                if (ver >= 4)
                {
                    if (!rdr.readU8(legacyBg1))                     { rdr.exitChunkPayload(chunk); return false; }
                    if (!rdr.readU8(legacyBg2))                     { rdr.exitChunkPayload(chunk); return false; }
                }

                if (ver >= 5)
                {
                    if (!rdr.readU8(legacyBg0Source))               { rdr.exitChunkPayload(chunk); return false; }
                }
            }

            int legacyPxBase = 0;
            int legacyPy = 0;

            if (ver <= 9)
            {
                if (!rdr.readI32(legacyPxBase))                     { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readI32(legacyPy))                         { rdr.exitChunkPayload(chunk); return false; }
            }

            if (ver >= 10)
            {
                if (!rdr.readU8(backgroundSequencer.shiftRegister))           { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readU8(backgroundSequencer.attributes.screenByte))   { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readU8(backgroundSequencer.attributes.colorByte))    { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readI32(backgroundSequencer.nextX))                  { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readU8(backgroundSequencer.dotsRemaining))           { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readU8(backgroundSequencer.multicolorPairValue))     { rdr.exitChunkPayload(chunk); return false; }
                if (!rdr.readU8(backgroundSequencer.multicolorPairPhase))     { rdr.exitChunkPayload(chunk); return false; }

            }
            else
            {
                int legacyPhaseRaw = 0;

                if (!rdr.readI32(legacyPhaseRaw))       { rdr.exitChunkPayload(chunk); return false; }

                const int legacyPhase = std::clamp(legacyPhaseRaw, 0, 8);

                const bool multicolorShifter = legacyMode == graphicsMode::multicolorBitmap ||
                    legacyMode == graphicsMode::illegalMulticolorBitmap || ((legacyMode == graphicsMode::multicolor ||
                    legacyMode == graphicsMode::illegalText) && legacyMulticolorText);

                if (legacyActiveBgValid)
                {
                    if (multicolorShifter)
                    {
                        const int pairsConsumed = legacyPhase / 2;

                        backgroundSequencer.shiftRegister = static_cast<uint8_t>(legacyRowBits << (pairsConsumed * 2));
                        backgroundSequencer.multicolorPairPhase = static_cast<uint8_t>(legacyPhase & 1);
                        backgroundSequencer.multicolorPairValue = static_cast<uint8_t>((backgroundSequencer.shiftRegister >> 6) & 0x03);
                    }
                    else
                    {
                        backgroundSequencer.shiftRegister = static_cast<uint8_t>(legacyRowBits << legacyPhase);
                        backgroundSequencer.multicolorPairValue = 0;
                        backgroundSequencer.multicolorPairPhase = 0;
                    }

                    backgroundSequencer.nextX = legacyPxBase + legacyPhase;
                    backgroundSequencer.dotsRemaining = static_cast<uint8_t>(8 - legacyPhase);
                }
                else
                {
                    backgroundSequencer.shiftRegister = 0;
                    backgroundSequencer.nextX = 0;
                    backgroundSequencer.dotsRemaining = 0;
                    backgroundSequencer.multicolorPairValue = 0;
                    backgroundSequencer.multicolorPairPhase = 0;
                }

                backgroundSequencer.attributes = {};
            }
        }
        else
        {
            // VICX v1 did not contain graphics-latch or live pixel-shifter state.
            resetBackgroundGraphicsLatches();
            resetBackgroundSequencer();
        }

        if (ver >= 10)
        {
            if (!rdr.readBool(pendingBgReload.valid))            { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(pendingBgReload.column))            { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readI32(pendingBgReload.baseX))             { rdr.exitChunkPayload(chunk); return false; }
        }
        else
        {
            // VICX v2-v9 had no pending dot-reload state.
            pendingBgReload = {};
        }

        if (!rdr.readBool(frameDone))                                   { rdr.exitChunkPayload(chunk); return false; }

        if (ver < 3)
            vicState.vc = vicState.vcBase;

        postLoadState();

        rdr.exitChunkPayload(chunk);
        return true;
    }

    return false;
}

void Vic::postLoadState()
{
    // Reconnect config pointer from current/restored mode.
    cfg_ = (mode_ == VideoMode::NTSC ? &NTSC_CONFIG : &PAL_CONFIG);

    // Keep only basic range safety. Do NOT rewrite restored internal sequencer state.
    if (registers.raster >= cfg_->maxRasterLines)
        registers.raster %= cfg_->maxRasterLines;

    if (currentCycle < 0)
        currentCycle = 0;

    if (currentCycle >= cfg_->cyclesPerLine)
        currentCycle %= cfg_->cyclesPerLine;

    // Normalize raw register-style values only.
    registers.rasterInterruptLine &= 0x01FF;

    registers.control &= 0x7F;
    registers.control2 &= 0x1F;
    registers.memory_pointer &= 0xFE;

    registers.interruptStatus &= 0x0F;
    registers.interruptEnable &= 0x0F;

    registers.borderColor &= 0x0F;
    registers.backgroundColor0 &= 0x0F;

    for (int i = 0; i < 3; ++i)
        registers.backgroundColor[i] &= 0x0F;

    registers.spriteMultiColor1 &= 0x0F;
    registers.spriteMultiColor2 &= 0x0F;

    for (int i = 0; i < 8; ++i)
        registers.spriteColors[i] &= 0x0F;

    // Make sure vectors exist and are valid-sized, but do not overwrite valid restored contents.
    auto fixSizeU8 = [&](std::vector<uint8_t>& v, uint8_t fill)
    {
        if (v.size() != static_cast<size_t>(cfg_->maxRasterLines))
            v.assign(cfg_->maxRasterLines, fill);
    };

    fixSizeU8(d011_per_raster, registers.control & 0x7F);
    fixSizeU8(d016_per_raster, registers.control2 & 0x1F);
    fixSizeU8(d018_per_raster, registers.memory_pointer & 0xFE);

    fixSizeU8(borderVertical_per_raster, vicState.verticalBorder ? 1 : 0);
    fixSizeU8(borderVerticalStart_per_raster, vicState.verticalBorder ? 1 : 0);

    if (borderLeftOpenX_per_raster.size() != static_cast<size_t>(cfg_->maxRasterLines))
        borderLeftOpenX_per_raster.assign(cfg_->maxRasterLines, vicState.leftBorderOpenX);

    if (borderRightCloseX_per_raster.size() != static_cast<size_t>(cfg_->maxRasterLines))
        borderRightCloseX_per_raster.assign(cfg_->maxRasterLines, vicState.rightBorderCloseX);

    rasterRowStates.resize(cfg_->maxRasterLines);
    lastFrameRasterRowStates.resize(cfg_->maxRasterLines);

    rasterPixelStates.resize(cfg_->maxRasterLines);
    lastFrameRasterPixelStates.resize(cfg_->maxRasterLines);

    // Keep sprite restored state, only mask fields that have known hardware ranges.
    for (auto& s : spriteUnits)
    {
        s.mc &= 0x3F;
        s.mcBase &= 0x3F;
    }

    // Recompute derived live state from restored values.
    currentCycleSlot = cycleSlotFor(registers.raster, currentCycle);

    updateGraphicsMode(registers.raster);
    updateBusArbitration();
    updateIRQLine();

    // Debug/monitor cache refresh only.
    updateMonitorCaches(registers.raster);

    // Treat this as diagnostic only unless behavior depends on it.
    lastRasterIRQSample = {};
}
