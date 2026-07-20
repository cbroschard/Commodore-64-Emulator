// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/FDC177x.h"
#include "IECBUS.h"
#include "MLMonitorBackend.h"
#include "Peripheral.h"

static const char* cpuBusCycleTypeName(CPU::CpuBusCycleType type)
{
    switch (type)
    {
        case CPU::CpuBusCycleType::None:        return "None";
        case CPU::CpuBusCycleType::OpcodeFetch: return "OpcodeFetch";
        case CPU::CpuBusCycleType::Read:        return "Read";
        case CPU::CpuBusCycleType::Write:       return "Write";
        case CPU::CpuBusCycleType::DummyRead:   return "DummyRead";
        case CPU::CpuBusCycleType::DummyWrite:  return "DummyWrite";
        case CPU::CpuBusCycleType::StackRead:   return "StackRead";
        case CPU::CpuBusCycleType::StackWrite:  return "StackWrite";
        default:                                return "Unknown";
    }
}

static const char* c64ColorName(uint8_t c)
{
    static const char* names[16] =
    {
        "Black", "White", "Red", "Cyan",
        "Purple", "Green", "Blue", "Yellow",
        "Orange", "Brown", "Light Red", "Dark Grey",
        "Grey", "Light Green", "Light Blue", "Light Grey"
    };

    return names[c & 0x0F];
}

static int bit(bool v)
{
    return v ? 1 : 0;
}

static void appendVIATimerDebug(std::ostream& out,
                                const DriveVIABase::VIATimerDebugView& t)
{
    out << "\n";
    out << "Timer Runtime State\n";
    out << "-------------------\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "T1 Counter: $"
        << std::setw(4) << static_cast<int>(t.timer1Counter)
        << "  Latch: $"
        << std::setw(4) << static_cast<int>(t.timer1Latch)
        << std::dec << std::setfill(' ')
        << "  Running: " << (t.timer1Running ? 1 : 0)
        << "  JustLoaded: " << (t.timer1JustLoaded ? 1 : 0)
        << "  ReloadPending: " << (t.timer1ReloadPending ? 1 : 0)
        << "  InhibitIRQ: " << (t.timer1InhibitIRQ ? 1 : 0)
        << "  PB7: " << (t.timer1PB7Level ? 1 : 0)
        << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "T2 Counter: $"
        << std::setw(4) << static_cast<int>(t.timer2Counter)
        << "  Latch: $"
        << std::setw(4) << static_cast<int>(t.timer2Latch)
        << std::dec << std::setfill(' ')
        << "  Running: " << (t.timer2Running ? 1 : 0)
        << "  JustLoaded: " << (t.timer2JustLoaded ? 1 : 0)
        << "  InhibitIRQ: " << (t.timer2InhibitIRQ ? 1 : 0)
        << "  LowLatchByte: $"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(t.timer2LowLatchByte)
        << std::dec << std::nouppercase << std::setfill(' ')
        << "\n";
}

static std::string cycleSlotMarkers(const Vic::VicCycleSlot& slot)
{
    std::string flags;

    auto add = [&](const char* name)
    {
        if (!flags.empty())
            flags += " ";
        flags += name;
    };

    if (slot.rasterIrqSample)
        add("IRQ");

    if (slot.latchRasterState)
        add("LATCH");

    if (slot.sampleBadline)
        add("BADSMPL");

    if (slot.startSpriteDmaCheck)
        add("SPRDMA");

    if (slot.transferDisplayState)
        add("DISPXFER");

    if (slot.startBadlineFetch)
        add("DMASTART");

    if (slot.refresh)
        add("REFRESH");

    return flags.empty() ? "-" : flags;
}

static const char* ownerName(Vic::BusOwner owner)
{
    switch (owner)
    {
        case Vic::BusOwner::CPU:           return "CPU";
        case Vic::BusOwner::BadLine:       return "BADLINE";
        case Vic::BusOwner::SpritePointer: return "SPRITEPTR";
        case Vic::BusOwner::SpriteData:    return "SPRITEDATA";
        case Vic::BusOwner::Refresh:       return "REFRESH";
        case Vic::BusOwner::Idle:          return "IDLE";
    }

    return "?";
}

static const Vic::RasterRowStateSnapshot* selectVicRowSnapshot(
    int raster,
    bool& usingPreviousFrame,
    const std::vector<Vic::RasterRowStateSnapshot>& currentRows,
    const std::vector<Vic::RasterRowStateSnapshot>& previousRows)
{
    usingPreviousFrame = false;

    if (raster >= 0 &&
        raster < static_cast<int>(previousRows.size()) &&
        previousRows[raster].valid)
    {
        usingPreviousFrame = true;
        return &previousRows[raster];
    }

    if (raster >= 0 &&
        raster < static_cast<int>(currentRows.size()) &&
        currentRows[raster].valid)
    {
        return &currentRows[raster];
    }

    return nullptr;
}

static std::string vicRasterRowStateDetailFromVectors(
    int raster,
    bool preferPreviousFrame,
    const std::vector<Vic::RasterRowStateSnapshot>& currentRows,
    const std::vector<Vic::RasterRowStateSnapshot>& previousRows,
    int maxRaster)
{
    std::ostringstream out;

    if (raster < 0 || raster >= maxRaster)
        return "";

    const auto& primary =
        preferPreviousFrame ? previousRows : currentRows;

    const auto& fallback =
        preferPreviousFrame ? currentRows : previousRows;

    const Vic::RasterRowStateSnapshot* s = nullptr;

    if (raster < static_cast<int>(primary.size()) && primary[raster].valid)
        s = &primary[raster];
    else if (raster < static_cast<int>(fallback.size()) && fallback[raster].valid)
        s = &fallback[raster];

    if (!s)
        return " rowstate unavailable";

    const int rel = s->firstBadlineY >= 0 ? (raster - s->firstBadlineY) : -1;
    const int displayRow = rel >= 0 ? (rel / 8) : -1;

    const int fineY = static_cast<int>(s->d011 & 0x07);
    const int rasterLow3 = raster & 0x07;
    const bool badlineByFineY = rasterLow3 == fineY;

    const int matrixRow = s->vcBase / 40;
    const int vmliRow = s->vmliBase / 40;

    out << " rowstate"
        << " firstBadlineY " << s->firstBadlineY
        << " fineY " << fineY
        << " rows " << (((s->d011 & 0x08) != 0) ? 25 : 24)
        << " rasterLow3 " << rasterLow3
        << " badlineByFineY " << (badlineByFineY ? 1 : 0)
        << " rc " << static_cast<int>(s->rc)
        << " vcBase " << s->vcBase
        << " matrixRow " << matrixRow
        << " vmliBase " << s->vmliBase
        << " vmliRow " << vmliRow
        << " vmliFetchIndex " << static_cast<int>(s->vmliFetchIndex)
        << " displayEnabled " << (s->displayEnabled ? 1 : 0)
        << " displayEnabledNext " << (s->displayEnabledNext ? 1 : 0)
        << " badLine " << (s->badLine ? 1 : 0)
        << " badLineSampled " << (s->badLineSampled ? 1 : 0)
        << " displayRowApprox " << displayRow;

    return out.str();
}

static const char* vicRasterEventKindName(Vic::RasterEventKind kind)
{
    switch (kind)
    {
        case Vic::RasterEventKind::Color:             return "Color";
        case Vic::RasterEventKind::Control:           return "Control $D011";
        case Vic::RasterEventKind::Control2:          return "Control2 $D016";
        case Vic::RasterEventKind::MemoryPointer:     return "Memory ptr $D018";
        case Vic::RasterEventKind::SpritePriority:    return "Sprite priority";
        case Vic::RasterEventKind::SpriteMode:        return "Sprite mode";
        case Vic::RasterEventKind::SpriteXExpansion:  return "Sprite X expansion";
        case Vic::RasterEventKind::SpriteEnable:      return "Sprite enable";
        case Vic::RasterEventKind::SpriteX:           return "Sprite X position";
    }

    return "Unknown";
}

static uint16_t vicScreenBaseFromD018(uint8_t d018)
{
    return static_cast<uint16_t>((d018 & 0xF0) << 6);
}

static uint16_t vicCharBaseFromD018(uint8_t d018)
{
    return static_cast<uint16_t>(((d018 >> 1) & 0x07) * 0x0800);
}

static uint16_t vicBitmapBaseFromD018(uint8_t d018)
{
    return static_cast<uint16_t>(((d018 >> 3) & 0x01) * 0x2000);
}

static std::string vicRasterEventDetail(const Vic::RasterEventRecord& e)
{
    std::ostringstream out;

    switch (e.kind)
    {
        case Vic::RasterEventKind::Control:
        {
            const uint8_t oldVal = e.oldValue & 0x7F;
            const uint8_t newVal = e.newValue & 0x7F;

            out << "$D011"
                << " YSC " << int(oldVal & 0x07) << "->" << int(newVal & 0x07)
                << " RSEL " << ((oldVal & 0x08) ? 1 : 0) << "->" << ((newVal & 0x08) ? 1 : 0)
                << " DEN " << ((oldVal & 0x10) ? 1 : 0) << "->" << ((newVal & 0x10) ? 1 : 0)
                << " BMM " << ((oldVal & 0x20) ? 1 : 0) << "->" << ((newVal & 0x20) ? 1 : 0)
                << " ECM " << ((oldVal & 0x40) ? 1 : 0) << "->" << ((newVal & 0x40) ? 1 : 0);

            return out.str();
        }

        case Vic::RasterEventKind::Control2:
        {
            const uint8_t oldVal = e.oldValue & 0x1F;
            const uint8_t newVal = e.newValue & 0x1F;

            out << "$D016"
                << " XSC " << int(oldVal & 0x07) << "->" << int(newVal & 0x07)
                << " CSEL " << ((oldVal & 0x08) ? 1 : 0) << "->" << ((newVal & 0x08) ? 1 : 0)
                << " MCM " << ((oldVal & 0x10) ? 1 : 0) << "->" << ((newVal & 0x10) ? 1 : 0);

            return out.str();
        }

        case Vic::RasterEventKind::MemoryPointer:
        {
            const uint8_t oldVal = e.oldValue & 0xFE;
            const uint8_t newVal = e.newValue & 0xFE;

            out << "$D018"
                << " screen $"
                << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                << vicScreenBaseFromD018(oldVal)
                << "->$" << std::setw(4) << vicScreenBaseFromD018(newVal)
                << " char $" << std::setw(4) << vicCharBaseFromD018(oldVal)
                << "->$" << std::setw(4) << vicCharBaseFromD018(newVal)
                << " bitmap $" << std::setw(4) << vicBitmapBaseFromD018(oldVal)
                << "->$" << std::setw(4) << vicBitmapBaseFromD018(newVal)
                << std::dec << std::nouppercase << std::setfill(' ');

            return out.str();
        }

        case Vic::RasterEventKind::Color:
            out << "color register write";
            return out.str();

        case Vic::RasterEventKind::SpritePriority:
            out << "$D01B sprite priority";
            return out.str();

        case Vic::RasterEventKind::SpriteMode:
            out << "$D01C sprite multicolor";
            return out.str();

        case Vic::RasterEventKind::SpriteXExpansion:
            out << "$D01D sprite X expansion";
            return out.str();

        case Vic::RasterEventKind::SpriteEnable:
            out << "$D015 sprite enable";
            return out.str();

        case Vic::RasterEventKind::SpriteX:
            out << "sprite X register write";
            return out.str();
    }

    return "";
}

static const char* vicFetchKindName(Vic::FetchKind kind)
{
    switch (kind)
    {
        case Vic::FetchKind::None:        return "None";
        case Vic::FetchKind::CharMatrix:  return "CharMatrix";

        case Vic::FetchKind::SpritePtr0:  return "SpritePtr0";
        case Vic::FetchKind::SpritePtr1:  return "SpritePtr1";
        case Vic::FetchKind::SpritePtr2:  return "SpritePtr2";
        case Vic::FetchKind::SpritePtr3:  return "SpritePtr3";
        case Vic::FetchKind::SpritePtr4:  return "SpritePtr4";
        case Vic::FetchKind::SpritePtr5:  return "SpritePtr5";
        case Vic::FetchKind::SpritePtr6:  return "SpritePtr6";
        case Vic::FetchKind::SpritePtr7:  return "SpritePtr7";

        case Vic::FetchKind::SpriteData0: return "SpriteData0";
        case Vic::FetchKind::SpriteData1: return "SpriteData1";
        case Vic::FetchKind::SpriteData2: return "SpriteData2";
        case Vic::FetchKind::SpriteData3: return "SpriteData3";
        case Vic::FetchKind::SpriteData4: return "SpriteData4";
        case Vic::FetchKind::SpriteData5: return "SpriteData5";
        case Vic::FetchKind::SpriteData6: return "SpriteData6";
        case Vic::FetchKind::SpriteData7: return "SpriteData7";
    }

    return "Unknown";
}

static const char* vicBusOwnerName(Vic::BusOwner owner)
{
    switch (owner)
    {
        case Vic::BusOwner::CPU:           return "CPU";
        case Vic::BusOwner::BadLine:       return "BADLINE";
        case Vic::BusOwner::SpritePointer: return "SPRITE POINTER";
        case Vic::BusOwner::SpriteData:    return "SPRITE DATA";
        case Vic::BusOwner::Refresh:       return "REFRESH";
        case Vic::BusOwner::Idle:          return "IDLE";
    }

    return "UNKNOWN";
}

static bool vicFetchKindIsSpritePointer(Vic::FetchKind kind)
{
    return kind >= Vic::FetchKind::SpritePtr0 &&
           kind <= Vic::FetchKind::SpritePtr7;
}

static bool vicFetchKindIsSpriteData(Vic::FetchKind kind)
{
    return kind >= Vic::FetchKind::SpriteData0 &&
           kind <= Vic::FetchKind::SpriteData7;
}

MLMonitorBackend::MLMonitorBackend() :
    cart(nullptr),
    cass(nullptr),
    cia1(nullptr),
    cia2(nullptr),
    cpu(nullptr),
    bus(nullptr),
    logger(nullptr),
    pla(nullptr),
    sid(nullptr),
    vic(nullptr)
{

}

MLMonitorBackend::~MLMonitorBackend() = default;

void MLMonitorBackend::detachCartridge()
{
    if (comp) comp->setCartridgeAttached(false);
}

bool MLMonitorBackend::getCartridgeAttached()
{
    if (comp) return comp->getCartridgeAttached();
    else return false;
}

std::string MLMonitorBackend::vicDumpBackgroundRowDebug(int raster) const
{
    if (!vic)
        return "VIC not available\n";

    std::ostringstream out;

    const int maxRaster = vic->getMaxRasterLinesForDebug();

    if (raster < 0 || raster >= maxRaster)
    {
        out << "Raster " << raster << " is out of range\n";
        return out.str();
    }

    bool usingPreviousFrame = false;

    const auto* snap =
        selectVicRowSnapshot(
            raster,
            usingPreviousFrame,
            vic->getCurrentRasterRowsForDebug(),
            vic->getLastFrameRasterRowsForDebug()
        );

    if (!snap)
    {
        out << "No row-state snapshot available for raster " << raster << "\n";
        return out.str();
    }

    const char* snapSource = usingPreviousFrame ? "previous frame" : "current frame";

    const uint8_t d011 = snap->d011 & 0x7F;
    const uint8_t d016 = snap->d016 & 0x1F;
    const uint8_t rowD018 = snap->d018 & 0xFE;

    const uint16_t screenBase = vicScreenBaseFromD018(rowD018);
    const uint16_t rowCharBase = vicCharBaseFromD018(rowD018);
    const uint16_t bitmapBase = vicBitmapBaseFromD018(rowD018);

    const int fineY = d011 & 0x07;
    const int fineX = d016 & 0x07;
    const int matrixRow = snap->vcBase / 40;
    const int vmliRow = snap->vmliBase / 40;
    const uint8_t yInChar = static_cast<uint8_t>(snap->rc & 0x07);

    out << "Background Row Debug\n";
    out << "--------------------\n";
    out << "snapshot: " << snapSource << "\n";
    out << "raster: " << raster << "\n";
    out << "mode: " << vic->decodeModeName() << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "D011: $" << std::setw(2) << static_cast<int>(d011)
        << std::dec
        << " fineY=" << fineY
        << " RSEL=" << (((d011 & 0x08) != 0) ? 25 : 24)
        << " DEN=" << (((d011 & 0x10) != 0) ? 1 : 0)
        << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "D016: $" << std::setw(2) << static_cast<int>(d016)
        << std::dec
        << " fineX=" << fineX
        << " CSEL=" << (((d016 & 0x08) != 0) ? 40 : 38)
        << " MCM=" << (((d016 & 0x10) != 0) ? 1 : 0)
        << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "D018 row-latched: $" << std::setw(2) << static_cast<int>(rowD018) << "\n";
    out << "screenBase: $" << std::setw(4) << screenBase << "\n";
    out << "rowCharBase: $" << std::setw(4) << rowCharBase << "\n";
    out << "bitmapBase:  $" << std::setw(4) << bitmapBase << "\n";

    out << std::dec << std::setfill(' ');

    out << "firstBadlineY: " << snap->firstBadlineY << "\n";
    out << "vcBase: " << snap->vcBase << "\n";
    out << "matrixRow: " << matrixRow << "\n";
    out << "vmliBase: " << snap->vmliBase << "\n";
    out << "vmliRow: " << vmliRow << "\n";
    out << "vmliFetchIndex: " << static_cast<int>(snap->vmliFetchIndex) << "\n";
    out << "rc/yInChar: " << static_cast<int>(yInChar) << "\n";
    out << "badLine: " << (snap->badLine ? 1 : 0) << "\n";
    out << "badLineSampled: " << (snap->badLineSampled ? 1 : 0) << "\n";

    out << "\n";
    out << "  col  x    scrAddr scr color d018 charBase charAddr bits\n";
    out << "  --------------------------------------------------------\n";

    const int columns = vic->getBackgroundMatrixColumnsForDebug();
    const int x0 = vic->getBackground40ColX0ForDebug();
    const uint16_t colorBase = vic->getColorMemoryStartForDebug();

    for (int col = 0; col < columns; ++col)
    {
        const int matrixOffset = snap->vcBase + col;

        const uint16_t screenAddr =
            static_cast<uint16_t>(screenBase + (matrixOffset & 0x03FF));

        const uint8_t screenByte =
            mem ? mem->vicRead(screenAddr, raster) : 0xFF;

        const uint16_t colorAddr =
            static_cast<uint16_t>(colorBase + (matrixOffset & 0x03FF));

        const uint8_t colorByte =
            mem ? static_cast<uint8_t>(mem->read(colorAddr) & 0x0F) : 0x0F;

        const int colX = x0 + fineX + (col * 8);

        const uint8_t colD018 =
            vic->d018ForRasterPixelXForDebug(
                raster,
                colX,
                usingPreviousFrame
            ) & 0xFE;

        const uint16_t colCharBase = vicCharBaseFromD018(colD018);

        const uint16_t charAddr =
            static_cast<uint16_t>(
                colCharBase +
                (static_cast<uint16_t>(screenByte) * 8) +
                yInChar
            );

        const uint8_t rowBits =
            mem ? mem->vicRead(charAddr, raster) : 0x00;

        out << "  "
            << std::dec << std::setw(3) << col
            << "  "
            << std::setw(3) << colX
            << "  $"
            << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << screenAddr
            << "   $"
            << std::setw(2) << static_cast<int>(screenByte)
            << "  $"
            << std::setw(2) << static_cast<int>(colorByte)
            << "   $"
            << std::setw(2) << static_cast<int>(colD018)
            << "   $"
            << std::setw(4) << colCharBase
            << "   $"
            << std::setw(4) << charAddr
            << "    $"
            << std::setw(2) << static_cast<int>(rowBits);

        if (rowBits != 0)
            out << "  *";

        out << std::dec << std::nouppercase << std::setfill(' ')
            << "\n";
    }

    return out.str();
}

std::string MLMonitorBackend::vicDumpBackgroundCellDebug(int raster, int col) const
{
    if (!vic)
        return "VIC not available\n";

    std::ostringstream out;

    const int maxRaster = vic->getMaxRasterLinesForDebug();
    const int columns = vic->getBackgroundMatrixColumnsForDebug();

    if (raster < 0 || raster >= maxRaster)
    {
        out << "Raster " << raster << " is out of range\n";
        return out.str();
    }

    if (col < 0 || col >= columns)
    {
        out << "Column " << col << " is out of range\n";
        return out.str();
    }

    bool usingPreviousFrame = false;

    const auto* snap =
        selectVicRowSnapshot(
            raster,
            usingPreviousFrame,
            vic->getCurrentRasterRowsForDebug(),
            vic->getLastFrameRasterRowsForDebug()
        );

    if (!snap)
    {
        out << "No row-state snapshot available for raster " << raster << "\n";
        return out.str();
    }

    const char* snapSource = usingPreviousFrame ? "previous frame" : "current frame";

    const uint8_t d011 = snap->d011 & 0x7F;
    const uint8_t d016 = snap->d016 & 0x1F;
    const uint8_t d018 = snap->d018 & 0xFE;

    const uint16_t screenBase = vicScreenBaseFromD018(d018);
    const uint16_t charBase = vicCharBaseFromD018(d018);
    const uint16_t bitmapBase = vicBitmapBaseFromD018(d018);

    const int fineY = d011 & 0x07;
    const int fineX = d016 & 0x07;

    const int matrixRow = snap->vcBase / 40;
    const int vmliRow = snap->vmliBase / 40;
    const int matrixOffset = snap->vcBase + col;

    const uint16_t screenAddr =
        static_cast<uint16_t>(screenBase + (matrixOffset & 0x03FF));

    const uint8_t screenByte =
        mem ? mem->vicRead(screenAddr, raster) : 0xFF;

    const uint16_t colorAddr =
        static_cast<uint16_t>(
            vic->getColorMemoryStartForDebug() + (matrixOffset & 0x03FF)
        );

    const uint8_t colorByte =
        mem ? static_cast<uint8_t>(mem->read(colorAddr) & 0x0F) : 0x0F;

    const uint8_t yInChar = static_cast<uint8_t>(snap->rc & 0x07);

    const uint16_t charAddr =
        static_cast<uint16_t>(
            charBase +
            (static_cast<uint16_t>(screenByte) * 8) +
            yInChar
        );

    const uint8_t rowBits =
        mem ? mem->vicRead(charAddr, raster) : 0x00;

    out << "Background Cell Debug\n";
    out << "---------------------\n";
    out << "snapshot: " << snapSource << "\n";
    out << "raster: " << raster << "\n";
    out << "col: " << col << "\n";
    out << "mode: " << vic->decodeModeName() << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "D011: $" << std::setw(2) << static_cast<int>(d011)
        << std::dec
        << " fineY=" << fineY
        << " RSEL=" << (((d011 & 0x08) != 0) ? 25 : 24)
        << " DEN=" << (((d011 & 0x10) != 0) ? 1 : 0)
        << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "D016: $" << std::setw(2) << static_cast<int>(d016)
        << std::dec
        << " fineX=" << fineX
        << " CSEL=" << (((d016 & 0x08) != 0) ? 40 : 38)
        << " MCM=" << (((d016 & 0x10) != 0) ? 1 : 0)
        << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "D018: $" << std::setw(2) << static_cast<int>(d018) << "\n";
    out << "screenBase: $" << std::setw(4) << screenBase << "\n";
    out << "charBase:   $" << std::setw(4) << charBase << "\n";
    out << "bitmapBase: $" << std::setw(4) << bitmapBase << "\n";

    out << std::dec << std::setfill(' ');

    out << "firstBadlineY: " << snap->firstBadlineY << "\n";
    out << "vcBase: " << snap->vcBase << "\n";
    out << "matrixRow: " << matrixRow << "\n";
    out << "vmliBase: " << snap->vmliBase << "\n";
    out << "vmliRow: " << vmliRow << "\n";
    out << "vmliFetchIndex: " << static_cast<int>(snap->vmliFetchIndex) << "\n";
    out << "rc/yInChar: " << static_cast<int>(yInChar) << "\n";
    out << "displayEnabled: " << (snap->displayEnabled ? 1 : 0) << "\n";
    out << "displayEnabledNext: " << (snap->displayEnabledNext ? 1 : 0) << "\n";
    out << "badLine: " << (snap->badLine ? 1 : 0) << "\n";
    out << "badLineSampled: " << (snap->badLineSampled ? 1 : 0) << "\n";

    out << std::hex << std::uppercase << std::setfill('0');

    out << "screenAddr: $" << std::setw(4) << screenAddr << "\n";
    out << "screenByte: $" << std::setw(2) << static_cast<int>(screenByte) << "\n";
    out << "colorAddr:  $" << std::setw(4) << colorAddr << "\n";
    out << "colorByte:  $" << std::setw(2) << static_cast<int>(colorByte) << "\n";
    out << "charAddr:   $" << std::setw(4) << charAddr << "\n";
    out << "rowBits:    $" << std::setw(2) << static_cast<int>(rowBits) << "\n";

    out << std::dec << std::nouppercase << std::setfill(' ');

    return out.str();
}

std::string MLMonitorBackend::vicDumpAllRasterEvents() const
{
    if (!vic)
        return "VIC not available\n";

    std::ostringstream out;

    const auto& currentEvents = vic->getCurrentRasterEventsForDebug();
    const auto& previousEvents = vic->getLastFrameRasterEventsForDebug();

    const std::vector<Vic::RasterEventRecord>* events = &previousEvents;
    const char* sourceName = "previous frame";

    if (events->empty() && !currentEvents.empty())
    {
        events = &currentEvents;
        sourceName = "current frame";
    }

    out << "All Raster Events (" << sourceName << ")\n";
    out << "-------------------------------------\n";
    out << "Total events: " << events->size() << "\n";
    out << "  raster  type                    cycle  x     addr   old  new  detail\n";

    if (events->empty())
    {
        out << "No recorded events.\n";
        return out.str();
    }

    for (const auto& e : *events)
    {
        out << "  "
            << std::dec << std::setw(6) << e.raster
            << "  "
            << std::left << std::setw(22) << vicRasterEventKindName(e.kind)
            << std::right
            << std::setw(5) << e.cycle
            << "  "
            << std::setw(4) << vic->rasterEventPixelXForDebug(e.cycle)
            << "  $"
            << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << e.address
            << "  $"
            << std::setw(2) << static_cast<int>(e.oldValue)
            << "   $"
            << std::setw(2) << static_cast<int>(e.newValue)
            << std::dec << std::nouppercase << std::setfill(' ');

        const std::string detail = vicRasterEventDetail(e);
        if (!detail.empty())
            out << "  " << detail;

        out << "\n";
    }

    return out.str();
}

std::string MLMonitorBackend::vicDumpRasterEventsSummary() const
{
    if (!vic)
        return "VIC not available\n";

    std::ostringstream out;

    auto summarize = [&](const char* name, const std::vector<Vic::RasterEventRecord>& events)
    {
        out << name << ": " << events.size() << " events\n";

        if (events.empty())
            return;

        int minRaster = vic->getMaxRasterLinesForDebug();
        int maxRaster = -1;

        int color = 0;
        int control = 0;
        int control2 = 0;
        int memoryPointer = 0;
        int priority = 0;
        int mode = 0;
        int xexp = 0;
        int enable = 0;
        int spriteX = 0;

        for (const auto& e : events)
        {
            minRaster = std::min(minRaster, e.raster);
            maxRaster = std::max(maxRaster, e.raster);

            switch (e.kind)
            {
                case Vic::RasterEventKind::Color:            ++color; break;
                case Vic::RasterEventKind::Control:          ++control; break;
                case Vic::RasterEventKind::Control2:         ++control2; break;
                case Vic::RasterEventKind::MemoryPointer:    ++memoryPointer; break;
                case Vic::RasterEventKind::SpritePriority:   ++priority; break;
                case Vic::RasterEventKind::SpriteMode:       ++mode; break;
                case Vic::RasterEventKind::SpriteXExpansion: ++xexp; break;
                case Vic::RasterEventKind::SpriteEnable:     ++enable; break;
                case Vic::RasterEventKind::SpriteX:          ++spriteX; break;
            }
        }

        out << "  raster range: " << minRaster << " - " << maxRaster << "\n";
        out << "  Color: " << color << "\n";
        out << "  Control $D011: " << control << "\n";
        out << "  Control2 $D016: " << control2 << "\n";
        out << "  Memory ptr $D018: " << memoryPointer << "\n";
        out << "  Sprite priority: " << priority << "\n";
        out << "  Sprite mode: " << mode << "\n";
        out << "  Sprite X expansion: " << xexp << "\n";
        out << "  Sprite enable: " << enable << "\n";
        out << "  Sprite X position: " << spriteX << "\n";
    };

    out << "Raster Event Summary\n";
    out << "--------------------\n";
    summarize("Current frame", vic->getCurrentRasterEventsForDebug());
    summarize("Previous frame", vic->getLastFrameRasterEventsForDebug());

    return out.str();
}

std::string MLMonitorBackend::vicDumpRasterEvents(int raster) const
{
    if (!vic)
        return "VIC not available\n";

    std::ostringstream out;

    const int maxRaster = vic->getMaxRasterLinesForDebug();

    if (raster < 0 || raster >= maxRaster)
    {
        out << "Raster " << raster << " is out of range\n";
        return out.str();
    }

    const auto& currentEvents = vic->getCurrentRasterEventsForDebug();
    const auto& previousEvents = vic->getLastFrameRasterEventsForDebug();

    const std::vector<Vic::RasterEventRecord>* events = &previousEvents;
    const char* sourceName = "previous frame";
    bool usingPreviousFrame = true;

    if (events->empty())
    {
        events = &currentEvents;
        sourceName = "current frame";
        usingPreviousFrame = false;
    }

    out << "Raster Events for line " << raster
        << " (" << sourceName << ")\n";
    out << "--------------------------------\n";
    out << "Total events in " << sourceName << ": " << events->size() << "\n";
    out << "  type                    cycle  x     addr   old  new  detail\n";

    bool any = false;

    for (const auto& e : *events)
    {
        if (e.raster != raster)
            continue;

        any = true;

        out << "  "
            << std::left << std::setw(22) << vicRasterEventKindName(e.kind)
            << std::right
            << std::dec << std::setw(5) << e.cycle
            << "  "
            << std::setw(4) << vic->rasterEventPixelXForDebug(e.cycle)
            << "  $"
            << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << e.address
            << "  $"
            << std::setw(2) << static_cast<int>(e.oldValue)
            << "   $"
            << std::setw(2) << static_cast<int>(e.newValue)
            << std::dec << std::nouppercase << std::setfill(' ');

        const std::string detail = vicRasterEventDetail(e);
        if (!detail.empty())
            out << "  " << detail;

        const std::string rowDetail =
            vicRasterRowStateDetailFromVectors(
                e.raster,
                usingPreviousFrame,
                vic->getCurrentRasterRowsForDebug(),
                vic->getLastFrameRasterRowsForDebug(),
                maxRaster
            );

        if (!rowDetail.empty())
            out << "  " << rowDetail;

        out << "\n";
    }

    if (!any)
        out << "No recorded events on this raster.\n";

    return out.str();
}

std::string MLMonitorBackend::vicDumpRasterRowState(int raster) const
{
    if (!vic)
        return "VIC not available\n";

    std::ostringstream out;

    out << "Raster Row State for line " << raster << " (previous frame)\n";
    out << "----------------------------------------\n";

    const std::string detail =
        vicRasterRowStateDetailFromVectors(
            raster,
            true,
            vic->getCurrentRasterRowsForDebug(),
            vic->getLastFrameRasterRowsForDebug(),
            vic->getMaxRasterLinesForDebug()
        );

    if (detail.empty())
        out << "No row-state snapshot available.\n";
    else
        out << detail << "\n";

    return out.str();
}

std::string MLMonitorBackend::VicDumpBadlineTimelineAroundRaster(int centerRaster) const
{
    if (!vic)
        return "VIC not available\n";

    std::ostringstream oss;

    const int maxRaster = vic->getMaxRasterLinesForDebug();

    const auto& currentRows = vic->getCurrentRasterRowsForDebug();
    const auto& previousRows = vic->getLastFrameRasterRowsForDebug();

    oss << "Bad-line timeline around raster " << centerRaster << "\n";
    oss << "------------------------------------------------------------\n";
    oss << "Raster  D011  DEN  YSC  Bad  LatchedBad  Disp  DispNext  RC  VCBase  VMLIBase  VMLIIdx\n";

    for (int r = centerRaster - 8; r <= centerRaster + 8; ++r)
    {
        if (r < 0 || r >= maxRaster)
            continue;

        const Vic::RasterRowStateSnapshot* s = nullptr;

        if (r < static_cast<int>(previousRows.size()) && previousRows[r].valid)
            s = &previousRows[r];
        else if (r < static_cast<int>(currentRows.size()) && currentRows[r].valid)
            s = &currentRows[r];

        if (!s)
        {
            oss << std::setw(6) << r
                << "  --    --   --   "
                << (vic->isBadLineForDebug(r) ? 1 : 0)
                << "    --          --    --        --  --      --        --\n";
            continue;
        }

        const uint8_t d011 = s->d011 & 0x7F;
        const bool den = (d011 & 0x10) != 0;
        const int ysc = d011 & 0x07;

        oss << std::setw(6) << r
            << "  $"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(d011)
            << std::dec << std::nouppercase << std::setfill(' ')
            << "    "
            << (den ? 1 : 0)
            << "    "
            << ysc
            << "    "
            << (vic->isBadLineForDebug(r) ? 1 : 0)
            << "       "
            << (s->badLineSampled ? 1 : 0)
            << "          "
            << (s->displayEnabled ? 1 : 0)
            << "         "
            << (s->displayEnabledNext ? 1 : 0)
            << "    "
            << static_cast<int>(s->rc)
            << "  "
            << s->vcBase
            << "      "
            << s->vmliBase
            << "        "
            << static_cast<int>(s->vmliFetchIndex)
            << "\n";
    }

    return oss.str();
}

std::string MLMonitorBackend::vicDumpBorderWindowAroundCurrentRaster() const
{
    if (!vic)
        return "VIC not available\n";

    return vicDumpBorderWindowAroundRaster(
        static_cast<int>(vic->getCurrentRaster())
    );
}

std::string MLMonitorBackend::vicDumpBorderWindowAroundRaster(int centerRaster) const
{
    if (!vic)
        return "VIC not available\n";

    std::ostringstream oss;

    const int maxRaster = vic->getMaxRasterLinesForDebug();

    oss << "VIC border window around raster " << centerRaster << "\n";
    oss << "------------------------------------------------------------\n";
    oss << "Raster  Vert  OpenX  CloseX  TopOpen  BottomClose  InWindow  D011  D016\n";

    for (int r = centerRaster - 8; r <= centerRaster + 8; ++r)
    {
        if (r < 0 || r >= maxRaster)
            continue;

        const auto s = vic->getBorderRasterDebugSnapshot(r);

        if (!s.valid)
            continue;

        oss << std::setw(6) << s.raster
            << "  "
            << (s.latchedVerticalBorder ? 1 : 0)
            << "     "
            << std::setw(5) << s.latchedBorderOpenX
            << "  "
            << std::setw(6) << s.latchedBorderCloseX
            << "  "
            << std::setw(7) << s.verticalTopOpen
            << "  "
            << std::setw(11) << s.verticalBottomClose
            << "  "
            << std::setw(8) << (s.withinVerticalDisplayWindow ? 1 : 0)
            << "  $"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(s.d011)
            << "  $"
            << std::setw(2)
            << static_cast<int>(s.d016)
            << std::dec << std::nouppercase << std::setfill(' ')
            << "\n";
    }

    return oss.str();
}

std::string MLMonitorBackend::vicDumpRasterFetchMap(int raster) const
{
    if (!vic)
        return "VIC not available\n";

    std::ostringstream out;

    const int maxRaster = vic->getMaxRasterLinesForDebug();

    if (raster < 0 || raster >= maxRaster)
    {
        out << "Invalid raster: " << raster << "\n";
        return out.str();
    }

    out << "VIC Raster Fetch Map\n\n";
    out << "Raster: " << raster << "\n";

    const bool badLine = vic->isBadLineForDebug(raster);
    out << "Badline: " << (badLine ? "Yes" : "No") << "\n\n";

    out << "Cyc BA AEC Owner      Fetch        Mtx Spr Byt Flags\n";

    for (int c = 0; c < vic->getCyclesPerLineForDebug(); ++c)
    {
        const auto slot = vic->cycleSlotFor(raster, c);

        // Cycle number. Force right alignment and zero fill here so prior
        // std::left formatting from text columns cannot leak into this field.
        out << std::right
            << std::setfill('0')
            << std::setw(2)
            << c
            << std::setfill(' ')
            << "  ";

        // BA / AEC
        out << (slot.baLow ? "L" : "H")
            << "  "
            << (slot.aecLow ? "L" : "H")
            << "   ";

        // Owner
        out << std::left
            << std::setw(10)
            << ownerName(slot.busOwner)
            << " ";

        // Fetch kind
        out << std::left
            << std::setw(12)
            << vicFetchKindName(slot.fetchKind)
            << " ";

        // Matrix index
        if (slot.matrixFetchIndex >= 0)
            out << std::right << std::setw(3) << slot.matrixFetchIndex;
        else
            out << "  -";

        out << " ";

        // Sprite index
        if (slot.spriteIndex >= 0)
            out << std::right << std::setw(3) << slot.spriteIndex;
        else
            out << "  -";

        out << " ";

        // Sprite byte index
        if (slot.spriteByteIndex >= 0)
            out << std::right << std::setw(3) << slot.spriteByteIndex;
        else
            out << "  -";

        out << " "
            << cycleSlotMarkers(slot)
            << "\n";
    }

    return out.str();
}

std::string MLMonitorBackend::vicDumpSpriteDmaState() const
{
    if (!vic)
        return "VIC not available\n";

    std::ostringstream oss;

    const auto snap = vic->getSpriteDebugSnapshot();

    oss << "Sprite DMA / output state\n";
    oss << "------------------------------------------------------------\n";
    oss << "Raster=" << snap.currentRaster
        << " Cycle=" << snap.currentCycle
        << " D015=$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(snap.d015)
        << " D017=$" << std::setw(2) << static_cast<int>(snap.d017)
        << " D01B=$" << std::setw(2) << static_cast<int>(snap.d01b)
        << " D01C=$" << std::setw(2) << static_cast<int>(snap.d01c)
        << " D01D=$" << std::setw(2) << static_cast<int>(snap.d01d)
        << std::dec << std::nouppercase << std::setfill(' ')
        << "\n\n";

    oss << "Spr En  Y    X    DMA RowLat YExp MC MCBase Row CurRow Ptr  DataBase "
           "ShiftBytes RowPrep XStart Width OutBit Rep Mode@X Exp@X En@X\n";

    for (int i = 0; i < 8; ++i)
    {
        const auto& sp = snap.sprites[i];

        oss << std::setw(3) << i << " "
            << " " << (sp.enabled ? 1 : 0) << "  "
            << std::setw(3) << sp.y << "  "
            << std::setw(4) << sp.x << "   "
            << (sp.dmaActive ? 1 : 0) << "     "
            << (sp.rowDataLatched ? 1 : 0) << "     "
            << (sp.yExpandLatch ? 1 : 0) << "   "
            << std::setw(2) << static_cast<int>(sp.mc) << "   "
            << std::setw(2) << static_cast<int>(sp.mcBase) << "     "
            << std::setw(2) << sp.row << "    "
            << std::setw(2) << sp.currentRow << "   "
            << "$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(sp.pointerByte)
            << "  $"
            << std::setw(4) << static_cast<int>(sp.dataBase)
            << "   $"
            << std::setw(2) << static_cast<int>(sp.shift0)
            << " $"
            << std::setw(2) << static_cast<int>(sp.shift1)
            << " $"
            << std::setw(2) << static_cast<int>(sp.shift2)
            << std::dec << std::nouppercase << std::setfill(' ')
            << "      "
            << (sp.rowPrepared ? 1 : 0) << "      "
            << std::setw(4) << sp.outputXStart << "   "
            << std::setw(3) << sp.outputWidth << "    "
            << std::setw(2) << sp.outputBit << "   "
            << std::setw(2) << sp.outputRepeat << "      "
            << (sp.multicolorAtX ? 1 : 0) << "     "
            << (sp.xExpandedAtX ? 1 : 0) << "    "
            << (sp.enabledAtX ? 1 : 0)
            << "\n";
    }

    oss << "\nNotes:\n";
    oss << "  Row = mcBase / 3. CurRow tracks physical raster line in Y-expanded mode.\n";
    oss << "  RowLat means rowDataLatched: a 3-byte sprite row is available for output.\n";
    oss << "  Mode@X / Exp@X / En@X are sampled at the sprite's current X start.\n";
    oss << "  ShiftBytes are the currently latched 3-byte sprite row.\n";

    oss << "\nLast collision timing:\n";

    if (snap.spriteSpriteCollision.valid)
    {
        oss << "  sprite-sprite:"
            << " raster=" << snap.spriteSpriteCollision.raster
            << " x=" << snap.spriteSpriteCollision.x
            << " cycle=" << snap.spriteSpriteCollision.cycle
            << " bits=$"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(snap.spriteSpriteCollision.bits)
            << std::dec << std::nouppercase << std::setfill(' ')
            << "\n";
    }
    else
    {
        oss << "  sprite-sprite: none\n";
    }

    if (snap.spriteBackgroundCollision.valid)
    {
        oss << "  sprite-background:"
            << " raster=" << snap.spriteBackgroundCollision.raster
            << " x=" << snap.spriteBackgroundCollision.x
            << " cycle=" << snap.spriteBackgroundCollision.cycle
            << " bits=$"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(snap.spriteBackgroundCollision.bits)
            << std::dec << std::nouppercase << std::setfill(' ')
            << "\n";
    }
    else
    {
        oss << "  sprite-background: none\n";
    }

    return oss.str();
}

std::string MLMonitorBackend::vicDumpCurrentCycleDebug() const
{
    if (!vic)
        return "VIC not available\n";

    return vicDumpCycleDebugFor(
        static_cast<int>(vic->getCurrentRaster()),
        vic->getCurrentCycleForDebug()
    );
}

std::string MLMonitorBackend::vicDumpCycleDebugFor(int raster, int cycle) const
{
    if (!vic)
        return "VIC not available\n";

    const auto s = vic->getCycleDebugSnapshot(raster, cycle);

    if (!s.valid)
        return s.error;

    const auto& slot = s.slot;

    const bool spriteSlotActive =
        slot.spriteIndex >= 0 &&
        slot.spriteIndex < 8 &&
        s.sprite.valid &&
        s.sprite.active;

    const bool ptrMismatch =
        spriteSlotActive &&
        vicFetchKindIsSpritePointer(slot.fetchKind) &&
        slot.busOwner != Vic::BusOwner::SpritePointer;

    const bool dataMismatch =
        spriteSlotActive &&
        vicFetchKindIsSpriteData(slot.fetchKind) &&
        slot.busOwner != Vic::BusOwner::SpriteData;

    std::ostringstream out;

    out << "VIC Cycle Debug\n\n";
    out << "Current raster: " << s.currentRaster << "\n";
    out << "Current cycle : " << s.currentCycle << "\n";
    out << "Live sample   : " << (s.liveSample ? "Yes" : "No") << "\n";

    if (!s.liveSample)
    {
        out << "NOTE: Fetch/owner/BA/AEC are calculated for the requested raster/cycle, "
            << "but VC/RC/VMLI/sprite DMA fields are current live state.\n";
    }

    out << "Raster: " << s.requestedRaster << "\n";
    out << "Cycle : " << s.requestedCycle << "\n";
    out << "Fetch : " << vicFetchKindName(slot.fetchKind) << "\n";
    out << "Owner : " << vicBusOwnerName(slot.busOwner) << "\n";
    out << "Badline active: " << (s.badLine ? "Yes" : "No") << "\n";

    out << "Live VCBASE: " << s.liveVcBase << "\n";
    out << "Live VMLI  : " << int(s.liveVmliFetchIndex) << "\n";
    out << "Live RC    : " << int(s.liveRc) << "\n";

    out << "BA    : " << (slot.baLow ? "Low" : "High") << "\n";
    out << "AEC   : " << (slot.aecLow ? "Low" : "High") << "\n";

    out << "Badline:"
        << " warning=" << bit(slot.badlineWarning)
        << " steal=" << bit(slot.badlineSteal)
        << " hold=" << bit(slot.badlineBAHold)
        << "\n";

    out << "Sprite:"
        << " index=" << slot.spriteIndex
        << " byte=" << slot.spriteByteIndex
        << " warning=" << bit(slot.spriteWarning)
        << " steal=" << bit(slot.spriteSteal)
        << " aecSteal=" << bit(slot.spriteAECSteal)
        << "\n";

    if (s.sprite.valid)
    {
        out << "Sprite DMA detail:"
            << " active=" << bit(s.sprite.active)
            << " mc=" << int(s.sprite.mc)
            << " mcBase=" << int(s.sprite.mcBase)
            << " row=" << s.sprite.currentRow
            << " rowLatched=" << bit(s.sprite.rowLatched)
            << " ptr=$" << std::hex << std::uppercase
            << std::setw(2) << std::setfill('0') << int(s.sprite.pointerByte)
            << " dataBase=$"
            << std::setw(4) << int(s.sprite.dataBase)
            << std::dec << std::nouppercase << std::setfill(' ')
            << "\n";
    }

    out << "Refresh: " << bit(slot.refresh) << "\n";
    out << "Raster IRQ:"
    << " sample=" << bit(slot.rasterIrqSample)
    << " target=" << s.rasterIrqTarget
    << " inRange=" << bit(s.rasterIrqTargetInRange)
    << " match=" << bit(s.rasterIrqCompareMatch)
    << " enabled=" << bit(s.rasterIrqEnabled)
    << " pending=" << bit(s.rasterIrqPending)
    << " line=" << bit(s.irqLineActiveNow)
    << "\n";

    out << "Matrix fetch index: " << slot.matrixFetchIndex << "\n";

    out << "DEN seen on $30: " << (s.denSeenOn30 ? "Yes" : "No") << "\n";
    out << "DEN@raster: " << (s.denAtRaster ? "On" : "Off") << "\n";

    out << "Live DisplayRow: " << s.liveDisplayRow << "\n";
    out << "FineY: " << int(s.fineY) << "\n";
    out << "FineX: " << int(s.fineX) << "\n";

    out << "BA src: "
        << (slot.badlineWarning ? "badline-warning " : "")
        << (slot.badlineSteal   ? "badline-steal "   : "")
        << (slot.badlineBAHold  ? "badline-hold "    : "")
        << (slot.spriteWarning  ? "sprite-warning "  : "")
        << (slot.spriteSteal    ? "sprite-steal "    : "")
        << ((!slot.badlineWarning &&
             !slot.badlineSteal &&
             !slot.badlineBAHold &&
             !slot.spriteWarning &&
             !slot.spriteSteal) ? "none" : "")
        << "\n";

    out << "AEC src: "
        << (slot.badlineSteal   ? "badline-steal " : "")
        << (slot.spriteAECSteal ? "sprite-steal "  : "")
        << ((!slot.badlineSteal && !slot.spriteAECSteal) ? "none" : "")
        << "\n";

    if (ptrMismatch)
        out << "WARNING: sprite pointer fetch does not have SpritePointer bus owner\n";

    if (dataMismatch)
        out << "WARNING: sprite data fetch does not have SpriteData bus owner\n";

    out << "\nSprite DMA active:";
    bool any = false;
    for (int i = 0; i < 8; ++i)
    {
        if (s.spriteDmaActive[i])
        {
            out << " " << i;
            any = true;
        }
    }
    if (!any)
        out << " none";

    out << "\nSprite row latched:";
    any = false;
    for (int i = 0; i < 8; ++i)
    {
        if (s.spriteRowLatched[i])
        {
            out << " " << i;
            any = true;
        }
    }
    if (!any)
        out << " none";

    out << "\n";

    return out.str();
}

std::string MLMonitorBackend::vicDumpRegs(const std::string& group) const
{
    if (!vic)
        return "VIC not attached\n";

    const auto s = vic->getRegisterDebugSnapshot();

    std::ostringstream out;

    const auto showRaster =
        group == "all" || group == "raster";

    const auto showIrq =
        group == "all" || group == "irq";

    const auto showSprites =
        group == "all" || group == "sprites";

    const auto showLatch =
        group == "all" || group == "latch";

    const auto showCollisions =
        group == "all" || group == "collisions";

    const auto showColors =
        group == "all" || group == "colors";

    const auto showPos =
        group == "all" || group == "pos";

    if (showRaster)
    {
        const uint8_t rasterHi = (s.currentRaster >> 8) & 0x01;
        const uint8_t d011Read = (s.control & 0x7F) | (rasterHi << 7);

        out << "Raster and Control Registers:\n\n";

        out << "D011 = $" << hex2(d011Read)
            << "   (CTRL1: YSCROLL = " << int(s.control & 0x07)
            << ", 25row = " << ((s.control & 0x08) ? "Yes" : "No")
            << ", Screen = " << ((s.control & 0x10) ? "On" : "Off")
            << ", Bitmap = " << ((s.control & 0x20) ? "Yes" : "No")
            << ", ECM = " << ((s.control & 0x40) ? "Yes" : "No")
            << ", RasterHi = " << int(rasterHi) << ")\n";

        out << "D012 = $" << hex2(static_cast<uint8_t>(s.currentRaster & 0xFF))
            << "   (RASTER = " << std::dec << s.currentRaster << ")\n";

        out << "D016 = $" << hex2(s.control2)
            << "   (CTRL2: XSCROLL = " << int(s.control2 & 0x07)
            << ", 40COL = " << ((s.control2 & 0x08) ? "Yes" : "No")
            << ", Multicolor = " << ((s.control2 & 0x10) ? "Yes" : "No")
            << ")\n";

        out << "D018 = $" << hex2(s.memoryPointer)
            << "   (Memory Pointer)\n";

        out << "Raster IRQ target = " << s.rasterInterruptLine
            << " ($" << hex4(s.rasterInterruptLine) << ")\n\n";
    }

    if (showIrq)
    {
        out << "Interrupt Registers:\n\n";

        out << "D019 = $" << hex2(s.interruptStatus)
            << "   pending=$" << hex2(s.interruptStatus & 0x0F)
            << " irqFlag=" << ((s.interruptStatus & 0x80) ? 1 : 0)
            << "\n";

        out << "D01A = $" << hex2(s.interruptEnable & 0x0F)
            << "   enabled mask=$" << hex2(s.interruptEnable & 0x0F)
            << "\n";

        out << "IRQ line active = " << (s.irqLineActive ? "Yes" : "No") << "\n";
        out << "Compare cycle   = " << s.rasterIrqCompareCycle
            << "   Sampled this line=" << (s.rasterIrqSampledThisLine ? "Yes" : "No")
            << "\n";

        out << "Compare match   = " << (s.rasterCompareMatchesNow ? "Yes" : "No")
            << "   targetInRange=" << (s.rasterIrqTargetInRange ? 1 : 0)
            << "\n";

        out << "Last Raster IRQ Sample:\n";
        if (s.lastRasterIrqSample.valid)
        {
            out << "  reason=" << s.lastRasterIrqSample.reason
                << " raster=" << s.lastRasterIrqSample.raster
                << " cycle=" << s.lastRasterIrqSample.cycle
                << " visibleRaster=" << s.lastRasterIrqSample.visibleRaster
                << " target=" << s.lastRasterIrqSample.targetRaster
                << " targetInRange=" << (s.lastRasterIrqSample.targetInRange ? 1 : 0)
                << " matched=" << (s.lastRasterIrqSample.matched ? 1 : 0)
                << " sampledBefore=" << (s.lastRasterIrqSample.sampledBefore ? 1 : 0)
                << "\n";
        }
        else
        {
            out << "  none\n";
        }

        out << "\n";
    }

    if (showSprites)
    {
        out << "Sprite Control Registers:\n\n";

        out << "D015 = $" << hex2(s.spriteEnabled)
            << " (Enable Mask: " << std::bitset<8>(s.spriteEnabled) << ")\n";

        out << "D017 = $" << hex2(s.spriteYExpansion)
            << " (Y-Expand: " << std::bitset<8>(s.spriteYExpansion) << ")\n";

        out << "D01B = $" << hex2(s.spritePriority)
            << " (Priority: " << std::bitset<8>(s.spritePriority) << ")\n";

        out << "D01C = $" << hex2(s.spriteMultiColor)
            << " (Multicolor: " << std::bitset<8>(s.spriteMultiColor) << ")\n";

        out << "D01D = $" << hex2(s.spriteXExpansion)
            << " (X-Expand: " << std::bitset<8>(s.spriteXExpansion) << ")\n\n";
    }

    if (showLatch)
    {
        out << "Latched VIC Registers:\n\n";
        out << "Current raster = " << s.currentRaster << "\n";
        out << "D011 latch = $" << hex2(s.latchedD011) << "\n";
        out << "D016 latch = $" << hex2(s.latchedD016) << "\n";
        out << "D018 latch = $" << hex2(s.latchedD018) << "\n";
        out << "DD00 bank  = $" << hex4(s.latchedDD00) << "\n";
        out << "VIC bank   = $" << hex4(s.vicBankBase) << "\n";
        out << "Char base  = $" << hex4(s.charBase) << "\n";
        out << "Screen base= $" << hex4(s.screenBase) << "\n";
        out << "Bitmap base= $" << hex4(s.bitmapBase) << "\n\n";
    }

    if (showCollisions)
    {
        out << "Collision Registers:\n\n";
        out << "D01E = $" << hex2(s.spriteCollision)
            << " (Sprite/Sprite collision latch)\n";
        out << "D01F = $" << hex2(s.spriteDataCollision)
            << " (Sprite/Background collision latch)\n\n";
    }

    if (showColors)
    {
        out << "Color Registers:\n\n";

        out << "D020 = $" << hex2(s.borderColor)
            << " (" << c64ColorName(s.borderColor) << ") Border\n";

        out << "D021 = $" << hex2(s.backgroundColor0)
            << " (" << c64ColorName(s.backgroundColor0) << ") Background 0\n";

        for (int i = 0; i < 3; ++i)
        {
            out << "D0" << std::uppercase << std::hex << (0x22 + i)
                << " = $" << hex2(s.backgroundColor[i])
                << " (" << c64ColorName(s.backgroundColor[i])
                << ") Background " << std::dec << (i + 1) << "\n";
        }

        out << "D025 = $" << hex2(s.spriteMultiColor1)
            << " (" << c64ColorName(s.spriteMultiColor1) << ") Sprite multicolor 1\n";

        out << "D026 = $" << hex2(s.spriteMultiColor2)
            << " (" << c64ColorName(s.spriteMultiColor2) << ") Sprite multicolor 2\n";

        for (int i = 0; i < 8; ++i)
        {
            out << "D0" << std::uppercase << std::hex << (0x27 + i)
                << " = $" << hex2(s.spriteColors[i])
                << " (" << c64ColorName(s.spriteColors[i])
                << ") Sprite " << std::dec << i << "\n";
        }

        out << "\n";
    }

    if (showPos)
    {
        out << "Sprite Position Registers:\n\n";

        out << "D010 = $" << hex2(s.spriteXMsb)
            << " (X MSB mask: " << std::bitset<8>(s.spriteXMsb) << ")\n\n";

        for (int i = 0; i < 8; ++i)
        {
            const int x = int(s.spriteX[i]) |
                ((s.spriteXMsb & (1 << i)) ? 0x100 : 0);

            out << "Sprite " << i
                << ": X=" << std::setw(3) << x
                << " Y=" << std::setw(3) << int(s.spriteY[i])
                << "  raw D0" << std::uppercase << std::hex << std::setw(2)
                << std::setfill('0') << (i * 2)
                << "=$" << hex2(s.spriteX[i])
                << " D0" << std::setw(2) << (i * 2 + 1)
                << "=$" << hex2(s.spriteY[i])
                << std::dec << std::setfill(' ')
                << "\n";
        }

        out << "\n";
    }

    if (!showRaster && !showIrq && !showSprites && !showLatch &&
        !showCollisions && !showColors && !showPos)
    {
        out << "Unknown VIC register group: " << group << "\n";
    }

    return out.str();
}

std::string MLMonitorBackend::vicDumpBadlineState() const
{
    if (!vic)
        return "VIC not attached\n";

    const auto s = vic->getBadlineDebugSnapshot();

    std::ostringstream oss;

    oss << "VIC badline/display row state\n";
    oss << "  raster=" << s.raster
        << " cycle=" << s.cycle << "\n";

    oss << "  badLine=" << s.badLine
        << " badLineSampled=" << s.badLineSampled << "\n";

    oss << "  displayEnabled=" << s.displayEnabled
        << " displayEnabledNext=" << s.displayEnabledNext << "\n";

    oss << "  denSeenOn30=" << s.denSeenOn30
        << " firstBadlineY=" << s.firstBadlineY << "\n";


    oss << "  vcBase=" << s.vcBase
        << " vmliBase=" << s.vmliBase
        << " vmliFetchIndex=" << static_cast<int>(s.vmliFetchIndex)
        << " rc=" << static_cast<int>(s.rc) << "\n";

    return oss.str();
}

std::string MLMonitorBackend::vicDumpBorderState() const
{
    if (!vic)
        return "VIC not available\n";

    const auto s = vic->getRegisterDebugSnapshot();

    std::ostringstream oss;

    oss << "VIC border state\n";
    oss << "  raster=" << s.currentRaster
        << " cycle=" << s.currentCycle << "\n";

    oss << "  live verticalBorder="
        << (s.liveVerticalBorder ? "on" : "off")
        << " latched verticalBorder="
        << (s.latchedVerticalBorder ? "on" : "off")
        << " match="
        << ((s.liveVerticalBorder == s.latchedVerticalBorder) ? "yes" : "NO")
        << "\n";

    oss << "  horizontal border window:\n"
        << "    latched openX=" << s.latchedBorderOpenX
        << " closeX=" << s.latchedBorderCloseX << "\n"
        << "    mask innerX0=" << s.maskInnerX0
        << " innerX1=" << s.maskInnerX1 << "\n";

    oss << "  live border flags:"
        << " verticalBorder=" << (s.liveVerticalBorder ? "on" : "off")
        << " leftBorder=" << (s.liveLeftBorder ? "on" : "off")
        << " rightBorder=" << (s.liveRightBorder ? "on" : "off")
        << "\n";

    oss << "  live border window:"
        << " leftBorderOpenX=" << s.liveLeftBorderOpenX
        << " rightBorderCloseX=" << s.liveRightBorderCloseX
        << "\n";

    oss << "  latched RSEL=" << ((s.latchedD011 & 0x08) ? 1 : 0)
        << " latched CSEL=" << ((s.latchedD016 & 0x08) ? 1 : 0)
        << "\n";

    oss << "  latched D011=$"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(s.latchedD011)
        << " latched D016=$"
        << std::setw(2)
        << static_cast<int>(s.latchedD016)
        << std::dec << std::nouppercase << std::setfill(' ')
        << "\n";

    oss << "  live D011=$"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(s.control)
        << " live D016=$"
        << std::setw(2)
        << static_cast<int>(s.control2)
        << std::dec << std::nouppercase << std::setfill(' ')
        << "\n";

    oss << "  verticalWindow topOpen=" << s.verticalTopOpen
        << " bottomClose=" << s.verticalBottomClose << "\n";

    oss << "  latched border:"
        << " verticalBorder=" << (s.latchedVerticalBorder ? "on" : "off")
        << " openX=" << s.latchedBorderOpenX
        << " closeX=" << s.latchedBorderCloseX
        << "\n";

    return oss.str();
}

void MLMonitorBackend::vicFFRaster(uint8_t targetRaster)
{
    while(vic->getCurrentRaster() != targetRaster)
    {
        vic->tick(1);
        cpu->tick();
        cia1->updateTimers(1);
        cia2->updateTimers(1);
        sid->tick(1);
    }
}

void MLMonitorBackend::enterMonitor()
{
    if (comp) comp->enterMonitor();
}

void MLMonitorBackend::coldReset()
{
    if (comp) comp->coldReset();
    else std::cerr << "Error: No Computer attached, cannot perform reset!\n";
}

void MLMonitorBackend::warmReset()
{
    if (comp) comp->warmReset();
    else std::cerr << "Error: No Computer attached, cannot perform reset!\n";
}

void MLMonitorBackend::irqForceOn()
{
    if (irq)
        irq->raiseIRQ(IRQLine::MONITOR);
}

void MLMonitorBackend::irqForceOff()
{
    if (irq)
        irq->clearIRQ(IRQLine::MONITOR);
}

void MLMonitorBackend::irqDisableAll()
{
    if (!vic && !cia1 && !cia2) return;

    irqForceOff();

    snapshot.has = true;
    snapshot.vic  = vic->snapshotIRQs();
    snapshot.cia1 = cia1->snapshotIRQs();
    snapshot.cia2 = cia2->snapshotIRQs();

    vic->disableAllIRQs();
    cia1->disableAllIRQs();
    cia2->disableAllIRQs();

    irqClearAll();  // acknowledge anything pending after the mask change
}

void MLMonitorBackend::irqClearAll()
{
    if (!vic && !cia1 && !cia2) return;

    irqForceOff();

    vic->clearPendingIRQs();
    cia1->clearPendingIRQs();
    cia2->clearPendingIRQs();
}

void MLMonitorBackend::irqRestore()
{
    if (!vic && !cia1 && !cia2) return;
    if (!snapshot.has) return;

    irqForceOff();

    vic->restoreIRQs(snapshot.vic);
    cia1->restoreIRQs(snapshot.cia1);
    cia2->restoreIRQs(snapshot.cia2);
}

void MLMonitorBackend::setPC(uint16_t value)
{
    if (!cpu)
        return;

    cpu->setPC(value);
    cpu->forceInstructionBoundaryForMonitor();
}

void MLMonitorBackend::cpuStepInstruction()
{
    if (!cpu)
        return;

    // Start or continue the current instruction.
    cpu->tick();

    // Finish the instruction by consuming its remaining cycles.
    int guard = 128;

    while (!cpu->isAtInstructionBoundary() && guard-- > 0)
    {
        cpu->tick();
    }
}

std::string MLMonitorBackend::cpuAddressStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastAddressDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    auto modeName = [](CPU::CPUAddressDebugMode mode)
    {
        switch (mode)
        {
            case CPU::CPUAddressDebugMode::IndirectX:         return "($nn,X)";
            case CPU::CPUAddressDebugMode::IndirectY:         return "($nn),Y";
            case CPU::CPUAddressDebugMode::IndirectYBoundary: return "($nn),Y read";
            default:                                          return "None";
        }
    };

    std::ostringstream out;

    out << "Last Addressing Mode\n";
    out << "--------------------\n";

    if (!s.valid)
    {
        out << "No indirect addressing mode has been recorded yet.\n";
        return out.str();
    }

    out << "Mode:           " << modeName(s.mode) << "\n";
    out << "Operand PC:     $" << hexWord(s.operandPC) << "\n";
    out << "ZP operand:     $" << hexByte(s.zpOperand) << "\n";
    out << "Index value:    $" << hexByte(s.indexValue) << "\n";
    out << "Indexed ZP:     $" << hexByte(s.indexedZP) << "\n";
    out << "Pointer low:    $" << hexByte(s.pointerLowAddr)
        << " -> $" << hexByte(s.pointerLowValue) << "\n";
    out << "Pointer high:   $" << hexByte(s.pointerHighAddr)
        << " -> $" << hexByte(s.pointerHighValue) << "\n";
    out << "Base address:   $" << hexWord(s.baseAddress) << "\n";
    out << "Effective addr: $" << hexWord(s.effectiveAddress) << "\n";
    out << "Page crossed:   " << (s.pageCrossed ? "yes" : "no") << "\n";

    if (s.dummyReadUsed)
        out << "Dummy read:     $" << hexWord(s.dummyReadAddress) << "\n";
    else
        out << "Dummy read:     none\n";

    if (s.valueRead != 0 || s.mode == CPU::CPUAddressDebugMode::IndirectYBoundary)
        out << "Value read:     $" << hexByte(s.valueRead) << "\n";

    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuBranchStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastBranchDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last Branch\n";
    out << "-----------\n";

    if (!s.valid)
    {
        out << "No branch has been recorded yet.\n";
        return out.str();
    }

    out << "Opcode PC:      $" << hexWord(s.opcodePC) << "\n";
    out << "Opcode:         $" << hexByte(s.opcode) << "\n";
    out << "Mnemonic:       " << (s.mnemonic ? s.mnemonic : "") << "\n";
    out << "Condition:      " << (s.condition ? "true" : "false") << "\n";
    out << "Taken:          " << (s.taken ? "yes" : "no") << "\n";
    out << "Offset:         " << std::dec << int(s.offset) << "\n";
    out << "Operand PC:     $" << hexWord(s.operandPC) << "\n";
    out << "Old PC:         $" << hexWord(s.oldPC) << "\n";
    out << "New PC:         $" << hexWord(s.newPC) << "\n";
    out << "Page crossed:   " << (s.pageCrossed ? "yes" : "no") << "\n";

    if (s.taken)
        out << "Taken dummy:    $" << hexWord(s.takenDummyRead) << "\n";
    else
        out << "Taken dummy:    none\n";

    if (s.pageCrossed)
        out << "Page dummy:     $" << hexWord(s.pageCrossDummyRead) << "\n";
    else
        out << "Page dummy:     none\n";

    out << "Extra cycles:   " << std::dec << int(s.extraCycles) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuInterruptStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastInterruptEntryDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    auto typeName = [](CPU::InterruptEntryType type)
    {
        switch (type)
        {
            case CPU::InterruptEntryType::IRQ: return "IRQ";
            case CPU::InterruptEntryType::NMI: return "NMI";
            case CPU::InterruptEntryType::BRK: return "BRK";
            default: return "None";
        }
    };

    std::ostringstream out;

    out << "Last Interrupt Entry\n";
    out << "--------------------\n";
    out << "Type:          " << typeName(s.type) << "\n";

    if (s.type == CPU::InterruptEntryType::None)
    {
        out << "No interrupt entry has been recorded yet.\n";
        return out.str();
    }

    out << "Accepted PC:   $" << hexWord(s.acceptedAtPC) << "\n";
    out << "Pushed return: $" << hexWord(s.pushedReturnPC) << "\n";
    out << "Pushed SR:     $" << hexByte(s.pushedSR) << "\n";
    out << "Vector addr:   $" << hexWord(s.vectorAddress) << "\n";
    out << "Vector target: $" << hexWord(s.vectorTarget) << "\n";
    out << "Total cycles:  " << std::dec << s.totalCycles << "\n";

    out << "SP before:     $" << hexByte(s.spBefore) << "\n";
    out << "SP after:      $" << hexByte(s.spAfter) << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuIrqStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getIrqDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    auto flagsString = [](uint8_t sr)
    {
        std::string f;
        f += (sr & 0x80) ? 'N' : '.';
        f += (sr & 0x40) ? 'V' : '.';
        f += '-';
        f += (sr & 0x10) ? 'B' : '.';
        f += (sr & 0x08) ? 'D' : '.';
        f += (sr & 0x04) ? 'I' : '.';
        f += (sr & 0x02) ? 'Z' : '.';
        f += (sr & 0x01) ? 'C' : '.';
        return f;
    };

    std::ostringstream out;

    out << "CPU IRQ/NMI State\n";
    out << "-----------------\n";

    out << "PC:              $" << hexWord(s.pc) << "\n";
    out << "SR:              $" << hexByte(s.sr)
        << "  " << flagsString(s.sr) << "\n";

    out << "I flag:          " << (s.iFlag ? "set" : "clear") << "\n";
    out << "IRQ line:        " << (s.irqLineActive ? "active" : "inactive") << "\n";
    out << "NMI line:        " << (s.nmiLine ? "high" : "low") << "\n";
    out << "NMI pending:     " << (s.nmiPending ? "yes" : "no") << "\n";
    out << "IRQ suppress:    " << (s.irqSuppressOne ? "yes" : "no") << "\n";
    out << "RDY/BA line:     " << (s.rdyLine ? "high / released" : "low / CPU wait") << "\n";
    out << "AEC line:        " << (s.aecLine ? "high / CPU bus enabled" : "low / VIC owns bus") << "\n";
    out << "SO level:        " << (s.soLevel ? "high" : "low") << "\n";
    out << "Cycles left:     " << s.cyclesRemaining << "\n";
    out << "Total cycles:    " << s.totalCycles << "\n";

    out << "\nLast instruction:\n";
    out << "PC:              $" << hexWord(s.lastOpcodePC) << "\n";
    out << "Opcode:          $" << hexByte(s.lastOpcode) << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuCycleStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    const auto s = cpu->getCycleDebugState();

    std::ostringstream out;

    out << "CPU Cycle State\n";
    out << "---------------\n";

    out << "Total cycles:       " << s.totalCycles << "\n";
    out << "Cycles remaining:   " << s.cyclesRemaining << "\n";
    out << "Between instr:      " << (s.betweenInstructions ? "yes" : "no") << "\n";
    out << "RDY/BA line:        " << (s.rdyLine ? "high / released" : "low / CPU wait") << "\n";
    out << "AEC line:           " << (s.aecLine ? "high / CPU bus enabled" : "low / VIC owns bus") << "\n";
    out << "Halted/JAM:         " << (s.halted ? "yes" : "no") << "\n";

    out << "\nVideo timing:\n";
    out << "  Mode:             " << (s.mode == VideoMode::PAL ? "PAL" : "NTSC") << "\n";
    out << "  Frame cycles:     " << s.frameCycle << " / " << s.cyclesPerFrame << "\n";
    out << "  Raster/Dot:       " << s.raster << " / " << s.dot << "\n";

    out << "\nLast instruction:\n";
    out << "PC:                 $" << hexWord(s.lastOpcodePC) << "\n";
    out << "Opcode:             $" << hexByte(s.lastOpcode) << "\n";

    out << "\nBus cycle:\n";
    out << "  Active:           " << (s.busCycleActive ? "yes" : "no") << "\n";
    out << "  Type:             " << cpuBusCycleTypeName(s.busCycleType) << "\n";
    out << "  Address:          $" << hexWord(s.busAddress) << "\n";
    out << "  Value:            $" << hexByte(s.busValue) << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuJMPStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastJMPDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last JMP\n";
    out << "--------\n";

    if (!s.valid)
    {
        out << "No JMP has been recorded yet.\n";
        return out.str();
    }

    out << "JMP opcode PC:  $" << hexWord(s.jmpOpcodePC) << "\n";
    out << "Opcode:         $" << hexByte(s.opcode) << "\n";
    out << "Mode:           " << (s.indirect ? "indirect" : "absolute") << "\n";
    out << "Operand addr:   $" << hexWord(s.operandAddress) << "\n";

    if (s.indirect)
    {
        out << "Pointer addr:   $" << hexWord(s.pointerAddress) << "\n";
        out << "Low read addr:  $" << hexWord(s.lowReadAddress) << "\n";
        out << "High read addr: $" << hexWord(s.highReadAddress) << "\n";
        out << "Low/high bytes: $" << hexByte(s.lowByte)
            << " / $" << hexByte(s.highByte) << "\n";
        out << "Page bug:       " << (s.indirectPageBug ? "yes" : "no") << "\n";
    }
    else
    {
        out << "Target low/high:$" << hexByte(s.lowByte)
            << " / $" << hexByte(s.highByte) << "\n";
    }

    out << "Final PC:       $" << hexWord(s.finalPC) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuJSRStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastJSRDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last JSR Stack Push\n";
    out << "-------------------\n";

    if (!s.valid)
    {
        out << "No JSR has been recorded yet.\n";
        return out.str();
    }

    out << "JSR opcode PC:  $" << hexWord(s.jsrOpcodePC) << "\n";
    out << "Target PC:      $" << hexWord(s.targetPC) << "\n";
    out << "Pushed return:  $" << hexWord(s.pushedReturn) << "\n";
    out << "Pushed high/low:$" << hexByte(s.pushedHigh)
        << " / $" << hexByte(s.pushedLow) << "\n";
    out << "SP before:      $" << hexByte(s.spBefore) << "\n";
    out << "SP after:       $" << hexByte(s.spAfter) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuPHAStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastPHADebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last PHA Stack Push\n";
    out << "-------------------\n";

    if (!s.valid)
    {
        out << "No PHA has been recorded yet.\n";
        return out.str();
    }

    out << "PHA opcode PC:  $" << hexWord(s.phaOpcodePC) << "\n";
    out << "Pushed A:       $" << hexByte(s.pushedA) << "\n";
    out << "SP before:      $" << hexByte(s.spBefore) << "\n";
    out << "SP after:       $" << hexByte(s.spAfter) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuPHPStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastPHPDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last PHP Status Push\n";
    out << "--------------------\n";

    if (!s.valid)
    {
        out << "No PHP has been recorded yet.\n";
        return out.str();
    }

    out << "PHP opcode PC:  $" << hexWord(s.phpOpcodePC) << "\n";
    out << "Internal SR:    $" << hexByte(s.internalSR) << "\n";
    out << "Pushed SR:      $" << hexByte(s.pushedSR) << "\n";
    out << "SP before:      $" << hexByte(s.spBefore) << "\n";
    out << "SP after:       $" << hexByte(s.spAfter) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuPLAStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastPLADebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last PLA Stack Pull\n";
    out << "-------------------\n";

    if (!s.valid)
    {
        out << "No PLA has been recorded yet.\n";
        return out.str();
    }

    out << "PLA opcode PC:  $" << hexWord(s.plaOpcodePC) << "\n";
    out << "Pulled A:       $" << hexByte(s.pulledA) << "\n";
    out << "Final A:        $" << hexByte(s.finalA) << "\n";
    out << "Z flag:         " << (s.zFlag ? "set" : "clear") << "\n";
    out << "N flag:         " << (s.nFlag ? "set" : "clear") << "\n";
    out << "SP before:      $" << hexByte(s.spBefore) << "\n";
    out << "SP after:       $" << hexByte(s.spAfter) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuPLPStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastPLPDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last PLP Status Restore\n";
    out << "-----------------------\n";

    if (!s.valid)
    {
        out << "No PLP has been recorded yet.\n";
        return out.str();
    }

    out << "PLP opcode PC:  $" << hexWord(s.plpOpcodePC) << "\n";
    out << "Pulled SR:      $" << hexByte(s.pulledSR) << "\n";
    out << "Final SR:       $" << hexByte(s.finalSR) << "\n";
    out << "SP before:      $" << hexByte(s.spBefore) << "\n";
    out << "SP after:       $" << hexByte(s.spAfter) << "\n";
    out << "I old/new:      " << (s.oldI ? "1" : "0")
        << " -> " << (s.newI ? "1" : "0") << "\n";
    out << "IRQ suppress:   " << (s.irqSuppressSet ? "set" : "not set") << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuRTIStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto rti = cpu->getLastRTIDebugState();
    const auto intr = cpu->getLastInterruptEntryDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last RTI Return\n";
    out << "---------------\n";

    if (!rti.valid)
    {
        out << "No RTI has been recorded yet.\n";

        if (intr.type != CPU::InterruptEntryType::None)
            out << "Compared to intr: no RTI recorded after last interrupt entry\n";

        return out.str();
    }

    out << "RTI opcode PC:  $" << hexWord(rti.rtiOpcodePC) << "\n";
    out << "Pulled SR:      $" << hexByte(rti.pulledSR) << "\n";
    out << "Final SR:       $" << hexByte(rti.finalSR) << "\n";
    out << "Pulled PCL/PCH: $" << hexByte(rti.pulledPCL)
        << " / $" << hexByte(rti.pulledPCH) << "\n";
    out << "Return PC:      $" << hexWord(rti.returnPC) << "\n";
    out << "SP before:      $" << hexByte(rti.spBefore) << "\n";
    out << "SP after:       $" << hexByte(rti.spAfter) << "\n";
    out << "I old/new:      " << (rti.oldI ? "1" : "0")
        << " -> " << (rti.newI ? "1" : "0") << "\n";
    out << "IRQ suppress:   " << (rti.irqSuppressSet ? "set" : "not set") << "\n";
    out << "Total cycles:   " << std::dec << rti.totalCycles << "\n";

    if (intr.type != CPU::InterruptEntryType::None)
    {
        out << "Compared to intr: ";

        if (rti.totalCycles < intr.totalCycles)
        {
            out << "STALE - RTI occurred before last interrupt entry\n";
        }
        else if (rti.totalCycles == intr.totalCycles)
        {
            out << "same cycle as last interrupt entry\n";
        }
        else
        {
            out << "fresh - RTI occurred after last interrupt entry\n";
        }
    }

    return out.str();
}

std::string MLMonitorBackend::cpuRTSStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastRTSDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last RTS Stack Pull\n";
    out << "-------------------\n";

    if (!s.valid)
    {
        out << "No RTS has been recorded yet.\n";
        return out.str();
    }

    out << "RTS opcode PC:  $" << hexWord(s.rtsOpcodePC) << "\n";
    out << "Pulled return:  $" << hexWord(s.pulledReturn) << "\n";
    out << "Final PC:       $" << hexWord(s.finalPC) << "\n";
    out << "Pulled low/high:$" << hexByte(s.pulledLow)
        << " / $" << hexByte(s.pulledHigh) << "\n";
    out << "SP before:      $" << hexByte(s.spBefore) << "\n";
    out << "SP after:       $" << hexByte(s.spAfter) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuStackStatus(int count) const
{
    if (!cpu)
        return "CPU not attached.\n";

    if (count <= 0)
        count = 16;

    if (count > 256)
        count = 256;

    const uint8_t sp = cpu->getSP();
    const uint8_t first = uint8_t(sp + 1);

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "CPU Stack\n";
    out << "---------\n";
    out << "SP:              $" << hexByte(sp) << "\n";
    out << "Next pop addr:   $" << hexWord(uint16_t(0x0100 | first)) << "\n";
    out << "Count:           " << std::dec << count << "\n\n";

    out << "Pop#  Addr   Value\n";

    for (int i = 0; i < count; ++i)
    {
        const uint8_t stackOffset = uint8_t(first + i);
        const uint16_t addr = uint16_t(0x0100 | stackOffset);
        const uint8_t value = cpu->debugRead(addr);

        out << std::dec << std::setw(4) << i << "  "
            << "$" << hexWord(addr) << "  "
            << "$" << hexByte(value) << "\n";
    }

    return out.str();
}

std::string MLMonitorBackend::cpuLastStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getCycleDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last CPU Instruction\n";
    out << "--------------------\n";
    out << "PC:          $" << hexWord(s.lastOpcodePC) << "\n";
    out << "Opcode:      $" << hexByte(s.lastOpcode) << "\n";
    out << "Cycles left: " << s.cyclesRemaining << "\n";
    out << "Raster/Dot:  " << s.raster << " / " << s.dot << "\n";

    return out.str();
}

void MLMonitorBackend::setJamMode(const std::string& mode)
{
    if (cpu)
    {
        if (mode == "freeze")
        {
            cpu->setJamMode(CPU::JamMode::FreezePC);
        }
        else if (mode == "halt")
        {
            cpu->setJamMode(CPU::JamMode::Halt);
        }
        else if (mode == "nop")
        {
            cpu->setJamMode(CPU::JamMode::NopCompat);
        }
    }
}

void MLMonitorBackend::dumpDriveList()
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    const auto& devs = bus->getDevices();  // map<int, Peripheral*>

    if (devs.empty())
    {
        std::cout << "No IEC devices registered.\n";
        return;
    }

    std::cout << "IEC Devices:\n";
    std::cout << "  ID   Type      Details                          Status      Track   Sector\n";
    std::cout << "  --   -------   ---------------------------      -------     -----   ------\n";

    for (const auto& [id, dev] : devs)
    {
        if (!dev)
            continue;

        if (dev->isDrive())
        {
            // Grab a pointer to the drive so we can run drive only methods
            auto drv = dev->asDrive();
            if (!drv)
            {
                std::cerr << "Error: Not a drive\n";
            }

            // Output drive ID, type, and disk image name
            std::string img = drv->getLoadedDiskName();
            std::cout << "  " << id
                      << "    " << drv->getDriveTypeName();

            if (!img.empty())
                std::cout << "       [" << img << "]";

            // Output drive status and current track and sector
            std::string currentStatus = decodeDriveStatus(drv->getDriveStatus());
            std::cout << "    " << currentStatus << "          " << static_cast<int>(drv->getCurrentTrack());
            std::cout << "        " << static_cast<int>(drv->getCurrentSector()) << "\n";
        }
        else
        {
            std::cout << "  " << id << "    (non-drive IEC device)\n";
        }
    }
}

void MLMonitorBackend::dumpDriveSummary(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    Drive* drive = dev->asDrive();

    // Get Drive status
    std::string currentStatus = decodeDriveStatus(dev->asDrive()->getDriveStatus());

    std::stringstream oss;
    oss << "Drive " << id << " Summary:\n";
    oss << "  Type:        " << drive->getDriveTypeName() << "\n";
    oss << "  Image:       " << drive->getLoadedDiskName() << "\n";
    oss << "  Disk Loaded: " << (drive->isDiskLoaded() ? "Yes" : "No") << "\n\n";
    oss << "  Track:       " << static_cast<int>(drive->getCurrentTrack()) << "\n";
    oss << "  Sector:      " << static_cast<int>(drive->getCurrentSector()) << "\n";
    oss << "  Motor:       " << (drive->isMotorOn() ? "On" : "Off") << "\n\n";
    oss << "  ATN Line:    " << (drive->getAtnLineLow() ? "Low" : "High") << "\n";
    oss << "  CLK Line:    " << (drive->getClkLineLow() ? "Low" : "High") << "\n";
    oss << "  DATA Line:   " << (drive->getDataLineLow() ? "Low" : "High") << "\n\n";
    oss << "  Status:      " << currentStatus << "\n";
    std::cout << oss.str();
}

void MLMonitorBackend::dumpDriveCIA(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    Drive* drive = dev->asDrive();

    if (!drive->hasCIA())
    {
        std::cout << "Device has no CIA chip\n";
        return;
    }

    auto* cia = dev->asDrive()->getCIA();
    auto registers = cia->getRegsView();

    std::stringstream oss;
    oss << "CIA Registers:\n";
    oss << std::left;

    // Ports
    oss << "  PORTA:  " << std::setw(10) << "" << "$" << hex2(registers.portA)
        << std::setw(6) << "" << "PORTB:  " << std::setw(10) << ""
        << "$" << hex2(registers.portB) << "\n";

    oss << "  DDRA:   " << std::setw(10) << "" << "$" <<hex2(registers.ddrA)
        << std::setw(4) << ""
        << "  DDRB:   " << std::setw(10) << "" << "$" << hex2(registers.ddrB) << "\n";

    // Timer bytes
    oss << "  TimerA Low Byte:  $" << hex2(registers.tAL)
        << std::setw(6) << ""
        << "TimerA High Byte: $" << hex2(registers.tAH) << "\n";

    oss << "  TimerB Low Byte:  $" << hex2(registers.tBL)
        << std::setw(6) << ""
        << "TimerB High Byte: $" << hex2(registers.tBH) << "\n";

    // Timer words
    oss << "  TimerA Counter:   $" << hex4(registers.tA)
        << std::setw(4) << "" << "TimerA Latch:  "
        << std::setw(3) << "" << "$" << hex4(registers.taLAT) << "\n";

    oss << "  TimerB Counter:   $" << hex4(registers.tB)
        << std::setw(4) << "" << "TimerB Latch:  "
        << std::setw(3) << "" << "$" << hex4(registers.tbLAT) << "\n";

    // TOD
    oss << "  TOD 10ths:        $" << hex2(registers.tod10)
    << std::setw(6) << ""
    << "TOD Seconds:      $" << hex2(registers.todSec) << "\n";

    oss << "  TOD Minutes:      $" << hex2(registers.todMin)
        << std::setw(6) << ""
        << "TOD Hours:        $" << hex2(registers.todHour) << "\n";

    // Misc (aligned 2-column rows)
    oss << "  Serial Data:      $" << hex2(registers.sd)
        << std::setw(6) << ""
        << "IER:              $" << hex2(registers.ier) << "\n";

    oss << "  CRA:              $" << hex2(registers.cra)
        << std::setw(6) << ""
        << "CRB:              $" << hex2(registers.crb) << "\n";

    oss << "  IFR:              $" << hex2(registers.interruptStatus)
        << std::setw(6) << ""
        << "IER:              $" << hex2(registers.ier) << "\n";

    auto iec = cia->getIECDecodeView();

    if (iec.available)
    {
        oss << "\n";
        oss << "CIA IEC Decode (" << iec.modelName << "):\n";

        oss << "  PR:              $" << hex2(iec.pr)
            << std::setw(6) << ""
            << "DDR:             $" << hex2(iec.ddr) << "\n";

        oss << "  Raw pins:        "
            << "PA=$" << hex2(iec.rawPortAPins)
            << "  PB=$" << hex2(iec.rawPortBPins)
            << "\n";

        oss << "  Cached inputs:   "
            << "ATN="  << (iec.atnInLow  ? "L" : "H")
            << "  CLK=" << (iec.clkInLow  ? "L" : "H")
            << "  DATA=" << (iec.dataInLow ? "L" : "H")
            << "  SRQ=" << (iec.srqInLow  ? "L" : "H")
            << "\n";

        oss << "  Output decode:   "
            << "busDirOutput=" << iec.busDirOutput
            << "  atnAckDataLow=" << iec.atnAckDataLow
            << "  datOutLow=" << iec.datOutAssertLow
            << "  clkOutLow=" << iec.clkOutAssertLow
            << "\n";

        oss << "  Resolved pins:   "
            << "ATN="  << (iec.resolvedAtnLow  ? "L" : "H")
            << "  CLK=" << (iec.resolvedClkLow  ? "L" : "H")
            << "  DATA=" << (iec.resolvedDataLow ? "L" : "H")
            << "\n";

        oss << "  Final outputs:   "
            << "DATA_low=" << iec.finalDataLow
            << "  CLK_low=" << iec.finalClkLow
            << "\n";
    }

    oss << "  Last IEC writes:\n";

    for (int i = 0; i < 8; ++i)
    {
        const auto& w = iec.writeHistory[i];

        if (!w.valid)
            continue;

        oss << "    PC=$" << hex4(w.pc)
            << " return=$" << hex4(w.retTarget)
            << " $" << hex4(w.address)
            << " reg=" << int(w.reg)
            << " value=$" << hex2(w.value)
            << " -> PR=$" << hex2(w.prAfter)
            << " DDR=$" << hex2(w.ddrAfter)
            << "\n";
    }

    oss << "  Last IEC reads:\n";

    for (int i = 0; i < 8; ++i)
    {
        const auto& r = iec.readHistory[i];

        if (!r.valid)
            continue;

        oss << "    PC=$" << hex4(r.pc)
            << " return=$" << hex4(r.retTarget)
            << " $" << hex4(r.address)
            << " reg=" << int(r.reg)
            << " value=$" << hex2(r.value)
            << "\n";
    }

    oss << "  Repeated read:   value=$" << hex2(iec.lastReadValue)
        << " sameCount=" << iec.sameReadCount
        << "\n";

    std::cout << oss.str();
}

void MLMonitorBackend::dumpDriveCPU(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Devicvice is not a Floppy Drive\n";
        return;
    }

    const CPU* cpu = dev->asDrive()->getDriveCPU();
    if (!cpu)
    {
        std::cout << "No CPU!\n";
        return;
    }

    // Get current cpu state
    CPUState st = cpu->getState();

    auto hex2 = [](uint32_t v){
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setw(2)
          << std::setfill('0') << (v & 0xFF);
        return s.str();
    };
    auto hex4 = [](uint32_t v){
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setw(4)
          << std::setfill('0') << (v & 0xFFFF);
        return s.str();
    };
    auto flagsBits = [&](uint8_t p){
        std::string b;
        b += (p & 0x80) ? '1' : '0'; // N
        b += (p & 0x40) ? '1' : '0'; // V
        b += '-';                    // (unused)
        b += (p & 0x10) ? '1' : '0'; // B
        b += (p & 0x08) ? '1' : '0'; // D
        b += (p & 0x04) ? '1' : '0'; // I
        b += (p & 0x02) ? '1' : '0'; // Z
        b += (p & 0x01) ? '1' : '0'; // C
        return b;
    };

    // NEW: read opcode at PC
    uint8_t op = cpu->debugRead(st.PC);

    std::ostringstream out;
    out << "Drive " << id << " CPU:\n";
    out << "PC=$" << hex4(st.PC)
        << "  A=$" << hex2(st.A)
        << "  X=$" << hex2(st.X)
        << "  Y=$" << hex2(st.Y)
        << "  SP=$" << hex2(st.SP)
        << "  P=$"  << hex2(st.SR)
        << "  (NV-BDIZC=" << flagsBits(st.SR) << ")\n";
    out << "OP=$" << hex2(op) << "\n";

    std::cout << out.str();
}

void MLMonitorBackend::driveCPUStep(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    CPU* cpu = dev->asDrive()->getDriveCPU();
    if (!cpu)
    {
        std::cout << "No CPU!\n";
        return;
    }

    auto* mem = dev->asDrive()->getMemory();
    uint16_t pc = cpu->getPC();

    // Output disassembly at PC
    std::string disASM = Disassembler::disassembleAt(pc, *mem);
    std::cout << disASM << std::endl;

    // Execute tick to step
    cpu->tick();
    uint32_t cycles = cpu->getElapsedCycles();
    dev->asDrive()->getMemory()->tick(cycles);

    // Dump CPU registers
    auto st = cpu->getState();
    auto hex2 = [](uint32_t v){
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (v & 0xFF);
        return s.str();
    };
    auto hex4 = [](uint32_t v){
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << (v & 0xFFFF);
        return s.str();
    };
    auto flagsBits = [&](uint8_t p){
        std::string b;
        b += (p & 0x80) ? '1' : '0'; // N
        b += (p & 0x40) ? '1' : '0'; // V
        b += '-';                    // (unused)
        b += (p & 0x10) ? '1' : '0'; // B
        b += (p & 0x08) ? '1' : '0'; // D
        b += (p & 0x04) ? '1' : '0'; // I
        b += (p & 0x02) ? '1' : '0'; // Z
        b += (p & 0x01) ? '1' : '0'; // C
        return b;
    };

    std::ostringstream out;
    out << "PC=$" << hex4(st.PC)
         << "  A=$" << hex2(st.A)
         << "  X=$" << hex2(st.X)
         << "  Y=$" << hex2(st.Y)
         << "  SP=$" << hex2(st.SP)
         << "  P=$" << hex2(st.SR)
         << "  (NV-BDIZC=" << flagsBits(st.SR) << ")\n";

    std::cout << out.str();
}

void MLMonitorBackend::dumpDriveMemory(int id, uint16_t startAddress, uint16_t count)
{
    // Define the default display count if count is 0
    const uint16_t DEFAULT_COUNT = 16;
    uint16_t bytesToDump = (count == 0) ? DEFAULT_COUNT : count;

    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    auto* mem = dev->asDrive()->getMemory();

    if (!mem)
    {
        std::cout << "No memory device.\n";
        return;
    }

    // Use a stream for formatted output
    std::stringstream oss;
    oss << "Drive " << id << " Memory Dump ($"
        << hex4(startAddress) << " for " << bytesToDump << " bytes):\n";

    // Set up formatting for hex values
    oss << std::uppercase << std::hex << std::setfill('0');

    uint16_t currentAddress = startAddress;
    uint16_t bytesRead = 0;

    while (bytesRead < bytesToDump)
    {
        // Print the starting address of the current line
        oss << "$" << std::setw(4) << currentAddress << ": ";

        // Buffer for ASCII representation
        std::string ascii;

        // Print 8 bytes per line
        for (int i = 0; i < 8; ++i)
        {
            if (bytesRead >= bytesToDump)
            {
                // Fill remaining space if the last line is short
                oss << "   ";
            }
            else
            {
                uint8_t value = mem->read(currentAddress);
                oss << std::setw(2) << static_cast<int>(value) << " ";

                // Append to ASCII string
                if (value >= 0x20 && value <= 0x7E)
                {
                    ascii += static_cast<char>(value);
                }
                else
                {
                    ascii += '.'; // Non-printable character
                }

                currentAddress++;
                bytesRead++;
            }
        }

        // Print the ASCII representation
        oss << " " << ascii << "\n";
    }

    std::cout << oss.str();
}

void MLMonitorBackend::dumpDriveIECState(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    Drive* d = dev->asDrive();
    const Drive::IECSnapshot s = d->snapshotIEC();

    auto HL = [](bool low) { return low ? "L" : "H"; };
    auto yn = [](bool v) { return v ? "1" : "0"; };

    auto busStateToStr = [](Drive::DriveBusState st) {
        switch (st)
        {
            case Drive::DriveBusState::IDLE:             return "IDLE";
            case Drive::DriveBusState::AWAITING_COMMAND: return "AWAITING_COMMAND";
            case Drive::DriveBusState::LISTENING:        return "LISTENING";
            case Drive::DriveBusState::TALKING:          return "TALKING";
            default:                                     return "UNKNOWN";
        }
    };

    std::cout << "Drive #" << id << " IEC monitor state:\n\n";

    std::cout << "Drive IEC physical:\n";
    std::cout << "  Lines seen:        "
              << "ATN="  << HL(s.atnLow)  << "  "
              << "CLK="  << HL(s.clkLow)  << "  "
              << "DATA=" << HL(s.dataLow) << "  "
              << "SRQ="  << HL(s.srqLow)  << "\n";

    auto yesno = [](bool v) { return v ? "yes" : "no"; };

    std::cout << "  Drive pulls low:   "
          << "ATN="  << yesno(s.drvAssertAtn)  << "  "
          << "CLK="  << yesno(s.drvAssertClk)  << "  "
          << "DATA=" << yesno(s.drvAssertData) << "  "
          << "SRQ="  << yesno(s.drvAssertSrq)  << "\n\n";

    std::cout << "Legacy/software IEC decode:\n";
    std::cout << "  Mode: " << busStateToStr(s.busState)
              << "   listen=" << yn(s.listening)
              << " talk=" << yn(s.talking)
              << "   SA=";

    if (s.secondaryAddress < 0) std::cout << "(none)\n";
    else                        std::cout << "$" << hex2(static_cast<uint8_t>(s.secondaryAddress)) << "\n";

    std::cout << "  Shifter: shift=$" << hex2(s.shiftReg)
              << " bits=" << s.bitsProcessed << "\n";

    std::cout << "  Handshake: waitingForAck=" << yn(s.waitingForAck)
              << " ackEdgeCountdown=" << s.ackEdgeCountdown
              << " waitingForClkRelease=" << yn(s.waitingForClkRelease)
              << " prevClkLevel=" << yn(s.prevClkLevel)
              << "\n";

    std::cout << "            ackHold=" << yn(s.ackHold)
              << " byteAckHold=" << yn(s.byteAckHold)
              << " ackDelay=" << s.ackDelay
              << " swallowFalling=" << yn(s.swallowPostHandshakeFalling)
              << "\n";

    std::cout << "  Talk queue: " << s.talkQueueLen << "\n";
}

void MLMonitorBackend::dumpDriveVIA1(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    if (!dev->asDrive()->hasVIA1())
    {
        std::cout << "Drive does not have a VIA1 chip\n";
        return;
    }

    auto* via1 = dev->asDrive()->getVIA1();
    if (!via1)
    {
        std::cout << "No VIA1!\n";
        return;
    }

    auto registers = via1->getRegsView();

    std::stringstream oss;
    oss << "VIA1 Registers:\n";
    oss << "  ORB: $" << hex2(registers.orbIRB)
        << "  ORA: $" << hex2(registers.oraIRA) << "\n";
    oss << "  DDRB: $" << hex2(registers.ddrB)
        << "  DDRA: $" << hex2(registers.ddrA) << "\n";

    // Timers as bytes
    oss << "  T1 Low Byte:  $" << hex2(registers.t1CL)
        << "  T1 High Byte: $" << hex2(registers.t1CH) << "\n";
    oss << "  T1 Latch Low: $" << hex2(registers.t1LL)
        << "  T1 Latch High:$" << hex2(registers.t1LH) << "\n";
    oss << "  T2 Low Byte:  $" << hex2(registers.t2CL)
        << "  T2 High Byte: $" << hex2(registers.t2CH) << "\n";

    // Timers as 16-bit words
    oss << "  T1 Counter:   $" << hex4(registers.t1CH, registers.t1CL)
        << "  T1 Latch: $"    << hex4(registers.t1LH, registers.t1LL) << "\n";
    oss << "  T2 Counter:   $" << hex4(registers.t2CH, registers.t2CL) << "\n";

    oss << "  Serial Shift: $" << hex2(registers.sr) << "\n";
    oss << "  Aux Control:  $" << hex2(registers.acr) << "\n";
    oss << "  Periph Ctrl:  $" << hex2(registers.pcr) << "\n";
    oss << "  IFR:          $" << hex2(registers.ifr) << "\n";
    oss << "  IER:          $" << hex2(registers.ier) << "\n";
    oss << "  ORA(no HS):   $" << hex2(registers.oraNoHS) << "\n";

    // Is this VIA pulling IRQ?
    bool irqActive = via1->checkIRQActive();
    oss << "  IRQ Active:   " << (irqActive ? "YES" : "NO") << "\n";

    auto timers = via1->getTimerDebugView();
    appendVIATimerDebug(oss, timers);

    std::cout << oss.str();
}

void MLMonitorBackend::dumpDriveVIA2(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    if (!dev->asDrive()->hasVIA2())
    {
        std::cout << "Drive does not have a VIA2 chip\n";
        return;
    }

    auto* via2 = dev->asDrive()->getVIA2();
    if (!via2)
    {
        std::cout << "No VIA2!\n";
        return;
    }

    auto registers = via2->getRegsView();
    auto mech  = via2->getMechanicsInfo();

    std::stringstream oss;
    oss << "VIA2 Registers (Mechanics):\n";
    oss << "  ORB: $" << hex2(registers.orbIRB)
        << "  ORA: $" << hex2(registers.oraIRA) << "\n";
    oss << "  DDRB: $" << hex2(registers.ddrB)
        << "  DDRA: $" << hex2(registers.ddrA) << "\n";

    // Timers as bytes
    oss << "  T1 Low Byte:   $" << hex2(registers.t1CL)
        << "  T1 High Byte:  $" << hex2(registers.t1CH) << "\n";
    oss << "  T1 Latch Low:  $" << hex2(registers.t1LL)
        << "  T1 Latch High: $" << hex2(registers.t1LH) << "\n";
    oss << "  T2 Low Byte:   $" << hex2(registers.t2CL)
        << "  T2 High Byte:  $" << hex2(registers.t2CH) << "\n";

    // Timers as 16-bit words
    oss << "  T1 Counter:    $" << hex4(registers.t1CH, registers.t1CL)
        << "  T1 Latch: $"      << hex4(registers.t1LH, registers.t1LL) << "\n";
    oss << "  T2 Counter:    $" << hex4(registers.t2CH, registers.t2CL) << "\n";

    oss << "  Serial Shift:  $" << hex2(registers.sr)  << "\n";
    oss << "  Aux Control:   $" << hex2(registers.acr) << "\n";
    oss << "  Periph Ctrl:   $" << hex2(registers.pcr) << "\n";
    oss << "  IFR:           $" << hex2(registers.ifr) << "\n";
    oss << "  IER:           $" << hex2(registers.ier) << "\n";
    oss << "  ORA(no HS):    $" << hex2(registers.oraNoHS) << "\n";

    // Mechanics decode from Port B
    if (mech.valid)
    {
        oss << "\nMechanics (decoded):\n";
        oss << "  Motor:   " << (mech.motorOn ? "ON" : "OFF") << "\n";
        oss << "  LED:     " << (mech.ledOn ? "ON" : "OFF") << "\n";
        oss << "  Density: code=" << static_cast<unsigned>(mech.densityCode)
            << " (0-3)\n";
    }
    else
    {
        oss << "\nMechanics: (no mechanics info for this VIA)\n";
    }

    bool irqActive = via2->checkIRQActive();
    oss << "  IRQ Active: " << (irqActive ? "YES" : "NO") << "\n";

    auto timers = via2->getTimerDebugView();
    appendVIATimerDebug(oss, timers);

    std::cout << oss.str();
}

void MLMonitorBackend::dumpDriveFDC(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    if (!dev->asDrive()->hasFDC())
    {
        std::cout << "Drive does not have a FDC\n";
        return;
    }

    auto* fdc = dev->asDrive()->getFDC();

    if (!fdc)
    {
        std::cout << "No FDC\n";
        return;
    }

    auto registers = fdc->getRegsView();

    auto yn = [](bool b){ return b ? "Y" : "N"; };

    // FDC177x Status bit masks (from your header)
    auto st = [&](uint8_t mask){ return (registers.status & mask) != 0; };

    auto decodeCmd = [](uint8_t cmd) -> const char*
    {
        switch (cmd & 0xF0)
        {
            case 0x00: return "RESTORE (I)";
            case 0x10: return "SEEK (I)";
            case 0x20: return "STEP (I)";
            case 0x40: return "STEP IN (I)";
            case 0x60: return "STEP OUT (I)";
            case 0x80: return "READ SECTOR (II)";
            case 0xA0: return "WRITE SECTOR (II)";
            case 0xC0: return "READ ADDRESS (III)";
            case 0xD0: return "FORCE INT (IV)";
            case 0xE0: return "READ TRACK (III)";
            case 0xF0: return "WRITE TRACK (III)";
            default:   return "UNKNOWN";
        }
    };

    auto hex4 = [](uint16_t v){
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << v;
        return s.str();
    };

    std::stringstream oss;

    oss << "FDC Registers:\n";
    oss << "  Status:  $" << hex2(registers.status)
        << "  [BUSY=" << yn(st(0x01))
        << " DRQ="    << yn(st(0x02))
        << " LOST/T0="<< yn(st(0x04))
        << " CRC="    << yn(st(0x08))
        << " RNF="    << yn(st(0x10))
        << " SPIN/DD="<< yn(st(0x20))
        << " WP="     << yn(st(0x40))
        << " MOTOR="  << yn(st(0x80))
        << "]\n";

    oss << "  Command: $" << hex2(registers.command)
        << "  " << decodeCmd(registers.command) << "\n";

    oss << "  Track:   $" << hex2(registers.track)
        << " (" << std::dec << static_cast<unsigned>(registers.track) << ")\n";

    oss << "  Sector:  $" << hex2(registers.sector)
        << " (" << std::dec << static_cast<unsigned>(registers.sector) << ")\n";

    oss << "  Data:    $" << hex2(registers.data)
        << " (" << std::dec << static_cast<unsigned>(registers.data) << ")\n";

    oss << "  DRQ:     " << yn(registers.drq)
        << "  (check=" << yn(fdc->checkDRQActive()) << ")\n";

    oss << "  INTRQ:   " << yn(registers.intrq)
        << "  (check=" << yn(fdc->checkIRQActive()) << ")\n";

    oss << "  Sector Size: $" << hex4(registers.currentSectorSize)
        << " (" << std::dec << registers.currentSectorSize << " bytes)\n";

    oss << "  Data Index:  $" << hex2(registers.dataIndex)
        << " (" << std::dec << static_cast<unsigned>(registers.dataIndex) << ")\n";

    oss << "  In-Progress: read=" << yn(registers.readSectorInProgress)
        << " write=" << yn(registers.writeSectorInProgress)
        << " cyclesUntilEvent=" << registers.cyclesUntilEvent << "\n";

    std::cout << oss.str();
}

std::string MLMonitorBackend::jamModeToString() const
{
    if (cpu)
    {
        CPU::JamMode mode = cpu->getJamMode();
        switch(mode)
        {
            case CPU::JamMode::FreezePC: return "FreezePC";
            case CPU::JamMode::Halt: return "Halt";
            case CPU::JamMode::NopCompat: return "NopCompat";
        }
    }

    // Default
        return "Unknown";
}

std::string MLMonitorBackend::decodeDriveStatus(Drive::DriveStatus status)
{
    switch(status)
    {
        case Drive::DriveStatus::IDLE:      return "IDLE";
        case Drive::DriveStatus::READY:     return "READY";
        case Drive::DriveStatus::READING:   return "READING";
        case Drive::DriveStatus::WRITING:   return "WRITING";
        case Drive::DriveStatus::SEEKING:   return "SEEKING";
        default: return "IDLE";
     }

     return "IDLE";
}
