// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Vic.h"
#include "IO.h"

Vic::Vic(VideoMode mode) :
    mode_(mode),
    cfg_(mode == VideoMode::NTSC ? &NTSC_CONFIG : &PAL_CONFIG),
    charPtrFIFO(40),
    colorPtrFIFO(40),
    rowCounter(0)
{
    d011_per_raster.resize(cfg_->maxRasterLines);
    d016_per_raster.resize(cfg_->maxRasterLines);
    d018_per_raster.resize(cfg_->maxRasterLines);
}

Vic::~Vic() = default;

void Vic::reset()
{
    // Initialize all registers to default values
    std::fill(std::begin(registers.spriteX), std::end(registers.spriteX), 0x00);
    std::fill(std::begin(registers.spriteY), std::end(registers.spriteY), 0x00);
    std::fill(std::begin(registers.spriteColors), std::end(registers.spriteColors), 0x00);
    registers.spriteX_MSB = 0x00;
    registers.control = 0x1B;
    registers.raster = 0x00;
    registers.light_pen_X = 0x00;
    registers.light_pen_Y = 0x00;
    registers.spriteEnabled = 0x00;
    registers.control2 = 0x08;
    registers.spriteYExpansion = 0x00;
    registers.memory_pointer = 0x14;
    registers.interruptStatus = 0x00;
    registers.interruptEnable = 0x00;
    registers.spritePriority = 0x00;
    registers.spriteMultiColor = 0x00;
    registers.spriteXExpansion = 0x00;
    registers.spriteCollision = 0x00;
    registers.spriteDataCollision = 0x00;
    registers.backgroundColor0 = 0x00;
    registers.borderColor = 0x00;
    registers.backgroundColor[0] = 0x00;
    registers.backgroundColor[1] = 0x00;
    registers.backgroundColor[2] = 0x00;
    registers.spriteMultiColor1 = 0x00;
    registers.spriteMultiColor2 = 0x00;
    registers.rasterInterruptLine = cfg_->maxRasterLines + 1;
    registers.undefined = 0xFF; // Undefined always returns 0xFF

    // AEC
    currentCycle = 0;
    AEC = true;

    // Default character mode
    currentMode = graphicsMode::standard;

    // Bad line vars reset
    currentScreenRow = 0;
    rowCounter = 0;

    // Frame completion flag
    frameDone = false;

    // Default per raster register latches
    std::fill(std::begin(d011_per_raster), std::end(d011_per_raster), 0x1B);
    std::fill(std::begin(d016_per_raster), std::end(d016_per_raster), 0x08);
    std::fill(std::begin(d018_per_raster), std::end(d018_per_raster), 0x14);

    // Initialize bgOpaque
    bgOpaque.resize(cfg_->visibleLines + 2*BORDER_SIZE);
    for (auto &row : bgOpaque) row.fill(0);

    // Initialize monitor caches
    updateMonitorCaches(registers.raster);
}

void Vic::setMode(VideoMode mode)
{
    mode_ = mode;
    cfg_  = (mode == VideoMode::NTSC ? &NTSC_CONFIG : &PAL_CONFIG);
}

uint8_t Vic::readRegister(uint16_t address)
{
    // Handle all SpriteX and SpriteY registers with helper
    if (address >= 0xD000 && address <= 0xD00F)
    {
        int index = getSpriteIndex(address);
        return isSpriteX(address) ? registers.spriteX[index] : registers.spriteY[index];
    }
    // Handle multicolor registers with helper
    else if (address >= 0xD022 && address <= 0xD024)
    {
        return getBackgroundColor(address - 0xD022);
    }
    // Handle SpriteColor registers with helper
    else if (address >= 0xD027 && address <= 0xD02E)
    {
        int index = getSpriteColorIndex(address);
        return registers.spriteColors[index];
    }

    switch(address)
    {
        case 0xD010:
        {
            return registers.spriteX_MSB;
        }
        case 0xD011:
        {
            // Bit 7 of $D011 reflects the high bit of the raster counter
            uint8_t highBit = (registers.raster >> 8) & 0x01;

            // Combine the control register (bits 0-6) with the high bit of the raster counter
            return (registers.control & 0x7F) | (highBit << 7);
        }
        case 0xD012:
        {
            // Return the low byte of the current raster counter
            return registers.raster & 0xFF;
        }
        case 0xD013:
        {
            return registers.light_pen_X;
        }
        case 0xD014:
        {
            return registers.light_pen_Y;
        }
        case 0xD015:
        {
            return registers.spriteEnabled;
        }
        case 0xD016:
        {
            return registers.control2;
        }
        case 0xD017:
        {
            return registers.spriteYExpansion;
        }
        case 0xD018:
        {
            return registers.memory_pointer;
        }
        case 0xD019:
        {
            uint8_t srcs = registers.interruptStatus & 0x0F;
            uint8_t any  = srcs ? 0x80 : 0x00;
            return (srcs | any | 0x70);
        }
        case 0xD01A:
        {
            return (registers.interruptEnable & 0x0F) | 0xF0; // Bits 4-6 are always 1
        }
        case 0xD01B:
        {
            return registers.spritePriority;
        }
        case 0xD01C:
        {
            return registers.spriteMultiColor;
        }
        case 0xD01D:
        {
            return registers.spriteXExpansion;
        }
        case 0xD01E:
        {
            uint8_t value = registers.spriteCollision;
            registers.spriteCollision = 0;
            return value;
        }
        case 0xD01F:
        {
            uint8_t value = registers.spriteDataCollision;
            registers.spriteDataCollision = 0;
            return value;
        }
        case 0xD020:
        {
            return registers.borderColor;
        }
        case 0xD021:
        {
            return registers.backgroundColor0;
        }
        case 0xD025:
        {
            return registers.spriteMultiColor1;
        }
        case 0xD026:
        {
            return registers.spriteMultiColor2;
        }
        case 0xD02F:
        {
            return registers.undefined;
        }
        case 0xD030:
        {
            return 0xFF; // handle for now as breadbin c64 only
        }
        default:
        {
            if (logger)
            {
                logger->WriteLog("Attempt to read to unhandled VIC address = " + std::to_string (static_cast<int>(address)));
                return 0xFF;
            }
        }
    }
    return 0xFF;
}

void Vic::writeRegister(uint16_t address, uint8_t value)
{
    // Handle SpriteX and SpriteY registers with helper
    if (address >= 0xD000 && address <= 0xD00F)
    {
        int index = getSpriteIndex(address);
        if (isSpriteX(address))
        {
            registers.spriteX[index] = value;
        }
        else
        {
            registers.spriteY[index] = value;
        }
        return;
    }
    // Handle multicolor registers with helper
    else if (address >= 0xD022 && address <= 0xD024)
    {
        registers.backgroundColor[address - 0xD022] = value & 0x0F;
        return;
    }
    // Handle Sprite Color registers with helper
    else if (address >= 0xD027 && address <= 0xD02E)
    {
        int index = getSpriteColorIndex(address);
        registers.spriteColors[index] = value & 0x0F; // Mask to 4 bits
        return;
    }
    switch(address)
    {
     case 0xD010:
        {
            registers.spriteX_MSB = value;
            break;
        }
        case 0xD011:
        {
            // Update the high bit of the raster interrupt line (bit 8)
            registers.rasterInterruptLine = (registers.rasterInterruptLine & 0x00FF) | ((value & 0x80) << 1);

            //registers.control = value & 0x7F;
            registers.control = value;

            break;
        }
        case 0xD012:
        {
            // Update the low byte of the raster interrupt line
            registers.rasterInterruptLine = (registers.rasterInterruptLine & 0xFF00) | value;
            break;
        }
        case 0xD013:
        {
            registers.light_pen_X = value;
            break;
        }
        case 0xD014:
        {
            registers.light_pen_Y = value;
            break;
        }
        case 0xD015:
        {
            registers.spriteEnabled = value;
            break;
        }
        case 0xD016:
        {
            registers.control2 = value;
            break;
        }
        case 0xD017:
        {
            registers.spriteYExpansion = value;
            break;
        }
        case 0xD018:
        {
            registers.memory_pointer = value;
            break;
        }
        case 0xD019:
        {
            value &= 0x0F;
            registers.interruptStatus &= ~value;

            if (IRQ &&
                !(registers.interruptStatus & registers.interruptEnable))
            {
              IRQ->clearIRQ(IRQ->VICII);
            }
            break;
        }
        case 0xD01A:
        {
            registers.interruptEnable = value & 0x0F; // Only bits 0-3 are valid

            // Check if enabling interrupts triggers a pending IRQ
            if ((registers.interruptStatus & registers.interruptEnable) && IRQ)
            {
                 IRQ->raiseIRQ(IRQ->VICII);
            }
            break;
        }
        case 0xD01B:
        {
            registers.spritePriority = value;
            break;
        }
        case 0xD01C:
        {
            registers.spriteMultiColor = value;
            break;
        }
        case 0xD01D:
        {
            registers.spriteXExpansion = value;
            break;
        }
        case 0xD01E:
        {
            registers.spriteCollision &= ~value;
            break;
        }
        case 0xD01F:
        {
            registers.spriteDataCollision &= ~value;
            break;
        }
        case 0xD020:
        {
            registers.borderColor = value & 0x0F;
            break;
        }
        case 0xD021:
        {
            registers.backgroundColor0 = value & 0x0F;
            break;
        }
        case 0xD025:
        {
            registers.spriteMultiColor1 = value & 0x0F;
            break;
        }
        case 0xD026:
        {
            registers.spriteMultiColor2 = value & 0x0F;
            break;
        }
        case 0xD02F:
        {
            break;
        }
        case 0xD030:
        {
            break;
        }
        default:
        {
            if (logger)
            {
                logger->WriteLog("Attempt to write to unhandled vic area address = " + std::to_string (static_cast<int>(address)));
                break;
            }
        }
    }
}

void Vic::tick(int cycles)
{
    while (cycles-- > 0)
    {
        // D016 latches at cycle 12
        if (currentCycle == 12)
        {
            d016_per_raster[registers.raster] = registers.control2;
        }

        if (currentCycle == cfg_->DMAStartCycle)
        {
            // Latch VIC registers right before char DMA
            d011_per_raster[registers.raster] = registers.control & 0x7F;
            d018_per_raster[registers.raster] = registers.memory_pointer;

            // Update the ML Monitor caches
            updateMonitorCaches(registers.raster);

            for (int i = 0; i < 8; ++i)
            {
                int rowInSprite, fbLine;
                if (spriteCoversRaster(i, registers.raster, rowInSprite, fbLine) && rowInSprite == 0)
                {
                    uint16_t ptrLoc = getScreenBase(registers.raster) + 0x03F8 + i;
                    sprPtrBase[i] = (uint16_t(mem->vicRead(ptrLoc, registers.raster)) << 6);
                }
            }
        }

        // Bad line DMA: fetch char + color
        if (isBadLine(registers.raster))
        {
            int fetchIndex = currentCycle - cfg_->DMAStartCycle;

            if (fetchIndex >= 0 && fetchIndex < 40)
            {
                currentScreenRow = (registers.raster - cfg_->firstVisibleLine) / 8;
                if (currentScreenRow >= 0)
                {
                    charPtrFIFO[fetchIndex]  = fetchScreenByte(currentScreenRow, fetchIndex, registers.raster);
                    colorPtrFIFO[fetchIndex] = fetchColorByte (currentScreenRow, fetchIndex, registers.raster) & 0x0F;
                }
            }

            // Reset rowCounter *once per bad line*, at the first DMA cycle
            if (currentCycle == cfg_->DMAStartCycle)
            {
                rowCounter = 0;   // row 0 will be used during this raster's rendering
            }
        }

        ++currentCycle;

        // --- End of raster line ---
        if (currentCycle >= cfg_->cyclesPerLine)
        {
            currentCycle = 0;

            const int curRaster = registers.raster;

            // Render this line
            renderLine(curRaster);

            // Collision detection for this line
            detectSpriteToSpriteCollision(curRaster);
            detectSpriteToBackgroundCollision(curRaster);

            // Increment rowCounter once per raster inside visible area
            const bool DEN = (d011_per_raster[curRaster] & 0x10) != 0;
            bool badNextLine = isBadLine((curRaster + 1) % cfg_->maxRasterLines);
            if (DEN && (curRaster >= cfg_->firstVisibleLine) && (curRaster <= cfg_->lastVisibleLine))
            {
                if (!badNextLine)
                {
                    rowCounter = (rowCounter + 1) & 0x07;
                }
            }

            // Raster interrupt
            if (curRaster == registers.rasterInterruptLine)
            {
                registers.interruptStatus |= 0x01;
                if (IRQ && (registers.interruptEnable & 0x01))
                {
                    IRQ->raiseIRQ(IRQ->VICII);
                }
            }

            // Advance raster counter
            registers.raster = (registers.raster + 1) % cfg_->maxRasterLines;

            // End-of-frame flag
            if (curRaster == cfg_->maxRasterLines - 1)
            {
                frameDone = true;

                // Fill the rest of the framebuffer with the border color
                for (int y = cfg_->maxRasterLines; y < IO::SCREEN_HEIGHT_WITH_BORDER; ++y)
                {
                    // x0 and x1 are not used when drawing full border lines, so 0 is fine.
                    IO_adapter->renderBorderLine(y, registers.borderColor, 0, 0);
                }
            }
        }

        // BA/AEC bus arbitration
        updateAEC();
    }
}

void Vic::updateAEC()
{
    const int DMA_START = cfg_->DMAStartCycle;
    const int DMA_END   = cfg_->DMAEndCycle;
    const int LINE_CYCLES = cfg_->cyclesPerLine;

    const bool inCharDMA = isBadLine(registers.raster) && currentCycle >= DMA_START && currentCycle <= DMA_END;

    constexpr int SPR_DMA_CYCLES = 24;
    const int sprStart = DMA_END + 1;
    const int rel = (currentCycle - sprStart + LINE_CYCLES) % LINE_CYCLES;
    bool inSpriteDMA = spriteDMANeededThisLine() && rel < SPR_DMA_CYCLES;

    const bool vicSteals = inCharDMA || inSpriteDMA;

    AEC = !vicSteals;

    // check for BA
    const bool ba = !vicSteals;
    if (processor)
    {
        processor->setBAHold(!ba);
    }
}

bool Vic::isBadLine(int raster)
{
    // DEN must be on
    if (!(d011_per_raster[raster] & 0x10)) return false;

    // Must be within raster range 0x30 to 0xF7 (48-247)
    if (raster < 0x30 || raster > 0xF7) return false;

    // Check Y scroll alignment
    if ((raster & 0x07) != fineYScroll(raster)) return false;

    return true;
}

void Vic::drawSprite(int raster, int rowInSprite, int sprIndex)
{
    int spriteX = spriteScreenXFor(sprIndex, raster);

    bool expandX = registers.spriteXExpansion & (1 << sprIndex);
    bool multClr = registers.spriteMultiColor & (1 << sprIndex);

    uint16_t dataAddr = getSpriteDataAddress(sprIndex);
    uint8_t col = registers.spriteColors[sprIndex] & 0x0F;
    uint8_t mc1 = registers.spriteMultiColor1 & 0x0F;
    uint8_t mc2 = registers.spriteMultiColor2 & 0x0F;

    uint32_t rowBits = (uint32_t)mem->vicRead(dataAddr + rowInSprite*3, raster) << 16 |
                       (uint32_t)mem->vicRead(dataAddr + rowInSprite*3 + 1, raster) <<  8 |
                       (uint32_t)mem->vicRead(dataAddr + rowInSprite*3 + 2, raster);

    int x0, x1;
    innerWindowForRaster(raster, x0, x1);

    const int screenY = fbY(raster);

    auto insideX = [&](int x){ return x >= x0 && x < x1; };

    if (!multClr)
    {
        int xDup = expandX ? 2 : 1;
        for (int colBit = 0; colBit < 24; ++colBit)
        {
            if (!(rowBits & (1 << (23 - colBit)))) continue;
            int drawX = spriteX + colBit * xDup;
            if (drawX >= x1) break;
            if (drawX + (xDup - 1) < x0) continue;
            for (int xx = 0; xx < xDup; ++xx)
            {
                int px = drawX + xx;
                if (insideX(px)) IO_adapter->setPixel(px, screenY, col);
            }
        }
    }
    else
    {
        const int baseW = 2;
        int xDup = (expandX ? 2 : 1) * baseW;
        for (int pair = 0; pair < 12; ++pair)
        {
            uint8_t bits = (rowBits >> (22 - pair*2)) & 0x03;
            if (!bits) continue;
            int drawX = spriteX + pair * xDup;
            if (drawX >= x1) break;
            if (drawX + xDup - 1 < x0) continue;
            uint8_t pixCol = (bits == 1) ? mc1 : (bits == 2) ? col : mc2;
            for (int xx = 0; xx < xDup; ++xx)
            {
                int px = drawX + xx;
                if (insideX(px)) IO_adapter->setPixel(px, screenY, pixCol);
            }
        }
    }
}

void Vic::renderSprites(int pass, int raster)
{
    for (int i = 0; i < 8; ++i)
    {
        if (!(registers.spriteEnabled & (1 << i)))
        {
            continue; // Sprite disabled
        }

        int spriteY = registers.spriteY[i];

        // Check for expanded sprite
        int spriteHeight = (registers.spriteYExpansion & (1 << i)) ? 42 : 21;

        if (raster < spriteY || raster >= spriteY + spriteHeight)
        {
            continue;
        }

        int rowInSprite = raster - spriteY;
        if (spriteHeight == 42)
        {
            rowInSprite /= 2;
        }
        bool behind = registers.spritePriority & (1 << i);
        if ((pass == 0 && behind) || (pass == 1 && !behind))
        {
            drawSprite(raster, rowInSprite, i);
        }
    }
}

void Vic::renderLine(int raster)
{
    updateGraphicsMode(raster);

    // Clear bgOpaque for the *framebuffer Y* line
    int screenY = fbY(raster);
    if (screenY >= 0 && screenY < (int)bgOpaque.size())
    for (int x = 0; x < 512; ++x) bgOpaque[screenY][x] = 0;

    int x0, x1;
    innerWindowForRaster(raster, x0, x1);

    IO_adapter->renderBorderLine(raster, registers.borderColor, x0, x1);

    const bool DEN = (d011_per_raster[raster] & 0x10) != 0;   // <-- latched

    if (!DEN)
    {
        IO_adapter->renderBackgroundLine(raster, registers.borderColor, x0, x1);
        renderSprites(0, raster);
        renderSprites(1, raster);
        return;
    }

    if (!(currentMode == graphicsMode::bitmap || currentMode == graphicsMode::multiColorBitmap))
    {
        // Inner window background: border color when DEN=0, otherwise BG color
        IO_adapter->renderBackgroundLine(raster, DEN ? registers.backgroundColor0 : registers.borderColor, x0, x1);
    }

    if (DEN)
    {
        const int lineXScroll = fineXScroll(raster);
        switch (currentMode)
        {
            case graphicsMode::standard:
            case graphicsMode::multiColor:
                renderTextLine(raster, lineXScroll);
                break;
            case graphicsMode::bitmap:
                renderBitmapLine(raster, lineXScroll);
                break;
            case graphicsMode::multiColorBitmap:
                renderBitmapMulticolorLine(raster, lineXScroll);
                break;
            case graphicsMode::extendedColorText:
                renderECMLine(raster, lineXScroll);
                break;
            default:
                break;
        }
    }

    // Sprites are visible regardless of DEN
    renderSprites(0, raster);

    renderSprites(1, raster);
}

void Vic::renderTextLine(int raster, int xScroll)
{
    int rows = getRSEL(raster) ? 25 : 24;
    int cols = getCSEL(raster) ? 40 : 38;
    int firstVis = cfg_->firstVisibleLine;

    int charRow = (raster - firstVis) >> 3;
    if (charRow >= rows) return;

    int yInChar = rowCounter;
    int fine    = xScroll & 7;
    int fetchCols = cols + (fine ? 1 : 0);

    int x0 = BORDER_SIZE + (cols == 38 ? 4 : 0);
    int x1 = x0 + cols * 8;

    int xStart = x0 - fine;

    int py = fbY(raster);
    if (charRow < 0) return;

    for (int col = 0; col < fetchCols; ++col)
    {
        int px = xStart + col * 8;
        if (px >= x1) break;
        if (px + 8 <= x0) continue;

        uint8_t scrByte, colorByte;
        if (col < 40)
        {
            scrByte  = charPtrFIFO[col];
            colorByte = colorPtrFIFO[col];
        }
        else if (col == 40)
        {
            scrByte  = fetchScreenByte(charRow, 40, raster);
            colorByte = fetchColorByte(charRow, 40, raster) & 0x0F;
        }
        else break;

        uint8_t fgColor = colorByte & 0x0F;
        uint8_t bgColor = registers.backgroundColor0;

        bool mcMode = (d016_per_raster[raster] & 0x10) && (fgColor & 0x08);

        if (!mcMode)
            renderChar(scrByte, px, py, fgColor, bgColor, yInChar, raster, x0, x1);
        else
            renderCharMultiColor(scrByte, px, py, colorByte & 0x07, bgColor, yInChar, raster, x0, x1);
    }
}

void Vic::renderBitmapLine(int raster, int xScroll)
{
    int rows = getRSEL(raster) ? 25 : 24;
    int cols = getCSEL(raster) ? 40 : 38;
    int firstVis = cfg_->firstVisibleLine;

    int charRow = (raster - firstVis) >> 3;
    if (charRow >= rows) return;

    int bitmapY = raster - firstVis;
    const uint16_t bitmapBase = getBitmapBase(raster);

    int fine = xScroll & 7;
    int fetchCols = cols + (fine ? 1 : 0);
    int x0 = BORDER_SIZE + (cols == 38 ? 4 : 0);
    int x1 = x0 + cols * 8;
    int xStart = x0 - fine;
    int py = fbY(raster);

    if (charRow < 0) return;

    for (int col = 0; col < fetchCols; ++col)
    {
        uint16_t byteOffset = (bitmapY & 7) + (col * 8) + ((bitmapY >> 3) * 320);
        uint8_t byte = mem->vicRead(bitmapBase + byteOffset, raster);

        uint8_t scr = fetchScreenByte(charRow, col, raster);
        uint8_t fgColor = (scr >> 4) & 0x0F;
        uint8_t bgColor = scr & 0x0F;

        int cellLeft = xStart + col * 8;
        if (cellLeft >= x1) break;
        if (cellLeft + 8 <= x0) continue;

        for (int bit = 0; bit < 8; ++bit)
        {
            bool pixelOn = (byte >> (7 - bit)) & 0x01;
            uint8_t color = pixelOn ? fgColor : bgColor;

            int pxRaw = cellLeft + bit;
            if (pxRaw < x0 || pxRaw >= x1) continue;

            IO_adapter->setPixel(pxRaw, py, color);

            if (pixelOn) markBGOpaque(fbY(raster), pxRaw);
        }
    }
}

void Vic::renderBitmapMulticolorLine(int raster, int xScroll)
{
    int rows = getRSEL(raster) ? 25 : 24;
    int cols = getCSEL(raster) ? 40 : 38;
    int firstVis = cfg_->firstVisibleLine;

    int charRow = (raster - firstVis) >> 3;
    if (charRow >= rows) return;

    int bitmapY = raster - firstVis;
    const uint16_t bitmapBase = getBitmapBase(raster);

    int fine = xScroll & 7;
    int fetchCols = cols + (fine ? 1 : 0);
    int x0 = BORDER_SIZE + (cols == 38 ? 4 : 0);
    int x1 = x0 + cols * 8;
    int xStart = x0 - fine;
    int py = fbY(raster);

    if (charRow < 0) return;

    for (int col = 0; col < fetchCols; ++col)
    {
        uint16_t byteOffset = (bitmapY & 7) + (col * 8) + ((bitmapY >> 3) * 320);
        uint8_t byte = mem->vicRead(bitmapBase + byteOffset, raster);

        uint8_t scr = fetchScreenByte(charRow, col, raster);
        uint8_t colNib = fetchColorByte(charRow, col, raster) & 0x0F;
        uint8_t bg0 = registers.backgroundColor0;

        int cellLeft = xStart + col * 8;
        if (cellLeft >= x1) break;
        if (cellLeft + 8 <= x0) continue;

        for (int pair = 0; pair < 4; ++pair)
        {
            uint8_t bits = (byte >> (6 - pair * 2)) & 0x03;
            uint8_t color = (bits == 0) ? bg0 : (bits == 1) ? ((scr >> 4) & 0x0F) : (bits == 2) ? (scr & 0x0F) : colNib;

            int pxRaw = cellLeft + pair * 2;
            if (pxRaw < x0 || pxRaw + 1 >= x1) continue;

            IO_adapter->setPixel(pxRaw, py, color);
            IO_adapter->setPixel(pxRaw + 1, py, color);

            if (bits != 0)
            {
                    markBGOpaque(fbY(raster), pxRaw);
                    markBGOpaque(fbY(raster), pxRaw + 1);
                }
            }
        }
}

void Vic::renderECMLine(int raster, int xScroll)
{
    int rows = getRSEL(raster) ? 25 : 24;
    int cols = getCSEL(raster) ? 40 : 38;
    int firstVis = cfg_->firstVisibleLine;

    int charRow = (raster - firstVis) >> 3;
    if (charRow >= rows) return;

    int yInChar = rowCounter;
    int fine = xScroll & 7;
    int fetchCols = cols + (fine ? 1 : 0);
    int x0 = BORDER_SIZE + (cols == 38 ? 4 : 0);
    int x1 = x0 + cols * 8;
    int xStart = x0 - fine;
    int py = fbY(raster);

    if (charRow < 0) return;

    for (int col = 0; col < fetchCols; ++col)
    {
        uint8_t scrByte, colorByte;
        if (col < 40)
        {
            scrByte  = charPtrFIFO[col];
            colorByte = colorPtrFIFO[col];
        }
        else if (col == 40)
        {
            scrByte  = fetchScreenByte(charRow, 40, raster);
            colorByte = fetchColorByte(charRow, 40, raster) & 0x0F;
        }
        else break;

        uint8_t charIndex = scrByte & 0x3F;
        uint8_t bgSel = (scrByte >> 6) & 0x03;
        uint8_t bgColor =
            (bgSel == 0) ? registers.backgroundColor0 :
            (bgSel == 1) ? getBackgroundColor(0) :
            (bgSel == 2) ? getBackgroundColor(1) :
                           getBackgroundColor(2);

        uint8_t fgColor = colorByte & 0x0F;

        int pxCell = xStart + col * 8;
        if (pxCell >= x1) break;
        if (pxCell + 8 <= x0) continue;

        uint16_t addr = getCHARBase(raster) + charIndex * 8;
        uint8_t row = mem->vicRead(addr + yInChar, raster);

        for (int bit = 0; bit < 8; ++bit)
        {
            bool pixelOn = (row >> (7 - bit)) & 0x01;
            uint8_t color = pixelOn ? fgColor : bgColor;

            int pxRaw = pxCell + bit;
            if (pxRaw < x0 || pxRaw >= x1) continue;

            IO_adapter->setPixel(pxRaw, py, color);

            if (pixelOn) markBGOpaque(fbY(raster), pxRaw);
        }
    }
}

void Vic::detectSpriteToSpriteCollision(int raster)
{
    uint8_t old = registers.spriteCollision;

    for (int i = 0; i < 8; ++i)
    {
        if (!(registers.spriteEnabled & (1 << i))) continue;
        for (int j = i + 1; j < 8; ++j)
        {
            if (!(registers.spriteEnabled & (1 << j))) continue;
            if (checkSpriteSpriteOverlapOnLine(i, j, raster))
                registers.spriteCollision |= (1 << i) | (1 << j);
        }
    }

    if ((registers.spriteCollision & ~old) && (registers.interruptEnable & 0x02) && IRQ)
    {
        registers.interruptStatus |= 0x02;
        IRQ->raiseIRQ(IRQ->VICII);
    }
}

bool Vic::checkSpriteSpriteOverlapOnLine(int A, int B, int raster)
{
    int ra, rb, fbLine;
    if (!spriteCoversRaster(A, raster, ra, fbLine)) return false;
    if (!spriteCoversRaster(B, raster, rb, fbLine)) return false;

    // On-screen X exactly like drawSprite()
    int xA = spriteScreenXFor(A, raster);
    int xB = spriteScreenXFor(B, raster);

    bool expA = (registers.spriteXExpansion & (1 << A)) != 0;
    bool expB = (registers.spriteXExpansion & (1 << B)) != 0;
    bool mcA  = (registers.spriteMultiColor  & (1 << A)) != 0;
    bool mcB  = (registers.spriteMultiColor  & (1 << B)) != 0;

    auto rowBitsFor = [&](int i, int row)
    {
        uint16_t addr = getSpriteDataAddress(i);
        return  (uint32_t)mem->vicRead(addr + row*3, raster)     << 16
              | (uint32_t)mem->vicRead(addr + row*3 + 1, raster) <<  8
              | (uint32_t)mem->vicRead(addr + row*3 + 2, raster);
    };

    uint32_t bitsA = rowBitsFor(A, ra);
    uint32_t bitsB = rowBitsFor(B, rb);

    int x0, x1;
    innerWindowForRaster(raster, x0, x1);

    auto insideX = [&](int x){ return x >= x0 && x < x1; };

    // Walk solid host pixels for one sprite row
    auto forEachSolidPixelX = [&](uint32_t bits, bool mult, bool exp, int baseX, auto &&emit)
    {
        if (!mult)
        {
            int xDup = exp ? 2 : 1;
            for (int b = 0; b < 24; ++b)
            {
                if ((bits & (1u << (23 - b))) == 0) continue;
                int drawX = baseX + b * xDup;
                if (drawX >= x1) break;
                for (int xx = 0; xx < xDup; ++xx)
                {
                    int px = drawX + xx;
                    if (insideX(px)) emit(px);
                }
            }
        }
        else
        {
            const int baseW = 2;
            int xDup = (exp ? 2 : 1) * baseW;
            for (int p = 0; p < 12; ++p)
            {
                uint8_t pair = (bits >> (22 - p*2)) & 0x03;
                if (!pair) continue;
                int drawX = baseX + p * xDup;
                if (drawX >= x1) break;
                for (int xx = 0; xx < xDup; ++xx)
                {
                    int px = drawX + xx;
                    if (insideX(px)) emit(px);
                }
            }
        }
    };

    // Bitmask of covered BG pixels for sprite A
    static uint8_t cover[512];
    std::memset(cover, 0, sizeof cover);

    forEachSolidPixelX(bitsA, mcA, expA, xA, [&](int px)
    {
        if (px >= 0 && px < (int)sizeof(cover)) cover[px] = 1;
    });

    bool hit = false;
    forEachSolidPixelX(bitsB, mcB, expB, xB, [&](int px)
    {
        if (px >= 0 && px < (int)sizeof(cover) && cover[px]) hit = true;
    });

    return hit;
}

void Vic::detectSpriteToBackgroundCollision(int raster)
{
    uint8_t old = registers.spriteDataCollision;

    for (int i = 0; i < 8; ++i)
    {
        if (!(registers.spriteEnabled & (1 << i))) continue;
        if (checkSpriteBackgroundOverlap(i, raster)) registers.spriteDataCollision |= (1 << i);
    }

    if ((registers.spriteDataCollision & ~old) && (registers.interruptEnable & 0x04) && IRQ)
    {
        registers.interruptStatus |= 0x04;
        IRQ->raiseIRQ(IRQ->VICII);
    }
}

bool Vic::checkSpriteBackgroundOverlap(int spriteIndex, int raster)
{
    int rowInSprite, fbLine;
    if (!spriteCoversRaster(spriteIndex, raster, rowInSprite, fbLine))
        return false;

    const int spriteX = spriteScreenXFor(spriteIndex, raster);

    const bool expandX = (registers.spriteXExpansion & (1 << spriteIndex)) != 0;
    const bool multClr = (registers.spriteMultiColor  & (1 << spriteIndex)) != 0;

    // Fetch sprite row bits
    const uint16_t dataAddr = getSpriteDataAddress(spriteIndex);
    const uint32_t rowBits =
        (uint32_t)mem->vicRead(dataAddr + rowInSprite * 3, raster) << 16 |
        (uint32_t)mem->vicRead(dataAddr + rowInSprite * 3 + 1, raster) <<  8 |
        (uint32_t)mem->vicRead(dataAddr + rowInSprite * 3 + 2, raster);

    // Paint window for this raster (fine-scroll aware)
    const int cols = getCSEL(raster) ? 40 : 38;
    const int fine = d016_per_raster[raster] & 0x07;
    const int x0   = BORDER_SIZE + (cols == 38 ? 4 : 0);
    const int leftPaintX  = x0 - fine;
    const int rightPaintX = x0 + cols * 8; // exclusive

    auto hitsBG = [&](int px) -> bool {
        // Only consider pixels where we actually painted/marked background this raster
        if (px < leftPaintX || px >= rightPaintX) return false;
        return isBackgroundPixelOpaque(px, fbLine);
    };

    if (!multClr)
    {
        const int xDup = expandX ? 2 : 1;
        for (int b = 0; b < 24; ++b)
        {
            if ((rowBits & (1u << (23 - b))) == 0) continue;

            const int drawX = spriteX + b * xDup;
            if (drawX >= rightPaintX + 8) break;     // safety
            if (drawX + (xDup - 1) < leftPaintX - 8) continue;

            for (int xx = 0; xx < xDup; ++xx)
            {
                const int px = drawX + xx;
                if (hitsBG(px)) return true;
            }
        }
    }
    else
    {
        const int baseW = 2;
        const int xDup  = (expandX ? 2 : 1) * baseW;

        for (int p = 0; p < 12; ++p)
        {
            const uint8_t bits = (rowBits >> (22 - p * 2)) & 0x03;
            if (!bits) continue;

            const int drawX = spriteX + p * xDup;
            if (drawX >= rightPaintX + xDup) break;
            if (drawX + (xDup - 1) < leftPaintX - xDup) continue;

            for (int xx = 0; xx < xDup; ++xx)
            {
                const int px = drawX + xx;
                if (hitsBG(px)) return true;
            }
        }
    }

    return false;
}

int Vic::spriteScreenXFor(int sprIndex, int raster) const
{
    int x = registers.spriteX[sprIndex];
    if (registers.spriteX_MSB & (1 << sprIndex))
        x += 256;

    // Apply VIC-II hardware offset + border
    return (x - cfg_->hardware_X) + BORDER_SIZE;
}

bool Vic::spriteCoversRaster(int sprIndex, int raster, int &rowInSprite, int &fbLine) const
{
    int y = registers.spriteY[sprIndex];
    int h = (registers.spriteYExpansion & (1 << sprIndex)) ? 42 : 21;

    if (raster < y || raster >= y + h) return false;

    rowInSprite = raster - y;
    if (h == 42) rowInSprite /= 2;

    fbLine = fbY(raster);

    return true;
}

bool Vic::isBackgroundPixelOpaque(int x, int y)
{
    if (y < 0 || y >= (int)bgOpaque.size()) return false;
    if (x < 0 || x >= (int)bgOpaque[y].size()) return false;
    return bgOpaque[y][x] != 0;
}

void Vic::updateGraphicsMode(int raster)
{
    bool MCM = (d016_per_raster[raster] & 0x10) != 0;
    bool BMM = (d011_per_raster[raster] & 0x20) != 0;
    bool ECM = (d011_per_raster[raster] & 0x40) != 0;

    if (!BMM && !MCM && !ECM)
    {
        currentMode = graphicsMode::standard;
    }
    else if (!BMM && MCM && !ECM)
    {
        currentMode = graphicsMode::multiColor;
    }
    else if (!BMM && !MCM && ECM)
    {
        currentMode = graphicsMode::extendedColorText;
    }
    else if (BMM && !MCM)
    {
        currentMode = graphicsMode::bitmap;
    }
    else if (BMM && MCM)
    {
        currentMode = graphicsMode::multiColorBitmap;
    }
    else
    {
        currentMode = graphicsMode::invalid;
    }
}

void Vic::innerWindowForRaster(int raster, int& x0, int& x1) const
{
    const int cols = getCSEL(raster) ? 40 : 38;
    x0 = BORDER_SIZE + (cols == 38 ? 4 : 0);
    x1 = x0 + cols * 8;
}

void Vic::renderChar(uint8_t c, int x, int y, uint8_t fg, uint8_t bg, int yInChar, int raster, int x0, int x1)
{
    uint16_t addr = getCHARBase(raster) + c * 8;
    uint8_t row = mem->vicRead(addr + yInChar, raster);

    for (int col = 0; col < 8; ++col)
    {
        int pxRaw = x + col;
        if (pxRaw < x0 || pxRaw >= x1) continue;

        bool bit = (row >> (7 - col)) & 0x01;
        uint8_t color = bit ? (fg & 0x0F) : (bg & 0x0F);

        // Draw to framebuffer
        IO_adapter->setPixel(pxRaw, y, color);

        if (bit) markBGOpaque(fbY(raster), pxRaw);
    }
}

void Vic::renderCharMultiColor(uint8_t c, int x, int y, uint8_t cellCol, uint8_t bg, int yInChar, int raster, int x0, int x1)
{
    uint16_t addr = getCHARBase(raster) + c * 8;
    uint8_t  row  = mem->vicRead(addr + yInChar, raster);

    uint8_t bg1 = registers.backgroundColor[0] & 0x0F;
    uint8_t bg2 = registers.backgroundColor[1] & 0x0F;
    uint8_t colRAM = (uint8_t)(cellCol) & 0x07;

    for (int pair = 0; pair < 4; ++pair)
    {
        uint8_t bits = (row >> ((3 - pair) * 2)) & 0x03;
        uint8_t col  =
            (bits == 0) ? (bg & 0x0F) :
            (bits == 1) ? bg1 :
            (bits == 2) ? bg2 :
                          colRAM;

        const int p0 = x + pair * 2;
        const int p1 = p0 + 1;

        if (p0 >= x1) break;
        if (p1 <  x0) continue;

        // Per-pixel clipping + marking
        if (p0 >= x0 && p0 < x1) {
            IO_adapter->setPixel(p0, y, col);
            if (bits != 0) markBGOpaque(fbY(raster), p0);
        }
        if (p1 >= x0 && p1 < x1) {
            IO_adapter->setPixel(p1, y, col);
            if (bits != 0) markBGOpaque(fbY(raster), p1);
        }
    }
}

uint8_t Vic::fetchScreenByte(int row, int col, int raster) const
{
    const int cols = getCSEL(raster) ? 40 : 38;
    uint16_t pageOffset = getScreenBase(raster);

    // VIC memory layout is always 40 chars wide
    int memCol = col;

    // In 38-col mode, visible columns 0..37 map to screen mem 1..38
    if (cols == 38)
    {
        memCol = col + 1;
    }

    uint16_t cellOffset = row * 40 + memCol;
    uint16_t address = pageOffset + cellOffset;
    return mem->vicRead(address, raster);
}

uint8_t Vic::fetchColorByte(int row, int col, int raster) const
{
    const int cols = getCSEL(raster) ? 40 : 38;

    int memCol = col;
    if (cols == 38)
    {
        memCol = col + 1;
    }

    uint16_t address = COLOR_MEMORY_START + row * 40 + memCol;
    return mem->read(address);
}

bool Vic::spriteDMANeededThisLine() const
{
    for (int i = 0; i < 8; ++i)
    {
        if (!(registers.spriteEnabled & (1 << i)))
            continue;

        int rowInSprite, fbLine;
        if (spriteCoversRaster(i, registers.raster, rowInSprite, fbLine) && rowInSprite == 0)
            return true;
    }
    return false;
}

void Vic::markBGOpaque(int screenY, int px)
{
    if (screenY >= 0 && screenY < (int)bgOpaque.size() &&
        px >= 0 && px < 512)
    {
        bgOpaque[screenY][px] = 1;
    }
}

std::string Vic::decodeModeName() const
{
    bool ecm = d011_per_raster[registers.raster] & 0x40;
    bool bmm = d011_per_raster[registers.raster] & 0x20;
    bool mcm = d016_per_raster[registers.raster] & 0x10;

    if (!bmm && !mcm && !ecm) return "Text";
    if (ecm && !bmm && !mcm) return "ECM (Extended Color Mode)";
    if (!bmm && mcm) return "Multicolor Text";
    if (bmm && !mcm) return "Bitmap";
    if (bmm && mcm)  return "Multicolor Bitmap";
    return "Unknown";
}

std::string Vic::getVICBanks() const
{
    std::stringstream out;
    out << std::hex << std::uppercase << std::setfill('0');

    // Current VIC bank base (CIA2 16 KB window)
    uint16_t bankBase = cia2object->getCurrentVICBank();

    out << "Active VIC Bank = " << (bankBase >> 14)
        << " ($" << std::setw(4) << bankBase
        << "-$" << std::setw(4) << (bankBase + 0x3FFF) << ")\n\n";

    uint16_t charOffset   = getCHARBase(registers.raster);
    uint16_t screenOffset = getScreenBase(registers.raster);
    uint16_t bitmapOffset = getBitmapBase(registers.raster);

    out << "CHAR Base   = offset $"   << std::setw(4) << charOffset
        << "  ->  address $" << std::setw(4) << (bankBase + charOffset) << "\n";

    out << "Screen Base = offset $" << std::setw(4) << screenOffset
        << "  ->  address $" << std::setw(4) << (bankBase + screenOffset) << "\n";

    out << "Bitmap Base = offset $" << std::setw(4) << bitmapOffset
        << "  ->  address $" << std::setw(4) << (bankBase + bitmapOffset) << "\n";

    return out.str();
}

std::string Vic::dumpRegisters(const std::string& group) const
{

    // Color lookup for colors section
    static const char* C64ColorNames[16] =
    {
        "Black","White","Red","Cyan","Purple","Green","Blue","Yellow",
        "Orange","Brown","Light Red","Dark Grey","Grey","Light Green","Light Blue","Light Grey"
    };

    uint16_t currentRaster = registers.raster;

    std::stringstream out;
    out << std::hex << std::uppercase << std::setfill('0');

    // Dump the regs

    // Raster and Control
    if (group == "all" || group == "raster")
    {
        out << "Raster and Control Registers:\n\n";

        out << "D011 = $" << std::setw(2) << static_cast<int>(registers.control) << "   (CTRL1: YSCROLL = " << (registers.control & 0x07)
            << ", 25row = " << ((registers.control & 0x08) ? "Yes" : "No") << ", Screen = " << ((registers.control & 0x10) ? "On" : "Off")
            << ", Bitmap = " << ((registers.control & 0x20) ? "Yes" : "No") << ", ECM = " << ((registers.control & 0x40) ? "Yes" : "No")
            << ", RasterHi = " << ((registers.control & 0x80) ? "1" : "0") << ")\n";

        out << "D012 = $" << std::setw(2) << registers.raster << "   (RASTER = " << std::dec << currentRaster << ")\n";

        out << "D016 = $" << std::setw(2) << std::hex << static_cast<int>(registers.control2) << "   (CTRL2: XSCROLL = " << (registers.control2 & 0x07)
            << ", 40COL = " << ((registers.control2 & 0x08) ? "Yes" : "No") << ", Multicolor = " << ((registers.control2 & 0x10) ? "Yes" : "No")
            << ",  HIRES = " << ((registers.control2 & 0x10) ? "No" : "Yes") << ")\n";

        out << "D018 = $" << std::setw(2) << static_cast<int>(registers.memory_pointer) << "   (MEM_PTR:   Screen = $" << std::setw(4)
            << screenBaseCache << ", CHAR = $" << std::setw(4) << charBaseCache << ", Bitmap = $"
            << std::setw(4) << bitmapBaseCache << ")\n";
    }

    // Interrupts
    if (group == "all" || group == "irq")
    {
        out << "\nInterrupt Registers:\n\n";

        out << "D019 = $" << std::setw(2) << static_cast<int>(registers.interruptStatus) << "   (RASTER IRQ: Raster = "
            << ((registers.interruptStatus & 0x01) ? "Yes" : "No") << ", SprSpr = " << ((registers.interruptStatus & 0x02) ? "Yes" : "No")
            << ", SprBg = " << ((registers.interruptStatus & 0x04) ? "Yes" : "No") << ", LightPen = " << ((registers.interruptStatus & 0x08) ? "Yes" : "No")
            << ", IRQ Occured = " << ((registers.interruptStatus & 0x80) ? "Yes" : "No") << ")\n";

        out << "D01A = $" << std::setw(2) << static_cast<int>(registers.interruptEnable) << "   (IRQ Enable: Raster = "
            << ((registers.interruptEnable & 0x01) ? "Yes" : "No") << ", SprSpr = " << ((registers.interruptEnable & 0x02) ? "Yes" : "No")
            << ", SprBg = " << ((registers.interruptEnable & 0x04) ? "Yes" : "No") << ", LightPen = " << ((registers.interruptEnable & 0x08) ? "Yes" : "No")
            << ")\n";
    }

    // Sprite control
    if (group == "all" || group == "sprites")
    {
        out << "\nSprite Control Registers:\n\n";

        out << "D015 = $" << std::setw(2) << static_cast<int>(registers.spriteEnabled) << " (Enable Mask: " << std::bitset<8> (registers.spriteEnabled)
            << ")\n";

        out << "D017 = $" << std::setw(2) << static_cast<int>(registers.spriteYExpansion) << " (Y-Expand: " << std::bitset<8>(registers.spriteYExpansion)
            << ")\n";

        out << "D01B = $" << std::setw(2) << static_cast<int>(registers.spritePriority) << " (Sprite Priority: " << std::bitset<8>(registers.spritePriority)
            << ")\n";

        out << "D01C = $" << std::setw(2) << static_cast<int>(registers.spriteMultiColor) << " (Sprite Multicolor: "
            << std::bitset<8>(registers.spriteMultiColor) << ")\n";

        out << "D01D = $" << std::setw(2) << static_cast<int>(registers.spriteXExpansion) << " (Sprite X Expansion: "
            << std::bitset<8>(registers.spriteXExpansion) << ")\n";
    }

    // Sprite collision
    if (group == "all" || group == "collisions")
    {
        out << "\nSprite Collision Registers\n\n";

        out << "D01E $" << std::setw(2) << static_cast<int>(registers.spriteCollision) << " (Sprite to Sprite Collision: "
            << std::bitset<8>(registers.spriteCollision) << ")\n";

        out << "D01F $" << std::setw(2) << static_cast<int>(registers.spriteDataCollision) << " (Sprite to Background Collsion " <<
            std::bitset<8>(registers.spriteDataCollision) << ")\n";
    }

    // Color registers
    if (group == "all" || group == "colors")
    {
        out << "\nColor Registers:\n\n";

        auto printColor = [&](const char* label, uint8_t value)
        {
            int idx = value & 0x0F; // only low 4 bits used
            out << label << " = $" << std::setw(2) << static_cast<int>(value)
                << "   (" << C64ColorNames[idx] << ")\n";
        };

        printColor("D020 (Border Color)", registers.borderColor);
        printColor("D021 (Background 0)", registers.backgroundColor0);
        printColor("D022 (Background 1)", registers.backgroundColor[0]);
        printColor("D023 (Background 2)", registers.backgroundColor[1]);
        printColor("D024 (Background 3)", registers.backgroundColor[2]);
        printColor("D025 (Sprite Multicolor 0)", registers.spriteMultiColor1);
        printColor("D026 (Sprite Multicolor 1)", registers.spriteMultiColor2);

        // Per-sprite colors
        for (int i = 0; i < 8; i++)
        {
            std::ostringstream label;
            label << "D0" << std::hex << std::uppercase << (27+i)
                  << " (Sprite " << i << " Color)";
            printColor(label.str().c_str(), registers.spriteColors[i]);
        }
    }

    // Sprite position
    if (group == "all" || group == "pos")
    {
        out << "\nSprite Position Registers:\n\n";

        for (int i = 0; i < 8; i++)
        {
            // X coordinate is 9 bits: low 8 from spriteX[i], high bit from spriteX_MSB
            int x = registers.spriteX[i] | ((registers.spriteX_MSB >> i) & 0x01) << 8;
            int y = registers.spriteY[i];

            out << "Sprite " << i
                << " -> X=$" << std::setw(3) << x
                << " (" << std::dec << x << "), "
                << "Y=$" << std::setw(3) << y
                << " (" << std::dec << y << ")\n";
        }
    }

    return out.str();
}

void Vic::updateMonitorCaches(int raster)
{
    // Cache the current bank
    currentVICBank = cia2object->getCurrentVICBank();

    // Build the full address
    charBaseCache = getCHARBase(raster) + currentVICBank;
    screenBaseCache = getScreenBase(raster) + currentVICBank;
    bitmapBaseCache = getBitmapBase(raster) + currentVICBank;
}
