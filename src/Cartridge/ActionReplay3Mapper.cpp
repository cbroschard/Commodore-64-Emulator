// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/ActionReplay3Mapper.h"

ActionReplay3Mapper::ActionReplay3Mapper() :
    selectedBank(0),
    freezeActive(false),
    preFreezeSelectedBank(0)
{

}

ActionReplay3Mapper::~ActionReplay3Mapper() = default;

void ActionReplay3Mapper::AR3Control::save(StateWriter& wrtr) const
{
    wrtr.writeU8(raw);
}

bool ActionReplay3Mapper::AR3Control::load(StateReader& rdr)
{
    if (!rdr.readU8(raw))   return false;

    decode();
    return true;
}

void ActionReplay3Mapper::AR3Control::decode()
{
    romBank      = raw & 0x01;
    cartDisabled = (raw & 0x04) != 0;
    exromHigh = ((raw & 0x08) != 0);
}

void ActionReplay3Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("A3RP");
    wrtr.writeU32(1); // version

    ctrl.save(wrtr);
    preFreezeCtrl.save(wrtr);

    wrtr.writeBool(freezeActive);
    wrtr.writeU8(selectedBank);
    wrtr.writeU8(preFreezeSelectedBank);

    wrtr.endChunk();
}

bool ActionReplay3Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "A3RP", 4) == 0)
    {
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
    // Not our chunk
    return false;
}

uint8_t ActionReplay3Mapper::read(uint16_t address)
{
    return 0xFF;
}

void ActionReplay3Mapper::write(uint16_t address, uint8_t value)
{
    if ((address & 0xFF00) != 0xDE00)
        return;

    if (ctrl.cartDisabled)
        return;

    ctrl.raw = value;
    ctrl.decode();
    applyMappingFromControl();
}

bool ActionReplay3Mapper::loadIntoMemory(uint8_t bank)
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

const char* ActionReplay3Mapper::getButtonName(uint32_t buttonIndex) const
{
    switch(buttonIndex)
    {
        case 0:  return "Freeze";
        case 1:  return "Reset";
        default: return "";
    }
}

void ActionReplay3Mapper::pressButton(uint32_t buttonIndex)
{
    switch (buttonIndex)
    {
        case 0:
            pressFreeze();
            break;

        case 1:
            pressReset();
            break;

        default:
            break;
    }
}

void ActionReplay3Mapper::pressFreeze()
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

void ActionReplay3Mapper::pressReset()
{
    freezeActive = false;
    preFreezeSelectedBank = 0;

    ctrl.raw = 0x00;
    ctrl.decode();

    preFreezeCtrl.raw = 0;
    preFreezeCtrl.decode();

    applyMappingFromControl();

    if (cart)
        cart->requestWarmReset();
}

bool ActionReplay3Mapper::applyMappingAfterLoad()
{
    if (!mem || !cart)
        return false;

    applyMappingFromControl();
    return true;
}


void ActionReplay3Mapper::applyMappingFromControl()
{
    if (!cart || !mem)
        return;

    if (ctrl.cartDisabled)
    {
        cart->clearCartridge(cartLocation::LO);
        cart->clearCartridge(cartLocation::HI);
        cart->clearCartridge(cartLocation::HI_E000);

        // disabled = no cartridge visible
        cart->setGameLine(true);
        cart->setExROMLine(true);
        return;
    }

    loadIntoMemory(ctrl.romBank);

    // AR3 is an 8K cart at $8000-$9FFF
    cart->setGameLine(true);                 // GAME inactive/high
    cart->setExROMLine(ctrl.exromHigh);      // live line level
}
