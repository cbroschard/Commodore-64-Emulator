// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/ActionReplay2Mapper.h"

ActionReplay2Mapper::ActionReplay2Mapper() :
    selectedBank(0),
    freezeActive(false),
    preFreezeSelectedBank(0)
{

}

ActionReplay2Mapper::~ActionReplay2Mapper() = default;

void ActionReplay2Mapper::AR2Control::save(StateWriter& wrtr) const
{
    wrtr.writeU8(raw);
}

bool ActionReplay2Mapper::AR2Control::load(StateReader& rdr)
{
    if (!rdr.readU8(raw))   return false;

    decode();
    return true;
}

void ActionReplay2Mapper::AR2Control::decode()
{
    romBank =       raw & 0x01;
    cartEnabled =   ((raw & 0x04) == 0);
}

void ActionReplay2Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("A2RP");
    wrtr.writeU32(1);

    ctrl.save(wrtr);
    preFreezeCtrl.save(wrtr);
    wrtr.writeBool(freezeActive);
    wrtr.writeU8(selectedBank);
    wrtr.writeU8(preFreezeSelectedBank);

    wrtr.endChunk();
}

bool ActionReplay2Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "A2RP", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                  { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                           { rdr.exitChunkPayload(chunk); return false; }

    if (!ctrl.load(rdr))                    { rdr.exitChunkPayload(chunk); return false; }
    if (!preFreezeCtrl.load(rdr))           { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(freezeActive))        { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(selectedBank))          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(preFreezeSelectedBank)) { rdr.exitChunkPayload(chunk); return false; }

    if (!applyMappingAfterLoad())           { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

uint8_t ActionReplay2Mapper::read(uint16_t address)
{
    if (!mem || !cart || !ctrl.cartEnabled)
        return 0xFF;

    if ((address & 0xFF00) == 0xDF00)
    {
        return mem->readCartridge(
            static_cast<uint16_t>(0x1F00 | (address & 0x00FF)),
            cartLocation::LO
        );
    }

    return 0xFF;
}

void ActionReplay2Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart || !mem)
        return;

    if ((address & 0xFF00) == 0xDF00)
    {
        // Disable the cart
        ctrl.raw |= 0x04;
        ctrl.decode();
        freezeActive = false;
        applyMappingFromControl();
        return;
    }

    if ((address & 0xFF00) == 0xDE00)
    {
        // Re-enable and select bank from bit 0
        ctrl.raw = static_cast<uint8_t>(value & 0x01);
        ctrl.decode();
        freezeActive = false;
        applyMappingFromControl();
        return;
    }
}

bool ActionReplay2Mapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart)
        return false;

    selectedBank = bank;

    // Start from a clean cartridge mapping.
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    const auto& sections = cart->getChipSections();

    for (const auto& s : sections)
    {
        if (s.bankNumber != bank)
            continue;

        if (s.data.size() >= 0x2000)
        {
            // Load first 8K into ROML ($8000-$9FFF)
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

            return true;
        }
    }

    return false;
}

const char* ActionReplay2Mapper::getButtonName(uint32_t buttonIndex) const
{
    switch(buttonIndex)
    {
        case 0:  return "Freeze";
        default: return "";
    }
}

void ActionReplay2Mapper::pressButton(uint32_t buttonIndex)
{
    switch(buttonIndex)
    {
        case 0:
            pressFreeze();
            break;
        default:
            break;
    }
}

void ActionReplay2Mapper::pressFreeze()
{
    if (!cart || !mem)
        return;

    if (!freezeActive)
    {
        preFreezeCtrl = ctrl;
        preFreezeSelectedBank = selectedBank;
    }
    freezeActive = true;

    ctrl.raw = 0x03;
    ctrl.decode();

    applyMappingFromControl();

    cart->requestCartridgeNMI();
}

bool ActionReplay2Mapper::applyMappingAfterLoad()
{
    if (!mem || !cart)
        return false;

    applyMappingFromControl();
    return true;
}

void ActionReplay2Mapper::applyMappingFromControl()
{
    if (!cart || !mem)
        return;

    if (!ctrl.cartEnabled)
    {
        cart->clearCartridge(cartLocation::LO);
        cart->clearCartridge(cartLocation::HI);
        cart->clearCartridge(cartLocation::HI_E000);

        // no cart visible
        cart->setGameLine(true);
        cart->setExROMLine(true);
        return;
    }

    loadIntoMemory(ctrl.romBank);

    // AR2 normal mode = 8K game
    cart->setGameLine(true);
    cart->setExROMLine(false);
}
