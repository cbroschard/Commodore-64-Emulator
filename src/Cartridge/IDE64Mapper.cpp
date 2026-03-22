// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/IDE64Mapper.h"

IDE64Mapper::IDE64Mapper()
{

}

IDE64Mapper::~IDE64Mapper() = default;

void IDE64Mapper::ControlState::save(StateWriter& wrtr) const
{
    wrtr.writeU8(de32Raw);

    for (int i = 0; i < 4; i++)
        wrtr.writeU8(romBankRegs[i]);

    for (int i = 0; i < 4; i++)
        wrtr.writeU8(memCfg[i]);

    wrtr.writeBool(killed);
}

bool IDE64Mapper::ControlState::load(StateReader& rdr)
{
    if (!rdr.readU8(de32Raw)) return false;

    for (int i = 0; i < 4; i++)
        if (!rdr.readU8(romBankRegs[i])) return false;

    for (int i = 0; i < 4; i++)
        if (!rdr.readU8(memCfg[i])) return false;

    if (!rdr.readBool(killed)) return false;

    decodeDE32();
    return true;
}

void IDE64Mapper::ControlState::decodeDE32()
{
    exrom      = (de32Raw & 0x01) != 0;
    game       = (de32Raw & 0x02) != 0;
    romAddr14  = (de32Raw & 0x04) != 0;
    romAddr15  = (de32Raw & 0x08) != 0;
    versionBit = (de32Raw & 0x10) != 0;
}

void IDE64Mapper::ControlState::composeDE32()
{
    de32Raw = 0;
    if (exrom)      de32Raw |= 0x01;
    if (game)       de32Raw |= 0x02;
    if (romAddr14)  de32Raw |= 0x04;
    if (romAddr15)  de32Raw |= 0x08;
    if (versionBit) de32Raw |= 0x10;
}

void IDE64Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("ID64");
    wrtr.writeU32(1); // version

    controller.saveState(wrtr);
    rtc.saveState(wrtr);

    ctrl.save(wrtr);

    wrtr.writeVectorU8(ram);
    wrtr.writeVectorU8(rom);
    wrtr.writeVectorU8(flashCfg);

    wrtr.endChunk();
}

bool IDE64Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "ID64", 4) == 0)
    {
        uint32_t ver = 0;
        if (!rdr.readU32(ver))              { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                       { rdr.exitChunkPayload(chunk); return false; }

        if (!controller.loadState(rdr))     { rdr.exitChunkPayload(chunk); return false; }
        if (!rtc.loadState(rdr))            { rdr.exitChunkPayload(chunk); return false; }

        if (!ctrl.load(rdr))                { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readVectorU8(ram))         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU8(rom))         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU8(flashCfg))    { rdr.exitChunkPayload(chunk); return false; }

        if (!applyMappingAfterLoad())       { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // not our chunk
    return false;
}

void IDE64Mapper::reset()
{
    controller.reset();
    rtc.reset();

    ctrl.killed             = false;
    ctrl.de32Raw            = 0x13; // version + GAME + EXROM
    ctrl.decodeDE32();
    ctrl.romBankRegs[0]     = ctrl.de32Raw;

    for (int i = 1; i < 4; i++)
        ctrl.romBankRegs[i] = 0x00;

    for (int i = 0; i < 4; i++)
        ctrl.memCfg[i]      = 0x00;

    (void)applyMappingAfterLoad();
}

uint8_t IDE64Mapper::read(uint16_t address)
{
    if (address >= IDE64_Controller_Start && address <= IDE64_Controller_End)
        return controller.readRegister(address);

    if (address == 0xDE32)
        return ctrl.de32Raw;

    if (address >= 0xDE33 && address <= 0xDE35)
        return ctrl.romBankRegs[address - 0xDE32];

    if (address == RTC_Address)
        return rtc.readByte();

    if (address >= IDE64_Ctrl_Cfg_Start && address <= IDE64_Ctrl_Cfg_End)
    {
        switch (address)
        {
            case 0xDEFB:
                return 0xFF;
            case 0xDEFC:
            case 0xDEFD:
            case 0xDEFE:
            case 0xDEFF:
                return ctrl.memCfg[address - 0xDEFC];
        }
    }
    return 0xFF;
}

void IDE64Mapper::write(uint16_t address, uint8_t value)
{
    if (address >= IDE64_Controller_Start && address <= IDE64_Controller_End)
        controller.writeRegister(address, value);

    else if (address == 0xDE32)
    {
        ctrl.de32Raw = value;
        ctrl.decodeDE32();
        ctrl.romBankRegs[0] = value;
        (void)applyMappingAfterLoad();
    }
    else if (address >= 0xDE33 && address <= 0xDE35)
    {
        ctrl.romBankRegs[address - 0xDE32] = value;
        (void)applyMappingAfterLoad();
    }

    else if (address == RTC_Address)
        rtc.writeByte(value);

    else if (address >= IDE64_Ctrl_Cfg_Start && address <= IDE64_Ctrl_Cfg_End)
    {
        switch (address)
        {
            case 0xDEFB:
                rtc.reset();
                ctrl.killed = true;
                (void)applyMappingAfterLoad();
                break;
            case 0xDEFC:
            case 0xDEFD:
            case 0xDEFE:
            case 0xDEFF:
                ctrl.memCfg[address - 0xDEFC] = value;
                (void)applyMappingAfterLoad();
                break;
        }
    }
}

bool IDE64Mapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart)
        return false;

    (void)bank;

    // Start from a clean cartridge mapping.
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    if (ctrl.killed)
        return true;

    const bool game  = ctrl.game;
    const bool exrom = ctrl.exrom;

    size_t romBase = 0;
    if (ctrl.romAddr14)
        romBase |= (1u << 14);
    if (ctrl.romAddr15)
        romBase |= (1u << 15);

    if (rom.empty())
        return false;

    // Standard mode: no visible cart ROM
    if (game && exrom)
        return true;

    // 16K-style: LO + HI
    if (!game && !exrom)
    {
        for (size_t i = 0; i < 0x2000; ++i)
        {
            uint8_t loVal = 0xFF;
            uint8_t hiVal = 0xFF;

            if (romBase + i < rom.size())
                loVal = rom[romBase + i];

            if (romBase + 0x2000 + i < rom.size())
                hiVal = rom[romBase + 0x2000 + i];

            mem->writeCartridge(static_cast<uint16_t>(i), loVal, cartLocation::LO);
            mem->writeCartridge(static_cast<uint16_t>(i), hiVal, cartLocation::HI);
        }

        return true;
    }

    // Ultimax-style: LO + HI_E000
    if (!game && exrom)
    {
        for (size_t i = 0; i < 0x2000; ++i)
        {
            uint8_t loVal   = 0xFF;
            uint8_t e000Val = 0xFF;

            if (romBase + i < rom.size())
                loVal = rom[romBase + i];

            if (romBase + 0x2000 + i < rom.size())
                e000Val = rom[romBase + 0x2000 + i];

            mem->writeCartridge(static_cast<uint16_t>(i), loVal, cartLocation::LO);
            mem->writeCartridge(static_cast<uint16_t>(i), e000Val, cartLocation::HI_E000);
        }

        return true;
    }

    return true;
}

bool IDE64Mapper::savePersistence(const std::string& path) const
{
    return true;
}

bool IDE64Mapper::loadPersistence(const std::string& path)
{
    return true;
}

bool IDE64Mapper::applyMappingAfterLoad()
{
    return loadIntoMemory(0);
}
