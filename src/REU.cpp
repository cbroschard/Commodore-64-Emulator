// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "REU.h"

REU::REU() :
    model(REUModel::None)
{

}

REU::~REU() = default;

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
    if (!isEnabled())
        return;

    regs.status &= static_cast<uint8_t>(~(SR_END_OF_BLOCK | SR_VERIFY_ERROR | SR_IRQ_PENDING));
    regs.status |= baseStatusForModel();

    // TODO: Add transfer

    regs.status |= SR_END_OF_BLOCK;
    updateIRQStatus();
}
