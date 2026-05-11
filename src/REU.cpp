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
    return 0xFF;
}

void REU::writeIO(uint16_t address, uint8_t value)
{

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
