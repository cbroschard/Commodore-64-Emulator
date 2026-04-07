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
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include "Common/VideoMode.h"
#include "CIA2.h"
#include "CPU.h"
#include "IO.h"
#include "IRQLine.h"
#include "Logging.h"
#include "Memory.h"
#include "StateReader.h"
#include "StateWriter.h"

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
        inline void attachTraceManagerInstance(TraceManager* traceMgr) { this->traceMgr = traceMgr; }

        // Setter for video mode
        void setMode(VideoMode mode);

        // State management
        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

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

        // Getter for Memory VIC Read for CIA2 Bank Base
        inline uint16_t getBankBaseFromVIC(int raster) { return dd00_per_raster[raster]; }

        // Reset to power on defaults
        void reset();

        // Rendering of all screens
        void renderLine(int raster);

        inline bool isFrameDone() const { return frameDone; }
        inline void clearFrameFlag() { frameDone = false; }

        inline bool getRSEL(int raster) const { return (effectiveD011ForRaster(raster) & 0x08) != 0; }
        inline bool getCSEL(int raster) const { return (effectiveD016ForRaster(raster) & 0x08) != 0; }
        inline bool getLatchedRSEL(int raster) const { return (latchedD011ForRaster(raster) & 0x08) != 0; }
        inline bool getLatchedCSEL(int raster) const { return (latchedD016ForRaster(raster) & 0x08) != 0; }

        // Getters for current memory locations
        inline uint16_t getCHARBase(int raster) const { return ((effectiveD018ForRaster(raster) >> 1) & 0x07) * 0x0800; }
        inline uint16_t getScreenBase(int raster) const { return ((effectiveD018ForRaster(raster) & 0xF0) << 6); }
        inline uint16_t getBitmapBase(int raster) const { return ((effectiveD018ForRaster(raster) >> 3) & 0x01) * 0x2000; }
        inline uint16_t getLatchedCHARBase(int raster) const { return ((latchedD018ForRaster(raster) >> 1) & 0x07) * 0x0800; }
        inline uint16_t getLatchedScreenBase(int raster) const { return ((latchedD018ForRaster(raster) & 0xF0) << 6); }
        inline uint16_t getLatchedBitmapBase(int raster) const { return ((latchedD018ForRaster(raster) >> 3) & 0x01) * 0x2000; }

        // ML Monitor
        enum class FetchKind
        {
            None,


            CharMatrix,

            SpritePtr0, SpritePtr1, SpritePtr2, SpritePtr3,
            SpritePtr4, SpritePtr5, SpritePtr6, SpritePtr7,

            SpriteData0, SpriteData1, SpriteData2, SpriteData3,
            SpriteData4, SpriteData5, SpriteData6, SpriteData7
        };

        struct VICIRQSnapshot { uint8_t ier = 0; };
        std::string decodeModeName() const;
        std::string getVICBanks() const;
        std::string dumpRegisters(const std::string& group) const;
        inline void setLog(bool enable) { setLogging = enable; }
        uint8_t getIER() const { return registers.interruptEnable & 0x0F; }
        uint8_t getIFR() const { return registers.interruptStatus & 0x0F; }
        inline bool irqLineActive() const { return (registers.interruptStatus & registers.interruptEnable & 0x0F); }
        inline VICIRQSnapshot snapshotIRQs() const { return VICIRQSnapshot { getIER() }; }
        inline void restoreIRQs(const VICIRQSnapshot& snapshot) { setIERExact(snapshot.ier & 0x0F); }
        inline void disableAllIRQs() { setIERExact(0); }
        void setIERExact(uint8_t mask);
        void clearPendingIRQs();
        inline uint16_t getRasterDot() const { return currentCycle * 8; } // Used for formatting trace
        inline uint16_t getCurrentRaster() const { return registers.raster; } // Used for formatting trace
        std::string dumpCurrentCycleDebug() const;
        std::string dumpCycleDebugFor(int raster, int cycle) const;
        std::string dumpRasterFetchMap(int raster) const;

    protected:

    private:
        // Non-owning pointers
        CIA2* cia2object;
        CPU* processor;
        IO* IO_adapter;
        IRQLine* IRQ;
        Logging* logger;
        Memory* mem;
        TraceManager* traceMgr;

        static const uint16_t COLOR_MEMORY_START = 0xD800;

        static constexpr int RASTER_IRQ_COMPARE_CYCLE = 1;

        // Screen constants
        static constexpr int BORDER_SIZE = 32;
        static constexpr int VISIBLE_WIDTH = 320 + 2 * BORDER_SIZE;   // 384

        // Video Mode configuration at runtime (NTSC or PAL)
        VideoMode mode_;
        const ModeConfig* cfg_;

        // tick 40-byte character/color fetch during badline
        uint8_t charPtrFIFO[40];
        uint8_t colorPtrFIFO[40];

        // bad line
        int firstBadlineY;
        bool denSeenOn30;

        // Keep track of frame completion
        bool frameDone;

        // IRQ
        bool rasterIrqSampledThisLine;

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

        struct InternalState
        {
            // Character matrix / raster engine
            uint16_t vcBase = 0;     // base video counter
            uint16_t vmliBase = 0;   // bad-line matrix fetch base for current row
            uint8_t rc = 0;          // row counter 0-7

            // Bad-line / display state
            bool displayEnabled = false;
            bool displayEnabledNext = false;
            bool badLine = false;
            bool badLineSampled = false;

            // Border flip-flops
            bool verticalBorder = true;
            bool leftBorder = true;
            bool rightBorder = true;

            int leftBorderOpenX = 0;
            int rightBorderCloseX = VISIBLE_WIDTH;

            int topBorderOpenRaster = 0;
            int bottomBorderCloseRaster = 0;

            // Bus arbitration
            bool ba = true;
            bool aec = true;

            // Open bus
            uint8_t openBus = 0xFF;
        } vicState;

        struct SpriteUnit
        {
            bool dmaActive = false;
            bool displayActive = false;
            bool yExpandLatch = false;

            uint8_t mc = 0;
            uint8_t mcBase = 0;

            uint8_t pointerByte = 0;
            uint16_t dataBase = 0;

            uint8_t shift0 = 0;
            uint8_t shift1 = 0;
            uint8_t shift2 = 0;

            int currentRow = 0;

            int startY = 0;

            int outputBit = 0;
            int outputRepeat = 0;
            bool rowPrepared = false;

            int outputXStart = 0;
            int outputWidth = 0;

            uint8_t fetched0 = 0;
            uint8_t fetched1 = 0;
            uint8_t fetched2 = 0;
        };

        std::array<SpriteUnit, 8> spriteUnits;

        std::array<std::array<uint8_t, 512>, 8> spriteOpaqueLine{};
        std::array<std::array<uint8_t, 512>, 8> spriteColorLine{};

        std::array<uint8_t, 512> bgColorLine{};
        std::array<uint8_t, 512> bgOpaqueLine{};

        // Per raster register latches
        std::vector<uint8_t> d011_per_raster;
        std::vector<uint8_t> d016_per_raster;
        std::vector<uint8_t> d018_per_raster;
        std::vector<uint16_t> dd00_per_raster;

        // Border latches
        std::vector<uint8_t> borderVertical_per_raster;
        std::vector<int16_t> borderLeftOpenX_per_raster;
        std::vector<int16_t> borderRightCloseX_per_raster;

        std::array<uint8_t, 512> borderMaskLine{};
        std::array<uint8_t, 512> finalColorLine{};

        // Caches for ML Monitor
        uint16_t charBaseCache;
        uint16_t screenBaseCache;
        uint16_t bitmapBaseCache;

        // Sprite pointer latch
        uint16_t sprPtrBase[8];

        // Cache background opaque pixels
        std::vector<std::array<uint8_t, 512>> bgOpaque;

        // ML Monitor logging
        bool setLogging;

        // Multicolor helper for readRegister
        inline uint8_t getBackgroundColor(int value) const { return registers.backgroundColor[value]; }

        // fine-scroll Helpers ($D016 bits 0-2 , $D011 bits 0-2)
        inline uint8_t fineXScroll(int raster) const { return effectiveD016ForRaster(raster) & 0x07; }
        inline uint8_t fineYScroll(int raster) const { return effectiveD011ForRaster(raster) & 0x07; }
        inline uint8_t latchedD011ForRaster(int raster) const { return d011_per_raster[raster] & 0x7F; }
        inline uint8_t latchedD016ForRaster(int raster) const { return d016_per_raster[raster] & 0x1F; }
        inline uint8_t latchedD018ForRaster(int raster) const { return d018_per_raster[raster] & 0xFE; }
        uint8_t effectiveD011ForRaster(int raster) const;
        uint8_t effectiveD016ForRaster(int raster) const;
        uint8_t effectiveD018ForRaster(int raster) const;

        // Read/Write register Helpers
        inline int getSpriteIndex(uint16_t address) const { return (address - 0xD000) / 2; }
        inline int getSpriteColorIndex(uint16_t address) const { return (address - 0xD027); }
        inline bool isSpriteX(uint16_t address) const { return ((address - 0xD000) % 2) == 0; }
        void markBGOpaque(int screenY, int px);

        // DD00 latch
        void latchNextRasterDD00();

        // OpenBus helper
        inline void updateOpenBus(uint8_t value) { vicState.openBus = value; }

        // Bus Arbitration Helpers
        bool isBadLineBusWarningCycle(int raster, int cycle) const;
        bool isBadLineBusStealCycle(int raster, int cycle) const;

        bool isSpriteBusWarningCycle(int raster, int cycle) const;
        bool isSpriteBusStealCycle(int raster, int cycle) const;

        bool shouldBALow(int raster, int cycle) const;
        bool shouldAECLow(int raster, int cycle) const;

        // Bad line Helpers
        void initializeFirstBadLineIfNeeded(int raster);
        bool isBadLine(int raster) const;
        void beginBadLineFetch();
        void fetchBadLineMatrixByte(int fetchIndex, int raster);
        void performBadLineFetchesForCurrentCycle();

        // Tick() Helpers
        void beginFrameIfNeeded();
        void runCycleDecisionPhase();
        void handleCycle0Decisions();
        void handleCycle14Decisions();
        void handleCycle15Decisions();
        void handleSpriteDmaStartDecisions();
        void handleDmaStartCycleDecisions();
        void handleCycle58Decisions();
        void runFetchPhase();

        void advanceCycleAndFinalizeLineIfNeeded();
        void finalizeCurrentRasterLine(int curRaster);
        void finalizeFrameIfNeeded(int curRaster);
        void advanceToNextRaster();
        void traceRasterEnd();

        void updatePerCycleState();

        // Address enable control
        void updateBusArbitration();
        bool AEC;
        int currentCycle;

        // IRQ Helpers
        inline bool rasterCompareMatchesNow() const { return registers.raster == registers.rasterInterruptLine; }
        uint16_t visibleRasterForIRQCompare() const;
        uint16_t visibleRasterForRead() const;
        void updateIRQLine();
        void triggerRasterIRQIfMatched();
        void raiseVicIRQSource(uint8_t sourceBitMask);
        void checkRasterIRQCompareTransition(uint16_t oldLine, uint16_t newLine);

        // Helper to keep monitor output consistent with IRQ status
        uint8_t d019Read() const;

        // Sprite DMA Helpers
        int spriteRowFromMCBase(int spr) const;
        bool shouldAdvanceSpriteMCBaseThisLine(int spr) const;
        bool isSpriteDMAComplete(int spr) const;
        void resetSpriteDMAState(int spr);
        void performSpriteDataFetches();
        void fetchSpriteDataByte(int sprite, int byteIndex, int raster);
        void latchSpriteShiftersFromFetchedBytes(int sprite);
        bool isSpritePointerFetchCycle(int sprite, int cycle) const;
        int spritePointerFetchSpriteForCycle(int cycle) const;
        int spriteDataFetchSpriteForCycle(int cycle) const;
        int spriteDataByteIndexForCycle(int sprite, int cycle) const;
        uint16_t spritePointerAddressForRaster(int sprite, int raster) const;

        // Sprite collision Helpers
        void detectSpriteToSpriteCollision(int raster);
        void detectSpriteToBackgroundCollision(int raster);
        bool checkSpriteBackgroundOverlap(int spriteIndex, int raster);
        bool checkSpriteSpriteOverlapOnLine(int A, int B, int raster);
        int spriteScreenXFor(int sprIndex, int raster) const;
        bool spriteDisplayCoversRaster(int sprIndex, int raster, int &rowInSprite, int &fbLine) const;

        void updateSpriteDMAStartForCurrentLine(int raster);
        void updateSpriteDMAEndOfLine(int raster);
        void fetchSpritePointer(int sprite, int raster);
        bool isSpriteDMAFetchCycle(int sprite, int cycle) const;
        int spriteFetchSlotStart(int sprite) const;
        void syncSpriteCompatAddress(int sprite);

        void prepareSpriteOutputForRaster(int raster);

        int spritePreparedOutputWidth(int sprIndex) const;
        void beginSpriteLineOutput(int sprIndex, int raster);

        void resetSpriteLineSequencer(int sprIndex, int raster);
        void advanceSpriteOutputState(int sprIndex);
        bool currentSpriteSequencerPixel(int sprIndex, uint8_t& outColor, bool& opaque) const;

        void clearSpriteLineBuffers();

        void beginSpriteRasterOutput(int raster);
        void stepSpriteSequencersAtX(int raster, int px);

        uint32_t getLatchedSpriteBits(int sprite) const;

        bool isBackgroundPixelOpaque(int x, int y);

        // Ensure graphics mode updates
        graphicsMode currentMode;
        void updateGraphicsMode(int raster);

        // Line rendering
        void renderTextLine(int raster, int xScroll);
        void renderBitmapLine(int raster, int xScroll);
        void renderBitmapMulticolorLine(int raster, int xScroll);
        void renderECMLine(int raster, int xScroll);

                struct TextCellSample
        {
            bool valid = false;

            int px = 0;
            int py = 0;

            int displayCol = 0;
            int yInChar = 0;

            uint8_t screenByte = 0;
            uint8_t colorByte = 0;
            uint8_t bgColor = 0;

            bool multicolor = false;
        };

        struct BackgroundPixel
        {
            uint8_t color = 0;
            bool opaque = false;
        };

        bool sampleTextCell(int raster, int xScroll, int col, TextCellSample& out) const;
        BackgroundPixel sampleStandardTextPixel(const TextCellSample& cell, int px, int raster) const;
        void writeBackgroundPixel(int px, const BackgroundPixel& pixel);

        // Helpers
        void clearBadLineFifo();
        void clearBackgroundLineBuffers();
        void generateBackgroundLine(int raster);

        void emitRasterLineInOrder(int raster);
        void emitRasterPixel(int raster, int px);

        int rasterVisibleStartX(int raster) const;
        int rasterVisibleEndX(int raster) const;

        bool isInnerDisplayPixel(int raster, int px) const;

        void buildBorderMaskLine(int raster);
        void composeFinalRasterLine(int raster);
        uint8_t compositePixelAtX(int raster, int px) const;
        uint8_t produceRasterPixel(int raster, int px) const;

        inline void spriteVisibleXRange(int& x0, int& x1) const { x0 = 0; x1 = 320 + 2 * BORDER_SIZE; }
        bool verticalDisplayOpenForRaster(int raster) const;
        bool horizontalBorderLatchedAtPixel(int raster, int px) const;
        void innerWindowForRaster(int raster, int& x0, int& x1) const;
        void getInnerDisplayBounds(int raster, int& leftInner, int& rightInner) const;
        void renderChar(uint8_t c, int x, int y, uint8_t fg, uint8_t bg, int yInChar, int raster, int x0, int x1);
        void renderCharMultiColor(uint8_t c, int x, int y, uint8_t cellCol, uint8_t bg, int yInChar, int raster, int x0, int x1);
        uint8_t fetchScreenByte(int row,int col, int raster) const;
        uint8_t fetchColorByte (int row,int col, int raster) const;

        int currentDisplayRowBase() const;
        uint8_t fetchDisplayScreenByte(int col, int raster) const;
        uint8_t fetchDisplayColorByte(int col, int raster) const;
        uint8_t resolveDisplayScreenByte(int displayCol, int raster) const;
        uint8_t resolveDisplayColorByte(int displayCol, int raster) const;

        void advanceVideoCountersEndOfLine(int raster);
        int currentCharacterRow() const;
        void currentDisplayRowCol(int displayCol, int& row, int& col) const;

        void updateVerticalBorderState(int raster);
        void updateHorizontalBorderState(int raster);
        bool rasterWithinVerticalDisplayWindow(int raster) const;
        bool borderActiveAtPixel(int raster, int px) const;

        // OpenBus Helpers
        uint8_t latchOpenBus(uint8_t value);
        uint8_t getOpenBus() const;
        uint8_t latchOpenBusMasked(uint8_t definedBits, uint8_t definedMask);

        // Screen helper
        inline int fbY(int raster) const { return BORDER_SIZE + (raster - cfg_->firstVisibleLine); }

        // Rebuild Border Latches
        void rebuildBorderRasterLatches();

        // ML Mnnitor Cache updater
        void updateMonitorCaches(int raster);

        FetchKind getFetchKindForCycle(int raster, int cycle) const;
        const char* fetchKindName(FetchKind kind) const;

        // Trace helpers
        bool vicTraceOn(TraceManager::TraceDetail d) const;
        TraceManager::Stamp makeVicStamp() const;

        // Generic category helpers
        void traceVicEvent(const std::string& text) const;
        void traceVicRegEvent(const std::string& text) const;
        void traceVicBadlineEvent(const std::string& text) const;
        void traceVicSpriteEvent(const std::string& text) const;
        void traceVicBusEvent(const std::string& text) const;

        // IRQ / raster helpers
        void traceVicRasterIrqEvent(const char* phase, uint16_t oldLine, uint16_t newLine, bool matched) const;
        void traceVicRasterRetargetTest(const char* phase, uint16_t oldLine, uint16_t newLine, bool sampled,  bool matched) const;

        // Register helpers
        void traceVicRegWrite(uint16_t address, uint8_t oldValue, uint8_t newValue) const;

        // Badline helpers
        void traceVicBadLineStart(int raster, int cycle, uint16_t vcBase, uint8_t rc, bool den) const;
        void traceVicBadLineFetch(int raster, int cycle, int fetchIndex,
            uint16_t vc, int row, int col, uint8_t screenByte, uint8_t colorByte) const;

        // Cycle helpers
        void traceVicCycleCheckpoint(const char* phase, int raster, int cycle) const;

        // Sprite helpers
        void traceVicSpriteDmaStart(int sprite) const;
        void traceVicSpritePtrFetch(int sprite, int raster, uint16_t ptrLoc, uint8_t ptr) const;
        void traceVicSpriteDataFetch(int sprite, int raster, int byteIndex, uint16_t addr, uint8_t value) const;
        void traceVicSpriteSlotEvent(int sprite, const char* phase, int raster, int cycle, int byteIndex = -1) const;
        void traceVicSpriteEolState(int sprite, int raster) const;
        void traceVicSpriteAdvanceDecision(int sprite, int raster, bool willAdvance) const;
        void traceVicSpriteStartCheck(int sprite, int raster, uint8_t spriteY, bool enabled, bool yExpanded,
            bool rasterMatch, bool willStart) const;
        void traceVicSpriteRowMismatch(int sprite, int raster, int computedRow) const;

        // Bus helpers
        void traceVicBusArb(bool oldBA, bool oldAEC, bool newBA, bool newAEC,
            bool badLineNow, bool baLow, bool aecLow) const;
        const char* busArbReason(int raster, int cycle) const;
};
#endif // VIC_H
