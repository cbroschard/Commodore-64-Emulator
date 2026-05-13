// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Memory.h"
#include "REU.h"

REU::REU() :
    irq(nullptr),
    mem(nullptr),
    model(REUModel::None)
{

}

REU::~REU() = default;

void REU::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("REU0");
    wrtr.writeU32(1); //version

    // Dump registers
    wrtr.writeU8(regs.status);
    wrtr.writeU8(regs.command);

    wrtr.writeU16(regs.c64Address);

    wrtr.writeU16(regs.reuAddressLo);
    wrtr.writeU8(regs.reuBank);

    wrtr.writeU16(regs.transferLen);

    wrtr.writeU8(regs.irqMask);
    wrtr.writeU8(regs.addressControl);

    // Dump RAM
    wrtr.writeVectorU8(ram);

    wrtr.endChunk();
}

bool REU::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "REU0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                  { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                           { rdr.exitChunkPayload(chunk); return false; }

        // Load registers
        if (!rdr.readU8(regs.status))           { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(regs.command))          { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU16(regs.c64Address))      { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU16(regs.reuAddressLo))    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(regs.reuBank))          { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU16(regs.transferLen))     { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(regs.irqMask))          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(regs.addressControl))   { rdr.exitChunkPayload(chunk); return false; }

        // Load RAM
        if (!rdr.readVectorU8(ram))             { rdr.exitChunkPayload(chunk); return false; }

        // Post load validation
        const std::size_t expectedBytes = bytesForREUModel(model);

        if (expectedBytes == 0)
        {
            ram.clear();
            model = REUModel::None;
        }
        else if (ram.size() != expectedBytes)
        {
            ram.resize(expectedBytes, 0x00);
        }

        regs.status = static_cast<uint8_t>(
            (regs.status & ~SR_SIZE_FLAG) | baseStatusForModel()
        );

        updateIRQStatus();

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Not our chunk
    return false;
}

void REU::reset()
{
    regs        = REURegisters{};
    regs.status = baseStatusForModel();
}

uint8_t REU::readIO(uint16_t address)
{
    const uint8_t reg = static_cast<uint8_t>(address & 0x0F);

    switch (reg)
    {
        case 0x00: // Status Register $DF00
            return regs.status;

        case 0x01: // Command Register $DF01
            return regs.command;

        case 0x02: // C64 Address Low $DF02
            return static_cast<uint8_t>(regs.c64Address & 0x00FF);

        case 0x03: // C64 Address High $DF03
            return static_cast<uint8_t>((regs.c64Address >> 8) & 0x00FF);

        case 0x04: // REU Address Low $DF04
            return static_cast<uint8_t>(regs.reuAddressLo & 0x00FF);

        case 0x05: // REU Address High $DF05
            return static_cast<uint8_t>((regs.reuAddressLo >> 8) & 0x00FF);

        case 0x06: // REU Bank $DF06
            return regs.reuBank;

        case 0x07: // Transfer Length Low $DF07
            return static_cast<uint8_t>(regs.transferLen & 0x00FF);

        case 0x08: // Transfer Length High $DF08
            return static_cast<uint8_t>((regs.transferLen >> 8) & 0x00FF);

        case 0x09: // IRQ Mask $DF09
            return regs.irqMask | IRQ_UNUSED_MASK;

        case 0x0A: // Address Control $DF0A
            return regs.addressControl | ACR_UNUSED_MASK;

        default:
            return 0xFF;
    }
}

void REU::writeIO(uint16_t address, uint8_t value)
{
    const uint8_t reg = static_cast<uint8_t>(address & 0x0F);

    switch (reg)
    {
        case 0x00: // Status Register $DF00
            // Read-mostly for now. Ignore writes.
            break;

        case 0x01: // Command Register $DF01
            regs.command = value;

            if (value & CR_EXECUTE)
                startTransfer();

            break;

        case 0x02: // C64 Address Low $DF02
            regs.c64Address = static_cast<uint16_t>(
                (regs.c64Address & 0xFF00) | value
            );
            break;

        case 0x03: // C64 Address High $DF03
            regs.c64Address = static_cast<uint16_t>(
                (regs.c64Address & 0x00FF) | (static_cast<uint16_t>(value) << 8)
            );
            break;

        case 0x04: // REU Address Low $DF04
            regs.reuAddressLo = static_cast<uint16_t>(
                (regs.reuAddressLo & 0xFF00) | value
            );
            break;

        case 0x05: // REU Address High $DF05
            regs.reuAddressLo = static_cast<uint16_t>(
                (regs.reuAddressLo & 0x00FF) | (static_cast<uint16_t>(value) << 8)
            );
            break;

        case 0x06: // REU Bank $DF06
            regs.reuBank = value;
            break;

        case 0x07: // Transfer Length Low $DF07
            regs.transferLen = static_cast<uint16_t>(
                (regs.transferLen & 0xFF00) | value
            );
            break;

        case 0x08: // Transfer Length High $DF08
            regs.transferLen = static_cast<uint16_t>(
                (regs.transferLen & 0x00FF) | (static_cast<uint16_t>(value) << 8)
            );
            break;

        case 0x09: // IRQ Mask $DF09
            regs.irqMask = value;
            updateIRQStatus();
            break;

        case 0x0A: // Address Control $DF0A
            regs.addressControl = value;
            break;

        default:
            break;
    }
}

void REU::setModel(REUModel reuModel)
{
    model = reuModel;

    const std::size_t bytes = bytesForREUModel(model);

    ram.clear();
    ram.resize(bytes, 0x00);

    reset();
}

uint8_t REU::baseStatusForModel() const
{
    switch (model)
    {
        case REUModel::Commodore1750:
        case REUModel::Custom1M:
        case REUModel::Custom2M:
        case REUModel::Custom4M:
        case REUModel::Custom8M:
        case REUModel::Custom16M:
            return SR_SIZE_FLAG;

        case REUModel::Commodore1700:
        case REUModel::Commodore1764:
        case REUModel::None:
        default:
            return 0x00;
    }
}

uint32_t REU::reuAddress() const
{
    return (static_cast<uint32_t>(regs.reuBank) << 16) |
           static_cast<uint32_t>(regs.reuAddressLo);
}

uint32_t REU::maskedREUAddress() const
{
    if (ram.empty())
        return 0;

    return reuAddress() % static_cast<uint32_t>(ram.size());
}

uint32_t REU::transferLengthBytes() const
{
    return regs.transferLen == 0 ? 0x10000u
                                 : static_cast<uint32_t>(regs.transferLen);
}

bool REU::shouldIncrementC64Address() const
{
    return (regs.addressControl & ACR_FIX_C64) == 0;
}

bool REU::shouldIncrementREUAddress() const
{
    return (regs.addressControl & ACR_FIX_REU) == 0;
}

void REU::incrementREUAddress()
{
    uint32_t addr = reuAddress();
    addr = (addr + 1) & 0xFFFFFFu;

    regs.reuAddressLo = static_cast<uint16_t>(addr & 0xFFFFu);
    regs.reuBank      = static_cast<uint8_t>((addr >> 16) & 0xFFu);
}

void REU::updateIRQStatus()
{
    regs.status &= ~SR_IRQ_PENDING;

    const bool irqEnabled =
        (regs.irqMask & IRQ_ENABLE) != 0;

    const bool endOfBlockIrq =
        (regs.irqMask & IRQ_END_OF_BLOCK) &&
        (regs.status & SR_END_OF_BLOCK);

    const bool verifyErrorIrq =
        (regs.irqMask & IRQ_VERIFY_ERROR) &&
        (regs.status & SR_VERIFY_ERROR);

    if (irqEnabled && (endOfBlockIrq || verifyErrorIrq))
        regs.status |= SR_IRQ_PENDING;
}

void REU::startTransfer()
{
    if (!isEnabled() || !mem)
        return;

    // Clear dynamic status bits, preserve model/version bits.
    regs.status &= static_cast<uint8_t>(~(SR_DYNAMIC_MASK ));

    regs.status |= baseStatusForModel();

    const uint8_t transferType = regs.command & CR_TRANSFER_MASK;
    const uint32_t length = transferLengthBytes();

    for (uint32_t i = 0; i < length; ++i)
    {
        const uint16_t c64Addr = regs.c64Address;
        const uint32_t reuAddr = maskedREUAddress();

        switch (transferType)
        {
            case 0x00: // C64 -> REU
            {
                const uint8_t value = mem->readForDMA(c64Addr);
                ram[reuAddr] = value;
                break;
            }

            case 0x01: // REU -> C64
            {
                const uint8_t value = ram[reuAddr];
                mem->writeForDMA(c64Addr, value);
                break;
            }

            case 0x02: // Swap C64 <-> REU
            {
                const uint8_t c64Value = mem->readForDMA(c64Addr);
                const uint8_t reuValue = ram[reuAddr];

                mem->writeForDMA(c64Addr, reuValue);
                ram[reuAddr] = c64Value;
                break;
            }

            case 0x03: // Verify C64 against REU
            {
                const uint8_t c64Value = mem->readForDMA(c64Addr);
                const uint8_t reuValue = ram[reuAddr];

                if (c64Value != reuValue)
                {
                    regs.status |= SR_VERIFY_ERROR;

                    regs.status |= SR_END_OF_BLOCK;
                    updateIRQStatus();
                    return;
                }

                break;
            }
        }

        if (shouldIncrementC64Address())
            regs.c64Address = static_cast<uint16_t>(regs.c64Address + 1);

        if (shouldIncrementREUAddress())
            incrementREUAddress();
    }

    regs.status |= SR_END_OF_BLOCK;
    updateIRQStatus();
}

std::string REU::dumpStatus() const
{
    auto yn = [](bool v)
    {
        return v ? "Y" : "N";
    };

    std::stringstream out;

    out << "REU: " << (isEnabled() ? "Enabled" : "Disabled") << "\n";

    if (!isEnabled())
    {
        out << "Model: " << displayNameForREUModel(model) << "\n";
        out << "Size:  " << displaySizeForREUModel(model) << "\n";
        return out.str();
    }

    out << "Model: " << displayNameForREUModel(model) << "\n";
    out << "Size:  " << displaySizeForREUModel(model) << "\n";

    if (!ram.empty())
    {
        const uint32_t maxAddr = static_cast<uint32_t>(ram.size() - 1);

        out << "RAM:   $000000-$"
            << std::hex << std::uppercase
            << std::setw(6) << std::setfill('0')
            << maxAddr
            << std::dec << "\n";
    }

    out << "\nStatus:\n";

    out << "  $DF00 Status:  $"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(regs.status)
        << std::dec
        << "  size="      << yn((regs.status & SR_SIZE_FLAG) != 0)
        << " verifyErr=" << yn((regs.status & SR_VERIFY_ERROR) != 0)
        << " endBlock="  << yn((regs.status & SR_END_OF_BLOCK) != 0)
        << " irq="       << yn((regs.status & SR_IRQ_PENDING) != 0)
        << "\n";

    out << "  $DF01 Command: $"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(regs.command)
        << std::dec
        << "  execute=" << yn((regs.command & CR_EXECUTE) != 0)
        << " type=" << transferTypeName(regs.command)
        << " autoload=" << yn((regs.command & CR_AUTOLOAD) != 0)
        << "\n";

    out << "\nTransfer:\n";

    out << "  C64 address:   $"
        << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
        << static_cast<int>(regs.c64Address)
        << std::dec << "\n";

    out << "  REU address:   $"
        << std::hex << std::uppercase << std::setw(6) << std::setfill('0')
        << reuAddress()
        << std::dec << "\n";

    out << "  Length:        $"
        << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
        << static_cast<int>(regs.transferLen)
        << std::dec
        << " / " << transferLengthBytes() << " bytes\n";

    out << "\nControl:\n";

    out << "  IRQ mask:      $"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(regs.irqMask)
        << std::dec
        << "  irqEnable=" << yn((regs.irqMask & IRQ_ENABLE) != 0)
        << " eobIrq="    << yn((regs.irqMask & IRQ_END_OF_BLOCK) != 0)
        << " verifyIrq=" << yn((regs.irqMask & IRQ_VERIFY_ERROR) != 0)
        << "\n";

    out << "  Addr control:  $"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(regs.addressControl)
        << std::dec
        << "  incC64=" << yn(shouldIncrementC64Address())
        << " incREU=" << yn(shouldIncrementREUAddress())
        << "\n";

    return out.str();
}

std::string REU::dumpRegs() const
{
    std::stringstream out;

    auto hex2 = [](uint8_t value)
    {
        std::stringstream ss;
        ss << std::hex << std::uppercase
           << std::setw(2) << std::setfill('0')
           << static_cast<int>(value);
        return ss.str();
    };

    const uint8_t df00 = regs.status;
    const uint8_t df01 = regs.command;

    const uint8_t df02 = static_cast<uint8_t>(regs.c64Address & 0x00FF);
    const uint8_t df03 = static_cast<uint8_t>((regs.c64Address >> 8) & 0x00FF);

    const uint8_t df04 = static_cast<uint8_t>(regs.reuAddressLo & 0x00FF);
    const uint8_t df05 = static_cast<uint8_t>((regs.reuAddressLo >> 8) & 0x00FF);
    const uint8_t df06 = regs.reuBank;

    const uint8_t df07 = static_cast<uint8_t>(regs.transferLen & 0x00FF);
    const uint8_t df08 = static_cast<uint8_t>((regs.transferLen >> 8) & 0x00FF);

    const uint8_t df09 = regs.irqMask;
    const uint8_t df0A = regs.addressControl;

    out << "REU Registers:\n"
        << "  DF00=$" << hex2(df00)
        << " DF01=$" << hex2(df01)
        << " DF02=$" << hex2(df02)
        << " DF03=$" << hex2(df03)
        << "\n"
        << "  DF04=$" << hex2(df04)
        << " DF05=$" << hex2(df05)
        << " DF06=$" << hex2(df06)
        << " DF07=$" << hex2(df07)
        << "\n"
        << "  DF08=$" << hex2(df08)
        << " DF09=$" << hex2(df09)
        << " DF0A=$" << hex2(df0A)
        << "\n";

    return out.str();
}

std::string REU::dumpRAM(uint32_t address, uint32_t count) const
{
    std::stringstream out;

    if (!isEnabled() || ram.empty())
    {
        out << "REU RAM unavailable - REU disabled\n";
        return out.str();
    }

    if (count == 0)
        count = 16;

    // Keep accidental huge dumps from flooding the monitor.
    if (count > 256)
        count = 256;

    const uint32_t ramSize = static_cast<uint32_t>(ram.size());
    const uint32_t start   = address % ramSize;

    out << "REU RAM dump from $"
        << std::hex << std::uppercase << std::setw(6) << std::setfill('0')
        << start
        << std::dec
        << ", " << count << " bytes:\n";

    for (uint32_t offset = 0; offset < count; offset += 16)
    {
        const uint32_t lineCount = std::min<uint32_t>(16, count - offset);
        const uint32_t lineAddr  = (start + offset) % ramSize;

        out << std::hex << std::uppercase << std::setw(6) << std::setfill('0')
            << lineAddr
            << ": ";

        // Hex bytes
        for (uint32_t i = 0; i < 16; ++i)
        {
            if (i < lineCount)
            {
                const uint32_t ramAddr = (start + offset + i) % ramSize;
                out << std::setw(2) << std::setfill('0')
                    << static_cast<int>(ram[ramAddr])
                    << " ";
            }
            else
            {
                out << "   ";
            }
        }

        out << " ";

        // ASCII preview
        for (uint32_t i = 0; i < lineCount; ++i)
        {
            const uint32_t ramAddr = (start + offset + i) % ramSize;
            const uint8_t ch = ram[ramAddr];

            if (ch >= 32 && ch <= 126)
                out << static_cast<char>(ch);
            else
                out << ".";
        }

        out << "\n";
    }

    return out.str();
}

const char* REU::transferTypeName(uint8_t command)
{
    switch (command & CR_TRANSFER_MASK)
    {
        case 0x00:
            return "C64->REU";

        case 0x01:
            return "REU->C64";

        case 0x02:
            return "SWAP";

        case 0x03:
            return "VERIFY";

        default:
            return "?";
    }
}
