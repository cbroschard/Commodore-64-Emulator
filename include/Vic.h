// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef VIC_H
#define VIC_H

class Bus;
class CIA2;
class CPU;
class DataBusLatch;
class IRQLine;
class IVideoSink;

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include "Debug/TraceManager.h"
#include "Common/VideoMode.h"
#include "StateReader.h"
#include "StateWriter.h"

class Vic
{
    public:
        Vic(VideoMode mode = VideoMode::NTSC);
        virtual ~Vic();

        // Pointer functions
        inline void attachBusInstance(Bus* bus) { this->bus = bus; }
        inline void attachCPUInstance(CPU* cpu) { this->cpu = cpu; }
        inline void attachIVideoSinkInstance(IVideoSink* sink) { this->sink = sink; }
        inline void attachCIA2Instance(CIA2* cia2) { this->cia2 = cia2; }
        inline void attachDataBusLatchInstance(DataBusLatch* dataBus) { this->dataBus = dataBus; }
        inline void attachIRQLineInstance(IRQLine* IRQ) { this->IRQ = IRQ; }
        inline void attachTraceManagerInstance(TraceManager* traceMgr) { this->traceMgr = traceMgr; }

        // Setter for video mode
        void setMode(VideoMode mode);

        // State management
        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        // Register read/write
        uint8_t readRegister(uint16_t address);
        uint8_t peekRegister(uint16_t address) const;
        void writeRegister(uint16_t address, uint8_t value);

        // Light pen latch
        void triggerLightPenLatch();

        // Cycle accurate updating of the raster, etc
        void beginCycle();
        void busPhase();
        void endCycle();

        // Getter for BUS arb status
        inline bool getBA() const { return vicState.ba; }
        inline bool getAEC() const { return vicState.aec; }

        // Graphics mode determination
        enum class graphicsMode
        {
            standard,
            multicolor,
            bitmap,
            multicolorBitmap,
            extendedColorText,

            illegalText,
            illegalBitmap,
            illegalMulticolorBitmap
        };

        // Getter for current graphics mode
        inline graphicsMode getCurrentGraphicsMode() const {  return currentMode; }

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
        inline uint16_t getLatchedScreenBase(int raster) const { return ((latchedD018ForRaster(raster) & 0xF0) << 6); }
        inline uint16_t getLatchedBitmapBase(int raster) const { return ((latchedD018ForRaster(raster) >> 3) & 0x01) * 0x2000; }

        // ML Monitor
        enum class FetchKind
        {
            None,

            CharMatrix,
            Graphics,

            SpritePtr0,
            SpritePtr1,
            SpritePtr2,
            SpritePtr3,
            SpritePtr4,
            SpritePtr5,
            SpritePtr6,
            SpritePtr7,

            SpriteData0,
            SpriteData1,
            SpriteData2,
            SpriteData3,
            SpriteData4,
            SpriteData5,
            SpriteData6,
            SpriteData7
        };

        // Bus arbitration reason
        enum class BusOwner
        {
            CPU,
            BadLine,
            SpritePointer,
            SpriteData,
            Refresh,
            Idle
        };

        enum class SpriteFetchPhase : uint8_t
        {
            None,
            Pointer,
            Data0,
            Data1,
            Data2
        };

        struct VicCycleSlot
        {
            FetchKind fetchKind = FetchKind::None;
            BusOwner busOwner = BusOwner::CPU;

            // Background graphics fetch (g-access)
            bool graphicsFetch = false;
            int graphicsFetchIndex = -1;

            bool badlineWarning = false;
            bool badlineSteal = false;
            bool badlineBAHold = false;

            bool spriteBAHold = false;
            bool spriteWarning = false;
            bool spriteAECSteal = false;

            uint8_t spriteDmaStartMask = 0;

            bool refresh = false;

            bool baLow = false;
            bool aecLow = false;

            bool cpuBusStolen = false;

            bool rasterIrqSample = false;

            // Phase 1: decision timing markers
            bool latchRasterState = false;        // cycle 0
            bool sampleBadline = false;           // cycle 14
            bool startSpriteDmaCheck = false;     // cycle 15
            bool transferDisplayState = false;    // cycle 58
            bool startBadlineFetch = false;       // cfg_->DMAStartCycle

            int spriteIndex = -1;
            int spriteByteIndex = -1;
            int matrixFetchIndex = -1;

            SpriteFetchPhase spriteFetchPhase = SpriteFetchPhase::None;
        };

        struct VicCycleDebugSnapshot
        {
            bool valid = false;
            std::string error;

            int requestedRaster = 0;
            int requestedCycle = 0;

            int currentRaster = 0;
            int currentCycle = 0;
            bool liveSample = false;

            VicCycleSlot slot {};

            bool badLine = false;
            bool denAtRaster = false;
            bool denSeenOn30 = false;

            bool liveBadLine = false;
            int badLineDmaStartCycle = -1;
            uint8_t badLineFetchIndex = 0;

            uint8_t cAccessScreenLatch = 0;
            uint8_t cAccessColorLatch = 0;
            bool cAccessLatchValid = false;
            int cAccessLatchIndex = -1;

            // Most recently completed graphics (g-access) latch
            bool graphicsLatchValid = false;
            int graphicsLatchIndex = -1;
            uint16_t graphicsLatchAddress = 0;
            uint8_t graphicsLatchD011 = 0;
            uint8_t graphicsLatchD016 = 0;
            uint8_t graphicsLatchD018 = 0;

            int graphicsReloadColumn = -1;
            int graphicsReloadX = -1;

            uint16_t liveVc = 0;
            uint16_t liveVcBase = 0;
            uint8_t liveVmliFetchIndex = 0;
            uint8_t liveRc = 0;
            int liveDisplayRow = 0;

            uint8_t fineY = 0;
            uint8_t fineX = 0;

            uint16_t rasterIrqTarget = 0;
            bool rasterIrqTargetInRange = false;
            bool rasterIrqCompareMatch = false;
            bool rasterIrqEnabled = false;
            bool rasterIrqPending = false;
            bool irqLineActiveNow = false;

            struct SpriteDebug
            {
                bool valid = false;
                bool active = false;
                bool rowLatched = false;

                uint8_t mc = 0;
                uint8_t mcBase = 0;
                int row = 0;
                int currentRow = 0;

                uint8_t pointerByte = 0;
                uint16_t dataBase = 0;
            };

            SpriteDebug sprite {};

            std::array<bool, 8> spriteDmaActive {};
            std::array<bool, 8> spriteRowLatched {};
        };

        struct VicSpriteDebugSnapshot
        {
            int currentRaster = 0;
            int currentCycle = 0;

            uint8_t d015 = 0;
            uint8_t d017 = 0;
            uint8_t d01b = 0;
            uint8_t d01c = 0;
            uint8_t d01d = 0;

            struct Sprite
            {
                bool enabled = false;
                int y = 0;
                int x = 0;

                bool dmaActive = false;
                bool rowDataLatched = false;
                bool yExpandFlipFlop = true;

                uint8_t mc = 0;
                uint8_t mcBase = 0;

                int row = 0;
                int currentRow = 0;

                uint8_t pointerByte = 0;
                uint16_t dataBase = 0;

                uint8_t shift0 = 0;
                uint8_t shift1 = 0;
                uint8_t shift2 = 0;

                uint16_t lastFetchAddr0 = 0;
                uint16_t lastFetchAddr1 = 0;
                uint16_t lastFetchAddr2 = 0;

                bool rowPrepared = false;

                int outputXStart = 0;
                int outputWidth = 0;
                int outputBit = 0;
                int outputRepeat = 0;

                bool multicolorAtX = false;
                bool xExpandedAtX = false;
                bool enabledAtX = false;
            };

            struct Collision
            {
                bool valid = false;
                int raster = 0;
                int x = 0;
                int cycle = 0;
                uint8_t bits = 0;
            };

            std::array<Sprite, 8> sprites {};

            Collision spriteSpriteCollision {};
            Collision spriteBackgroundCollision {};
        };

        struct VicRegisterDebugSnapshot
        {
            uint16_t currentRaster = 0;
            int currentCycle = 0;

            // Sprite registers
            std::array<uint8_t, 8> spriteX {};
            std::array<uint8_t, 8> spriteY {};
            uint8_t spriteXMsb = 0;

            uint8_t spriteEnabled = 0;
            uint8_t spriteYExpansion = 0;
            uint8_t spritePriority = 0;
            uint8_t spriteMultiColor = 0;
            uint8_t spriteXExpansion = 0;

            // Control / raster / memory
            uint8_t control = 0;          // D011 low 7 bits internally
            uint8_t control2 = 0;         // D016
            uint8_t memoryPointer = 0;    // D018
            uint16_t rasterInterruptLine = 0;

            // IRQ
            uint8_t interruptStatus = 0;  // D019 raw/internal
            uint8_t interruptEnable = 0;  // D01A
            bool irqLineActive = false;
            bool rasterIrqCompareMatched = false;
            int rasterIrqCompareCycle = 0;
            bool rasterCompareMatchesNow = false;
            bool rasterIrqTargetInRange = false;

            // Collision
            uint8_t spriteCollision = 0;      // D01E
            uint8_t spriteDataCollision = 0;  // D01F

            // Colors
            uint8_t borderColor = 0;
            uint8_t backgroundColor0 = 0;
            std::array<uint8_t, 3> backgroundColor {};
            uint8_t spriteMultiColor1 = 0;
            uint8_t spriteMultiColor2 = 0;
            std::array<uint8_t, 8> spriteColors {};

            // Light pen / undefined
            uint8_t lightPenX = 0;
            uint8_t lightPenY = 0;
            uint8_t undefinedReg = 0;

            // Current raster latches
            uint8_t latchedD011 = 0;
            uint8_t latchedD016 = 0;
            uint8_t latchedD018 = 0;
            uint16_t latchedDD00 = 0;

            // Border / display window debug
            bool liveVerticalBorder = false;
            bool liveLeftBorder = false;
            bool liveRightBorder = false;

            int liveLeftBorderOpenX = 0;
            int liveRightBorderCloseX = 0;

            bool latchedVerticalBorder = false;
            int latchedBorderOpenX = 0;
            int latchedBorderCloseX = 0;

            int maskInnerX0 = 0;
            int maskInnerX1 = 0;

            int verticalTopOpen = 0;
            int verticalBottomClose = 0;
            bool withinVerticalDisplayWindow = false;

            // Decoded bases
            uint16_t charBase = 0;
            uint16_t screenBase = 0;
            uint16_t bitmapBase = 0;
            uint16_t vicBankBase = 0;

            struct RasterIrqSample
            {
                bool valid = false;
                int raster = 0;
                int cycle = 0;
                uint16_t visibleRaster = 0;
                uint16_t targetRaster = 0;
                bool targetInRange = false;
                bool matched = false;
                bool sampledBefore = false;
                std::string reason;
            };

            RasterIrqSample lastRasterIrqSample {};
        };

        struct VicBadlineDebugSnapshot
        {
            uint16_t raster = 0;
            int cycle = 0;

            bool badLine = false;
            bool badLineSampled = false;

            bool displayEnabled = false;
            bool displayEnabledNext = false;

            bool denSeenOn30 = false;
            int firstBadlineY = -1;

            uint16_t vcBase = 0;
            uint16_t vmliBase = 0;
            uint8_t vmliFetchIndex = 0;
            uint8_t rc = 0;
        };

        struct RasterRowStateSnapshot
        {
            bool valid = false;

            int raster = 0;
            int firstBadlineY = -1;

            uint8_t rc = 0;
            uint8_t displayRc = 0;
            uint16_t vcBase = 0;
            uint16_t vmliBase = 0;
            uint8_t vmliFetchIndex = 0;

            bool displayEnabled = false;
            bool displayEnabledNext = false;
            bool badLine = false;
            bool badLineSampled = false;

            uint8_t d011 = 0;
            uint8_t d016 = 0;
            uint8_t d018 = 0;
        };

        enum class RasterEventKind : uint8_t
        {
            Color,
            Control,
            RasterIRQTarget,
            Control2,
            MemoryPointer,
            SpritePriority,
            SpriteMode,
            SpriteXExpansion,
            SpriteYExpansion,
            SpriteEnable,
            SpriteX
        };

        enum class VicBusPhase : uint8_t
        {
            Phi1,
            Phi2
        };

        struct RasterEventRecord
        {
            RasterEventKind kind = RasterEventKind::Color;

            int raster = 0;
            int cycle = 0;
            VicBusPhase phase = VicBusPhase::Phi2;

            uint16_t address = 0;
            uint8_t oldValue = 0;
            uint8_t newValue = 0;
        };

        struct VicBorderRasterDebugSnapshot
        {
            bool valid = false;

            int raster = 0;

            bool latchedVerticalBorder = false;
            int latchedBorderOpenX = 0;
            int latchedBorderCloseX = 0;

            int verticalTopOpen = 0;
            int verticalBottomClose = 0;
            bool withinVerticalDisplayWindow = false;

            uint8_t d011 = 0;
            uint8_t d016 = 0;
        };

        VicBorderRasterDebugSnapshot getBorderRasterDebugSnapshot(int raster) const;

        VicCycleSlot currentCycleSlot {};
        VicCycleSlot cycleSlotFor(int raster, int cycle) const;

        struct VICIRQSnapshot { uint8_t ier = 0; };
        inline uint8_t getIER() const { return registers.interruptEnable & 0x0F; }
        inline uint8_t getIFR() const { return registers.interruptStatus & 0x0F; }
        inline bool irqLineActive() const { return (registers.interruptStatus & registers.interruptEnable & 0x0F); }
        inline VICIRQSnapshot snapshotIRQs() const { return VICIRQSnapshot { getIER() }; }
        inline void restoreIRQs(const VICIRQSnapshot& snapshot) { setIERExact(snapshot.ier & 0x0F); }
        inline void disableAllIRQs() { setIERExact(0); }
        inline uint16_t getRasterDot() const { return currentCycle * 8; } // Used for formatting trace
        inline uint16_t getCurrentRaster() const { return registers.raster; } // Used for formatting trace
        inline int getCurrentCycleForDebug() const { return currentCycle; }
        inline int getMaxRasterLinesForDebug() const { return cfg_ ? cfg_->maxRasterLines : 0; }
        inline int getCyclesPerLineForDebug() const { return cfg_ ? cfg_->cyclesPerLine : 0; }
        inline int rasterEventPixelXForDebug(int cycle) const { return rasterEventPixelX(cycle); }
        inline const std::vector<RasterEventRecord>& getCurrentRasterEventsForDebug() const { return rasterEventLog; }
        inline const std::vector<RasterEventRecord>& getLastFrameRasterEventsForDebug() const { return lastFrameRasterEventLog; }
        inline const std::vector<RasterRowStateSnapshot>& getCurrentRasterRowsForDebug() const { return rasterRowStates; }
        inline const std::vector<RasterRowStateSnapshot>& getLastFrameRasterRowsForDebug() const { return lastFrameRasterRowStates; }

        inline int getBackgroundMatrixColumnsForDebug() const { return BACKGROUND_MATRIX_COLUMNS; }
        inline int getBackground40ColX0ForDebug() const { return BACKGROUND_40COL_X0; }
        inline uint16_t getColorMemoryStartForDebug() const { return COLOR_MEMORY_START; }
        inline uint8_t d018ForRasterPixelXForDebug ( int raster, int px, bool preferPreviousFrame) const
                                                   { return d018ForRasterPixelX(raster, px, preferPreviousFrame); }

        uint8_t vicReadForDebug(uint16_t address, int raster) const;
        uint8_t vicReadColorForDebug(uint16_t address) const;
        bool isBadLineForDebug(int raster) const;
        void setIERExact(uint8_t mask);
        void clearPendingIRQs();
        std::string decodeModeName() const;
        std::string getVICBanks() const;
        VicCycleDebugSnapshot getCycleDebugSnapshot(int raster, int cycle) const;
        VicSpriteDebugSnapshot getSpriteDebugSnapshot() const;
        VicRegisterDebugSnapshot getRegisterDebugSnapshot() const;
        VicBadlineDebugSnapshot getBadlineDebugSnapshot() const;
        std::string dumpRasterPixelCompositionDebug(int raster, int x0, int x1) const;
        std::string dumpBusArbitrationLine(int raster) const;

    private:
        // Non-owning pointers
        Bus* bus;
        CIA2* cia2;
        CPU* cpu;
        DataBusLatch* dataBus;
        IVideoSink* sink;
        IRQLine* IRQ;
        TraceManager* traceMgr;

        static const uint16_t COLOR_MEMORY_START = 0xD800;
        static constexpr int RASTER_IRQ_COMPARE_CYCLE = 0;
        static constexpr int SPRITE_OUTPUT_WIDTH_EXPANDED_MAX = 48;
        static constexpr uint16_t IDLE_FETCH_ADDRESS = 0x3FFF;

        // Screen constants
        static constexpr int BORDER_SIZE = 32;
        static constexpr int VISIBLE_WIDTH = 320 + 2 * BORDER_SIZE;   // 384

        // VIC-II background sequencer always operates on 40 display columns.
        // CSEL changes the border clipping, not the number of matrix columns fetched.
        static constexpr int BACKGROUND_MATRIX_COLUMNS = 40;

        static constexpr int BACKGROUND_40COL_X0 = 31;

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

        // Raster IRQ comparator edge state.
        // True while the current 9-bit raster counter matches the
        // programmed $D011/$D012 raster IRQ target.
        bool rasterIrqCompareMatched;
        bool rasterIrqTriggeredThisLine;
        bool rasterIrqDeferredReassert;

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
            uint16_t vc = 0;          // live video counter
            uint16_t vcBase = 0;      // video counter base
            uint16_t vmliBase = 0;
            uint8_t vmliFetchIndex = 0;
            uint8_t rc = 0;

            // Bad-line / display state
            bool displayEnabled = false;
            bool displayEnabledNext = false;
            bool badLine = false;
            bool badLineSampled = false;
            int badLineDmaStartCycle = -1;
            uint8_t badLineFetchIndex = 0;
            bool badLineInitializedThisRaster = false;

            // Border flip-flops
            bool verticalBorder = true;
            bool horizontalBorder = true;

            bool leftBorder = true;
            bool rightBorder = true;

            int leftBorderOpenX = 0;
            int rightBorderCloseX = VISIBLE_WIDTH;

            int topBorderOpenRaster = 0;
            int bottomBorderCloseRaster = 0;

            // Bus arbitration
            bool ba = true;
            bool aec = true;
        } vicState;

        struct SpriteUnit
        {
            // DMA lifetime/state
            bool dmaActive = false;

            // VIC-II vertical-expansion flip-flop.
            // This is distinct from the live $D017 register state.
            bool yExpandFlipFlop = true;
            bool yCrunchPending = false;

            uint8_t mc = 0;
            uint8_t mcBase = 0;

            uint8_t pointerByte = 0;
            uint16_t dataBase = 0;

            // Latched row bits currently visible to the sprite output sequencer.
            uint8_t shift0 = 0;
            uint8_t shift1 = 0;
            uint8_t shift2 = 0;

            int currentRow = 0;
            int startY = 0;

            // Line-local sprite output sequencer state.
            int outputBit = 0;
            int outputRepeat = 0;
            bool rowPrepared = false;

            // True once a full 3-byte sprite row has been fetched and copied
            // into shift0/1/2. This is the authoritative "row data exists"
            // signal for visible output.
            bool rowDataLatched = false;

            int outputXStart = 0;
            int outputWidth = 0;

            // Most recently fetched DMA row bytes.
            uint8_t fetched0 = 0;
            uint8_t fetched1 = 0;
            uint8_t fetched2 = 0;

            uint16_t lastFetchAddr0 = 0;
            uint16_t lastFetchAddr1 = 0;
            uint16_t lastFetchAddr2 = 0;
        };

        struct HorizontalBorderWindow
        {
            int openX = 0;
            int closeX = VISIBLE_WIDTH;
        };

        struct VerticalBorderWindow
        {
            int topOpen = 0;
            int bottomClose = 0;
        };

        enum class BackgroundSource : uint8_t
        {
            Border = 0,
            BG0,
            BG1,
            BG2,
            BG3,
            Foreground,
            Bitmap,
            Unknown
        };

        enum class SpriteColorSource : uint8_t
        {
            None = 0,
            SpriteOwnColor,
            SpriteMultiColor1,
            SpriteMultiColor2
        };

        struct MatrixRowCache
        {
            bool valid = false;
            uint16_t vcBase = 0;
            int row = -1;

            std::array<uint8_t, BACKGROUND_MATRIX_COLUMNS> screen {};
            std::array<uint8_t, BACKGROUND_MATRIX_COLUMNS> color {};
            std::array<uint8_t, BACKGROUND_MATRIX_COLUMNS> fetched {};
            std::array<uint8_t, BACKGROUND_MATRIX_COLUMNS> invalid {};
            std::array<uint8_t, BACKGROUND_MATRIX_COLUMNS> invalidScreen {};
            std::array<uint8_t, BACKGROUND_MATRIX_COLUMNS> invalidColor {};
        };

        MatrixRowCache activeMatrixRow;

        // Most recent character-matrix c-access observed by the VIC.
        uint8_t cAccessScreenLatch;
        uint8_t cAccessColorLatch;
        bool cAccessLatchValid;
        int cAccessLatchIndex;

        HorizontalBorderWindow horizontalBorderWindowForCSEL(bool csel40) const;
        VerticalBorderWindow verticalBorderWindowForRaster(int raster) const;
        int horizontalBorderOpenCompareX(bool csel40) const;
        int horizontalBorderCloseCompareX(bool csel40) const;
        int verticalBorderOpenCompareRaster(bool rsel25) const;
        int verticalBorderCloseCompareRaster(bool rsel25) const;
        void updateVerticalBorderStateAtLeftCompare(int raster, int px);
        void updateHorizontalBorderStateAtPixel(int raster, int px);

        std::array<SpriteUnit, 8> spriteUnits;

        std::array<std::array<uint8_t, 512>, 8> spriteOpaqueLine{};
        std::array<std::array<uint8_t, 512>, 8> spriteColorLine{};
        std::array<std::array<SpriteColorSource, 512>, 8> spriteColorSourceLine{};

        std::array<uint8_t, 512> bgColorLine{};
        std::array<uint8_t, 512> bgOpaqueLine{};
        std::array<BackgroundSource, 512> bgSourceLine{};

        // Per raster register latches
        std::vector<uint8_t> d011_per_raster;
        std::vector<uint8_t> d016_per_raster;
        std::vector<uint8_t> d018_per_raster;

        // Border latches
        std::vector<uint8_t> borderVertical_per_raster;
        std::vector<int16_t> borderLeftOpenX_per_raster;
        std::vector<int16_t> borderRightCloseX_per_raster;
        std::vector<uint8_t> borderVerticalStart_per_raster;

        std::array<uint8_t, 512> borderMaskLine{};
        std::array<uint8_t, 512> finalColorLine{};

        // Caches for ML Monitor
        uint16_t charBaseCache;
        uint16_t screenBaseCache;
        uint16_t bitmapBaseCache;

        // Sprite pointer latch
        uint16_t sprPtrBase[8];

        // Multicolor helper for readRegister
        inline uint8_t getBackgroundColor(int value) const { return registers.backgroundColor[value]; }

        // fine-scroll Helpers ($D016 bits 0-2 , $D011 bits 0-2)
        inline uint8_t fineXScroll(int raster) const { return effectiveD016ForRaster(raster) & 0x07; }
        inline uint8_t fineYScroll(int raster) const { return effectiveD011ForRaster(raster) & 0x07; }
        inline uint8_t latchedD011ForRaster(int raster) const { return d011_per_raster[raster] & 0x7F; }
        inline uint8_t latchedD016ForRaster(int raster) const { return d016_per_raster[raster] & 0x1F; }
        inline uint8_t latchedD018ForRaster(int raster) const { return d018_per_raster[raster] & 0xFE; }
        inline uint8_t d016XScroll(uint8_t value) const { return value & 0x07; }
        inline bool d016CSEL(uint8_t value) const { return (value & 0x08) != 0; }
        inline bool d016MCM(uint8_t value) const { return (value & 0x10) != 0; }
        uint8_t effectiveD011ForRaster(int raster) const;
        uint8_t effectiveD016ForRaster(int raster) const;

        uint8_t d011ForRasterPixelX(int raster, int px, bool preferPreviousFrame) const;
        uint8_t d016ForRasterPixelX(int raster, int px, bool preferPreviousFrame) const;
        uint8_t d018ForRasterPixelX(int raster, int px, bool preferPreviousFrame) const;

        // Read/Write register Helpers
        inline int getSpriteIndex(uint16_t address) const { return (address - 0xD000) / 2; }
        inline int getSpriteColorIndex(uint16_t address) const { return (address - 0xD027); }
        inline bool isSpriteX(uint16_t address) const { return ((address - 0xD000) % 2) == 0; }

        // OpenBus helper
        void updateOpenBus(uint8_t value);
        void performIdleFetchForCurrentCycle();

        // Bus Arbitration Helpers
        bool isBadLineCandidateForBusWarning(int raster) const;
        bool isBadLineBusWarningCycle(int raster, int cycle) const;
        bool isBadLineBusStealCycle(int raster, int cycle) const;
        bool isBadLineBAHoldCycle(int raster, int cycle) const;

        bool isRefreshCycle(int cycle) const;

        bool isSpriteBusWarningCycle(int raster, int cycle) const;
        bool isSpriteBusStealCycle(int raster, int cycle) const;
        bool isSpriteBusAECStealCycle(int raster, int cycle) const;
        bool isSpriteDataCpuStealCycle(int sprite, int cycle) const;
        bool isSpriteBusBAHoldCycle(int raster, int cycle) const;

        // Bad line Helpers
        void initializeFirstBadLineIfNeeded(int raster);
        bool isBadLine(int raster) const;
        void beginBadLineFetch();
        void fetchBadLineMatrixByte(int fetchIndex, int raster);
        void performBadLineFetchesForCurrentCycle();
        void updateLiveBadLineCondition();

        // Tick() Helpers
        void beginFrameIfNeeded();
        void runCycleDecisionPhase();
        void handleCycle0Decisions();
        void handleCycle14Decisions();
        void handleBadLineFetchStartDecisions();;
        void handleDmaStartCycleDecisions();
        void handleCycle58Decisions();
        void advanceCharacterSequencerAtCycle58();
        void runFetchPhase();

        void reloadGraphicsSequencerAtCycle14(bool badLineAt14);
        void advanceGraphicsSequencerAfterGAccess();

        // Pixel accurate helpers
        void runPixelOutputPhase();
        void outputPixel(int raster, int x);

        bool performBackgroundGraphicsFetchForCurrentCycle();

        void advanceCycleAndFinalizeLineIfNeeded();
        void finalizeCurrentRasterLine(int curRaster);
        void finalizeFrameIfNeeded(int curRaster);
        void advanceToNextRaster();
        void traceRasterEnd();

        // Address enable control
        void updateBusArbitration();
        int currentCycle;

        struct RasterIRQSampleSnapshot
        {
            bool valid = false;

            int raster = -1;
            int cycle = -1;

            uint16_t visibleRaster = 0;
            uint16_t targetRaster = 0;

            bool targetInRange = false;
            bool matched = false;
            bool sampledBefore = false;

            std::string reason;
        };

        RasterIRQSampleSnapshot lastRasterIRQSample;

        // IRQ Helpers
        bool rasterIRQTargetMatchesVisibleRaster() const;
        inline bool rasterCompareMatchesNow() const { return rasterIRQTargetMatchesVisibleRaster(); }
        uint16_t visibleRasterForIRQCompare() const;
        uint16_t visibleRasterForRead() const;
        void updateIRQLine();
        void raiseVicIRQSource(uint8_t sourceBitMask);
        void evaluateRasterIRQCompare(const char* reason);
        void setRasterIRQTarget(uint16_t newLine, const char* reason, uint8_t writtenValue,
        bool highWrite);
        void handleRasterIRQTargetWrite(uint16_t oldTarget, uint16_t newTarget, bool highWrite, bool rmwWrite, bool rmwDummyWrite);
        bool rasterIRQTargetInRange() const;

        int rasterIRQCompareCycle() const;
        bool isRasterIRQCompareCycle(int cycle) const;

        // Helper to keep monitor output consistent with IRQ status
        uint8_t d019Read() const;

        // Sprite DMA Helpers
        int spriteRowFromMCBase(int spr) const;
        void resetSpriteDMAState(int spr);
        void performSpriteDataFetchForSprite(int sprite);
        int spritePointerFetchSpriteForKind(FetchKind kind) const;
        int spriteDataFetchSpriteForKind(FetchKind kind) const;
        void fetchSpriteDataByte(int sprite, int byteIndex, int raster);
        void latchSpriteShiftersFromFetchedBytes(int sprite);
        int spriteDataByteIndexForCycle(int sprite, int cycle) const;
        uint16_t spritePointerAddressForRaster(int sprite, int raster, int cycle) const;
        void clearSpriteFetchedRowState(int sprite);

        struct SpriteCollisionTimingSnapshot
        {
            bool valid = false;
            int raster = -1;
            int x = -1;
            int cycle = -1;
            uint8_t bits = 0;
        };

        // Sprite collision Helpers
        int spriteRegisterXForRasterPixel(int sprIndex, int raster, int px) const;
        int spriteScreenXFor(int sprIndex, int raster) const;
        bool spriteDisplayCoversRaster(int sprIndex, int raster, int &rowInSprite, int &fbLine) const;

        void latchSpriteSpriteCollision(uint8_t bits, int raster, int firstX);
        void latchSpriteBackgroundCollision(uint8_t bits, int raster, int firstX);

        SpriteCollisionTimingSnapshot lastSpriteSpriteCollision;
        SpriteCollisionTimingSnapshot lastSpriteBackgroundCollision;

        uint8_t updateSpriteDMAStartForCurrentLine(int raster);
        void updateSpriteDMAEndOfLine(int raster);
        void fetchSpritePointer(int sprite, int raster);
        bool isSpriteDMAFetchCycle(int sprite, int cycle) const;
        int spriteFetchSlotStart(int sprite) const;
        void updateSpriteYExpansionFlipFlops();

        void advanceSpriteMCBaseFirstStep();
        void advanceSpriteMCBaseSecondStep();

        void prepareSpriteOutputForRaster(int raster);
        bool spriteCanRenderThisRaster(int sprite) const;
        void resetSpriteLineOutputState(int sprite);

        void beginSpriteLineOutput(int sprIndex, int raster);

        void resetSpriteLineSequencer(int sprIndex, int raster);
        void advanceSpriteOutputState(int sprIndex, int px);
        bool currentSpriteSequencerPixel(int sprIndex, int px, uint8_t& outColor, bool& opaque, SpriteColorSource& outSource) const;

        void clearSpriteLineBuffers();

        void beginSpriteRasterOutput(int raster);
        void stepSpriteSequencersAtX(int raster, int px);
        void outputSpritePixel(int raster, int px);

        uint32_t getLatchedSpriteBits(int sprite) const;

        bool initialSpriteMulticolorModeForRaster(int raster, uint8_t& value) const;
        bool spriteMulticolorAtPixel(int sprite, int px) const;
        bool initialSpriteXExpansionForRaster(int raster, uint8_t& value) const;
        bool spriteXExpandedAtPixel(int sprite, int px) const;
        bool firstRasterSpriteEnableEventValue(int raster, uint8_t& value) const;
        bool spriteEnabledAtPixel(int sprite, int px) const;

        // Ensure graphics mode updates
        graphicsMode currentMode;

        graphicsMode graphicsModeFromRegisters(uint8_t d011, uint8_t d016) const;
        graphicsMode graphicsModeForRaster(int raster) const;
        bool isIllegalGraphicsMode(graphicsMode mode) const;

        void updateGraphicsMode(int raster);

        struct BackgroundPixel
        {
            uint8_t color = 0;
            bool opaque = false;
            BackgroundSource source = BackgroundSource::Unknown;
        };

        struct ActiveBackgroundPixelState
        {
            bool valid = false;
            bool multicolorText = false;

            graphicsMode mode = graphicsMode::standard;

            uint8_t rowBits = 0;

            uint8_t fg = 0;
            uint8_t bg0 = 0;
            uint8_t bg1 = 0;
            uint8_t bg2 = 0;

            BackgroundSource bg0Source = BackgroundSource::BG0;

            int pxBase = 0;
            int py = 0;
            int phase = 0;
        };

        struct RasterColorEvent
        {
            int raster = 0;
            int cycle = 0;
            VicBusPhase phase = VicBusPhase::Phi2;

            uint16_t address = 0;
            uint8_t oldValue = 0;
            uint8_t newValue = 0;
        };

        struct RasterPriorityEvent
        {
            int raster = 0;
            int cycle = 0;
            VicBusPhase phase = VicBusPhase::Phi2;

            uint8_t oldValue = 0;
            uint8_t newValue = 0;
        };

        struct RasterSpriteModeEvent
        {
            int raster = 0;
            int cycle = 0;
            VicBusPhase phase = VicBusPhase::Phi2;

            uint8_t oldValue = 0;
            uint8_t newValue = 0;
        };

        struct RasterSpriteXExpansionEvent
        {
            int raster = 0;
            int cycle = 0;
            VicBusPhase phase = VicBusPhase::Phi2;

            uint8_t oldValue = 0;
            uint8_t newValue = 0;
        };

        struct RasterSpriteEnableEvent
        {
            int raster = 0;
            int cycle = 0;
            VicBusPhase phase = VicBusPhase::Phi2;

            uint8_t oldValue = 0;
            uint8_t newValue = 0;
        };

        struct RasterSpriteXEvent
        {
            int raster = 0;
            int cycle = 0;
            uint16_t address = 0;
            uint8_t oldValue = 0;
            uint8_t newValue = 0;
        };

        struct RasterPixelCompositionSnapshot
        {
            bool valid = false;
            int raster = 0;

            std::array<uint8_t, VISIBLE_WIDTH> bgColor {};
            std::array<uint8_t, VISIBLE_WIDTH> bgOpaque {};
            std::array<uint8_t, VISIBLE_WIDTH> bgSource {};
            std::array<uint8_t, VISIBLE_WIDTH> borderMask {};
            std::array<uint8_t, VISIBLE_WIDTH> finalColor {};
            std::array<uint8_t, VISIBLE_WIDTH> spriteMask {};
        };

        struct BackgroundGraphicsLatch
        {
            bool valid = false;

            int column = -1;

            uint8_t screenByte = 0;
            uint8_t colorByte = 0;
            uint8_t graphicsByte = 0;

            uint16_t graphicsAddress = 0;

            // VIC register state at the actual graphics fetch.
            uint8_t d011 = 0;
            uint8_t d016 = 0;
            uint8_t d018 = 0;

            graphicsMode mode = graphicsMode::standard;
        };

        std::array<BackgroundGraphicsLatch, BACKGROUND_MATRIX_COLUMNS> backgroundGraphicsLatches {};

        std::vector<RasterPixelCompositionSnapshot> rasterPixelStates;
        std::vector<RasterPixelCompositionSnapshot> lastFrameRasterPixelStates;
        std::vector<RasterRowStateSnapshot> rasterRowStates;
        std::vector<RasterRowStateSnapshot> lastFrameRasterRowStates;
        std::vector<RasterEventRecord> rasterEventLog;
        std::vector<RasterEventRecord> lastFrameRasterEventLog;
        std::vector<RasterSpriteXEvent> rasterSpriteXEvents;
        std::vector<RasterSpriteEnableEvent> rasterSpriteEnableEvents;
        std::vector<RasterSpriteXExpansionEvent> rasterSpriteXExpansionEvents;
        std::vector<RasterSpriteModeEvent> rasterSpriteModeEvents;
        std::vector<RasterPriorityEvent> rasterPriorityEvents;
        std::vector<RasterColorEvent> rasterColorEvents;
        std::vector<std::vector<RasterEventRecord>> rasterEventsByRaster;
        std::vector<std::vector<RasterEventRecord>> lastFrameRasterEventsByRaster;

        void resetBackgroundGraphicsLatches();
        void fetchStandardTextGraphicsByte(int raster, int column, uint8_t d011, uint8_t d016, uint8_t d018);

        void fetchStandardBitmapGraphicsByte(int raster, int column, uint8_t d011, uint8_t d016, uint8_t d018);

        void recordRasterColorWrite(uint16_t address, uint8_t oldValue, uint8_t newValue);
        void recordRasterPriorityWrite(uint8_t oldValue, uint8_t newValue);
        void recordRasterSpriteModeWrite(uint8_t oldValue, uint8_t newValue);
        void recordRasterSpriteXExpansionWrite(uint8_t oldValue, uint8_t newValue);
        void recordRasterSpriteEnableWrite(uint8_t oldValue, uint8_t newValue);
        void recordRasterSpriteXWrite(uint16_t address, uint8_t oldValue, uint8_t newValue);
        void recordRasterEventLog(RasterEventKind kind, uint16_t address, uint8_t oldValue, uint8_t newValue);

        void snapshotRasterPixelComposition(int raster);
        void snapshotRasterRowState(int raster);

        bool initialSpritePriorityForRaster(int raster, uint8_t& value) const;
        bool spriteBehindBackgroundAtPixel(int sprite, int px) const;

        SpriteFetchPhase spriteFetchPhaseForCycle(int sprite, int cycle) const;
        bool spriteFetchPhaseStealsCpu(SpriteFetchPhase phase) const;
        int firstSpriteCpuStealCycle(int sprite) const;

        ActiveBackgroundPixelState activeBgPixel;

        inline bool activeStandardTextPixelStateFinished() const { return !activeBgPixel.valid || activeBgPixel.phase >= 8; }

        void resetActiveBackgroundPixelState();
        BackgroundPixel sampleAndAdvanceActiveStandardTextPixel();
        void loadActiveStandardTextPixelStateFromLatch(int raster, int column, int px);
        void loadActiveStandardBitmapPixelStateFromLatch(int raster, int column, int px);
        BackgroundPixel sampleAndAdvanceActiveStandardBitmapPixel();
        BackgroundPixel sampleAndAdvanceActiveMulticolorBitmapPixel();

        void resetActiveMatrixRow();
        bool activeMatrixRowByteForDisplayCol(int displayCol, uint8_t& screenByte, uint8_t& colorByte) const;

        void resetCAccessLatch();

        void stampBackgroundPixelSource(int px, int py, uint8_t color, bool opaque, BackgroundSource source);

        BackgroundPixel sampleAndAdvanceActiveMulticolorTextPixel();

        // Helpers
        void clearBadLineFifo();
        void clearBackgroundLineBuffers();

        void emitRasterLineInOrder(int raster);

        int rasterVisibleStartX(int raster) const;
        int rasterVisibleEndX(int raster) const;

        bool isInnerDisplayPixel(int raster, int px) const;

        void buildBorderMaskLine(int raster);
        void composeFinalRasterLine(int raster);
        BackgroundPixel sampleBackgroundPixelAtX(int raster, int px) const;
        uint8_t compositePixelAtX(int raster, int px) const;

        int rasterPixelToCycle(int px) const;
        int rasterEventPixelX(int cycle) const;
        int rasterRegisterEventPixelX(const RasterEventRecord& e) const;
        int rasterColorEventPixelX(const RasterColorEvent& e) const;
        int rasterPriorityEventPixelX(const RasterPriorityEvent& e) const;
        int rasterSpriteModeEventPixelX(const RasterSpriteModeEvent& e) const;
        int rasterSpriteXExpansionEventPixelX(const RasterSpriteXExpansionEvent& e) const;
        int rasterSpriteEnableEventPixelX(const RasterSpriteEnableEvent& e) const;
        uint8_t spriteYExpansionForRasterPixelX(int raster, int px, bool preferPreviousFrame) const;
        bool spriteYExpandedAtPixel(int sprite, int raster, int px) const;
        bool firstRasterColorEventValue(int raster, uint16_t address, uint8_t& value) const;

        void applyBorderColorEventsToFinalLine(int raster);
        void applyExtendedBackgroundColorEventsToLine(int raster);
        void applyBackgroundColorEventsToLine(int raster);

        uint16_t charBaseForRasterPixelX(int raster, int px) const;
        uint16_t screenBaseForRasterPixelX(int raster, int px) const;
        uint16_t bitmapBaseForRasterPixelX(int raster, int px) const;

        void applySpriteColorEventsToLine(int raster);

        void innerWindowForRaster(int raster, int& x0, int& x1) const;
        uint8_t fetchColorByte (int row,int col, int raster) const;

        int currentDisplayRowBase() const;

        bool shouldUseFetchedMatrixForDisplayCol(int displayCol, int raster) const;
        bool fetchedMatrixBytesForDisplayCol(int displayCol, int raster, uint8_t& screenByte, uint8_t& colorByte) const;

        int currentCharacterRow() const;
        void currentDisplayRowCol(int displayCol, int& row, int& col) const;

        void updateVerticalBorderState(int raster);
        void updateHorizontalBorderState(int raster);
        bool rasterWithinVerticalDisplayWindow(int raster) const;
        bool borderActiveAtPixel(int raster, int px) const;
        void applyVerticalBorderCompare(int raster, uint8_t d011);

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

        // Trace helpers
        bool vicTraceOn(TraceManager::TraceDetail d) const;
        TraceManager::Stamp makeVicStamp() const;

        void traceBackgroundGraphicsFetch(int raster, int cycle, int column, int fetchPixelX, int outputX) const;

        // Generic category helper
        void traceVicBusEvent(const std::string& text) const;

        // IRQ / raster helpers
        void traceVicRasterIrqEvent(const char* phase, uint16_t oldLine, uint16_t newLine, bool matched) const;
        void traceVicRasterRetargetTest(const char* reason, uint16_t oldLine, uint16_t newLine, bool matchedBefore, bool matchedAfter,
                    bool triggeredBefore, bool triggeredAfter, uint8_t isrBefore, uint8_t isrAfter, bool irqBefore, bool irqAfter) const;

        // Register helpers
        void traceVicRegWrite(uint16_t address, uint8_t oldValue, uint8_t newValue) const;

        // Badline helpers
        void traceVicBadLineStart(int raster, int cycle, uint16_t vcBase, uint8_t rc, bool den) const;
        void traceVicBadLineFetch(int raster, int cycle, int fetchIndex, uint16_t vc, int row, int col, uint8_t screenByte, uint8_t colorByte) const;

        // Cycle helpers
        int cyclePixelX(int cycle) const;
        int cycleFramebufferX(int cycle) const;

        void traceVicCycleCheckpoint(const char* phase, int raster, int cycle) const;

        // Sprite helpers
        void traceVicSpriteDmaStart(int sprite) const;
        void traceVicSpritePtrFetch(int sprite, int raster, uint16_t ptrLoc, uint8_t ptr) const;
        void traceVicSpriteDataFetch(int sprite, int raster, int byteIndex, uint16_t addr, uint8_t value) const;
        void traceVicSpriteSlotEvent(int sprite, const char* phase, int raster, int cycle, int byteIndex = -1) const;
        void traceVicSpriteEolState(int sprite, int raster) const;
        void traceVicSpriteAdvanceDecision(int sprite, int raster, bool willAdvance) const;
        void traceVicSpriteStartCheck(int sprite, int raster, uint8_t spriteY, bool enabled, bool yExpanded, bool rasterMatch, bool willStart) const;
        void traceVicSpriteRowMismatch(int sprite, int raster, int computedRow) const;

        // Bus helpers
        void traceVicBusArb(bool oldBA, bool oldAEC, bool newBA, bool newAEC, bool badLineNow, bool baLow, bool aecLow) const;
        const char* busArbReason(int raster, int cycle) const;

        void postLoadState();
};

#endif // VIC_H
