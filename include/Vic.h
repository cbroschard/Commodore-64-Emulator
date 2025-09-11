// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef VIC_H
#define VIC_H

#include <algorithm>
#include <cstdint>
#include "common.h"
#include "CIA2.h"
#include "CPU.h"
#include "IO.h"
#include "IRQLine.h"
#include "Logging.h"
#include "Memory.h"

struct ModeConfig
{
    uint16_t maxRasterLines;
    uint8_t  cyclesPerLine;
    uint8_t  frameRate;
    uint16_t vblankStartLine;
    uint16_t vblankEndLine;
    int      visibleLines;
    int      badLineCycles;
    int      firstVisibleLine;
    int      lastVisibleLine;
    int      DMAStartCycle;
    int      DMAEndCycle;
    int      hardware_X;
};

static constexpr ModeConfig NTSC_CONFIG =
{
    262,   // maxRasterLines
    65,   // cyclesPerLine
    60,   // frameRate
    251,   // vblankStartLine
    21,   // vblankEndLine
    200,   // visibleLines
    40,   // badLineCycles
    51,   // firstVisibleLine
    250,   // lastVisibleLine
    15,    // DMAStartCycle
    54,     // DMAEndCycle
    24     // hardwareX
};

static constexpr ModeConfig PAL_CONFIG =
{
    312,   // maxRasterLines
    63,   // cyclesPerLine
    50,   // frameRate
    251,   // vblankStartLine
    50,   // vblankEndLine
    200,   // visibleLines
    40,   // badLineCycles
    51,   // firstVisibleLine
    250,   // lastVisibleLine
    14,    // DMAStartCycle
    53,     // DMAEndCycle
    31      // hardwareX
};

class Vic
{
    public:
        Vic(VideoMode mode = VideoMode::NTSC);
        virtual ~Vic();

        // Pointer functions
        inline void attachIOInstance(IO* IO_adapter) { this->IO_adapter = IO_adapter; }
        inline void attachCPUInstance(CPU* processor) { this->processor = processor; }
        inline void attachMemoryInstance(Memory* mem) { this->mem = mem; }
        inline void attachCIA2Instance(CIA2* cia2object) { this->cia2object = cia2object; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachIRQLineInstance(IRQLine* IRQ) { this->IRQ = IRQ; }

        // Setter for video mode
        void setMode(VideoMode mode);

        // Register read/write
        void writeRegister(uint16_t address, uint8_t value);
        uint8_t readRegister(uint16_t address);

        // Tick method for cycle accurate updating of the raster, etc
        void tick(int cycles);

        // Getter for AEC status
        inline bool getAEC() const { return AEC; }

        // Graphics mode determination
        enum class graphicsMode
        {
            standard,
            multiColor,
            bitmap,
            multiColorBitmap,
            extendedColorText,
            invalid
        };

        // Getter for current graphics mode
        inline graphicsMode getCurrentGraphicsMode() const {  return currentMode; }

        // Reset to power on defaults
        void reset();

        // Rendering of all screens
        void renderLine(int raster);

        inline bool isFrameDone() const { return frameDone; }
        inline void clearFrameFlag() { frameDone = false; }

        inline bool getRSEL(int raster) const { return (d011_per_raster[raster] & 0x08) != 0; }
        inline bool getCSEL(int raster) const { return (d016_per_raster[raster] & 0x08) != 0; }

        // Getters for current memory locations
        inline uint16_t getCHARBase(int raster) const { return ((d018_per_raster[raster] >> 1) & 0x07) * 0x0800; }
        inline uint16_t getScreenBase(int raster) const { return ((d018_per_raster[raster] & 0xF0) << 6); }
        inline uint16_t getBitmapBase(int raster) const { return ((d018_per_raster[raster] >> 3) & 0x01) * 0x2000; }

        // ML Monitor helpers
        std::string decodeModeName() const;
        std::string getVICBanks() const;
        std::string dumpRegisters(const std::string& group) const;
        uint8_t getCurrentRaster() { return registers.raster; }

    protected:

    private:

        // Non-owning pointers
        IO* IO_adapter = nullptr;
        CPU* processor = nullptr;
        Memory* mem = nullptr;
        CIA2* cia2object = nullptr;
        Logging* logger = nullptr;
        IRQLine* IRQ = nullptr;

        static const uint16_t COLOR_MEMORY_START = 0xD800;

        // Screen constants
        static constexpr int BORDER_SIZE = 32;

        // Video Mode configuration at runtime (NTSC or PAL)
        VideoMode mode_;
        const ModeConfig* cfg_;

        // tick 40-byte character/color fetch during badline
        uint8_t charPtrFIFO[40];
        uint8_t colorPtrFIFO[40];
        uint8_t rowCounter;
        int currentScreenRow;

        // Keep track of frame completion
        bool frameDone;

        struct Registers
        {
            uint8_t spriteX[8];                 // SPRITE_X0 to SPRITE_X7 (0xD000, 0xD002, ...)
            uint8_t spriteY[8];                 // SPRITE_Y0 to SPRITE_Y7 (0xD001, 0xD003, ...)
            uint8_t spriteX_MSB;                // 0xd010
            uint8_t control;                    // Raster control (0xD011)
            uint16_t raster;                    // RASTER (0xD012)
            uint8_t light_pen_X;                // 0xD013
            uint8_t light_pen_Y;                // 0xD014
            uint8_t spriteEnabled;              // SPRITE_ENABLED (0xD015)
            uint8_t control2;                   // CONTROL_REGISTER_2 (0xD016)
            uint8_t spriteYExpansion;           // (0xD017)
            uint8_t memory_pointer;             // (0xD018)
            uint8_t interruptStatus;            // INTERRUPT_STATUS (0xD019)
            uint8_t interruptEnable;            // INTERRUPT_ENABLE (0xD01A)
            uint8_t spritePriority;             // 0xD01B
            uint8_t spriteMultiColor;           // 0xD01C
            uint8_t spriteXExpansion;           // 0xD01D
            uint8_t spriteCollision;            // 0xD01E
            uint8_t spriteDataCollision;        // 0xD01F
            uint8_t borderColor;                // Border Color (0xD020)
            uint8_t backgroundColor0;           // Background Color (0xD021)
            uint8_t backgroundColor[3];         // Background Color 1,2,3 (0xD022 to 0xD024)
            uint8_t spriteMultiColor1;          // Sprite Multi Color 1 (0xD025)
            uint8_t spriteMultiColor2;          // Sprite Multi Color 2 (0xD026)
            uint8_t spriteColors[8];            // 0xD027 to 0xD02E
            uint8_t undefined;                  // 0xD02F undefined register
            uint16_t rasterInterruptLine;       // Raster Interrupt Line
        } registers;

        // Per raster register latches
        std::vector<uint8_t> d011_per_raster;
        std::vector<uint8_t> d016_per_raster;
        std::vector<uint8_t> d018_per_raster;

        // Caches for ML Monitor
        uint16_t charBaseCache;
        uint16_t screenBaseCache;
        uint16_t bitmapBaseCache;
        uint16_t currentVICBank;

        // Sprite pointer latch
        uint16_t sprPtrBase[8];

        // Cache background opaque pixels
        std::vector<std::array<uint8_t, 512>> bgOpaque;

        // Multicolor helper for readRegister
        inline uint8_t getBackgroundColor(int value) const { return registers.backgroundColor[value]; }

        // fine-scroll helpers ($D016 bits 0-2 , $D011 bits 0-2)
        inline uint8_t fineXScroll(int raster) const { return d016_per_raster[raster] & 0x07; }
        inline uint8_t fineYScroll(int raster) const { return d011_per_raster[raster]  & 0x07; }

        // Read/Write register Helpers
        inline int getSpriteIndex(uint16_t address) const { return (address - 0xD000) / 2; }
        inline int getSpriteColorIndex(uint16_t address) const { return (address - 0xD027); }
        inline bool isSpriteX(uint16_t address) const { return ((address - 0xD000) % 2) == 0; }
        void markBGOpaque(int screenY, int px);

        // Render sprites
        void renderSprites(int pass, int raster);
        void drawSprite(int raster, int rowInSprite, int sprIndex);

        // Bad line detection
        bool isBadLine(int raster);

        // Address enable control
        void updateAEC();
        bool AEC;
        int currentCycle;

        // Sprite collision functions
        void detectSpriteToSpriteCollision(int raster);
        void detectSpriteToBackgroundCollision(int raster);
        bool checkSpriteBackgroundOverlap(int spriteIndex, int raster);
        bool checkSpriteSpriteOverlapOnLine(int A, int B, int raster);
        int spriteScreenXFor(int sprIndex, int raster) const;
        bool spriteCoversRaster(int sprIndex, int raster, int &rowInSprite, int &fbLine) const;
        bool isBackgroundPixelOpaque(int x, int y);
        inline uint16_t getSpriteDataAddress(int sprIndex) const { return sprPtrBase[sprIndex]; }

        // Ensure graphics mode updates
        graphicsMode currentMode;
        void updateGraphicsMode(int raster);

        // Line rendering
        void renderTextLine(int raster, int xScroll);
        void renderBitmapLine(int raster, int xScroll);
        void renderBitmapMulticolorLine(int raster, int xScroll);
        void renderECMLine(int raster, int xScroll);

        // Helpers
        void innerWindowForRaster(int raster, int& x0, int& x1) const;
        void renderChar(uint8_t c, int x, int y, uint8_t fg, uint8_t bg, int yInChar, int raster, int x0, int x1);
        void renderCharMultiColor(uint8_t c, int x, int y, uint8_t cellCol, uint8_t bg, int yInChar, int raster, int x0, int x1);
        uint8_t fetchScreenByte(int row,int col, int raster) const;
        uint8_t fetchColorByte (int row,int col, int raster) const;
        bool spriteDMANeededThisLine() const;

        // Screen helper
        inline int fbY(int raster) const { return BORDER_SIZE + (raster - cfg_->firstVisibleLine); }

        // ML Mnnitor Cache updater
        void updateMonitorCaches(int raster);
};
#endif // VIC_H
