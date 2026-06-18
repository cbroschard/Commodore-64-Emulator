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
    controller.attachDevice(0, &device0);
    controller.attachDevice(1, &device1);
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
    ctrl.de32Raw            = 0x12; // version + GAME, EXROM low
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
    if (!ctrl.game && ctrl.exrom &&
        ((address >= 0x1000 && address <= 0x7FFF) ||
         (address >= 0xC000 && address <= 0xCFFF)))
    {
        if (!cart)
            return 0xFF;

        return cart->readRAM(address & 0x7FFF);
    }

    // Upper half of selected ROM during open/Ultimax configuration
    if (!ctrl.game && ctrl.exrom &&
        address >= 0xA000 && address <= 0xBFFF)
    {
        if (rom.empty())
            return 0xFF;

        const size_t bankNumber =
            (ctrl.romAddr14 ? 1u : 0u) |
            (ctrl.romAddr15 ? 2u : 0u);

        const size_t offset =
            bankNumber * 0x4000 +
            static_cast<size_t>(address - 0x8000);

        if (offset >= rom.size())
            return 0xFF;

        return rom[offset];
    }

    // IDE/ATA controller registers: $DE20-$DE2F
    if (address >= IDE64_Controller_Start &&
        address <= IDE64_Controller_End)
    {
        return controller.readRegister(address);
    }

    if (address == 0xDE32)
    {
        ctrl.composeDE32();
        ctrl.romBankRegs[0] = ctrl.de32Raw;
        return ctrl.de32Raw;
    }

    // $DE33-$DE35 are write-only bank-selection addresses.
    if (address >= 0xDE33 && address <= 0xDE35)
        return 0xFF;

    // DS1302 RTC
    if (address == RTC_Address)
    {
        if (ctrl.killed)
            return 0xFF;

        return rtc.readByte();
    }

    if (address >= 0xDE60 && address <= 0xDEFF)
    {
        if (ctrl.killed || rom.empty())
            return 0xFF;

        const size_t bankNumber =
            (ctrl.romAddr14 ? 1u : 0u) |
            (ctrl.romAddr15 ? 2u : 0u);

        const size_t offset =
            bankNumber * 0x4000 +
            0x1E00 +
            static_cast<size_t>(address & 0x00FF);

        if (offset >= rom.size())
            return 0xFF;

        return rom[offset];
    }

    return 0xFF;
}

void IDE64Mapper::write(uint16_t address, uint8_t value)
{
    // IDE64 internal RAM
    if (!ctrl.game && ctrl.exrom &&
        ((address >= 0x1000 && address <= 0x7FFF) ||
         (address >= 0xC000 && address <= 0xCFFF)))
    {
        if (cart)
            cart->writeRAM(address & 0x7FFF, value);

        return;
    }

    // ROM is read-only in this window
    if (!ctrl.game && ctrl.exrom &&
        address >= 0xA000 && address <= 0xBFFF)
    {
        return;
    }

    // IDE/ATA controller registers: $DE20-$DE2F
    if (address >= IDE64_Controller_Start &&
        address <= IDE64_Controller_End)
    {
        controller.writeRegister(address, value);
        return;
    }

    if (address >= 0xDE32 && address <= 0xDE35)
    {
        const uint8_t bank =
            static_cast<uint8_t>(address - 0xDE32);

        ctrl.romAddr14 = (bank & 0x01) != 0;
        ctrl.romAddr15 = (bank & 0x02) != 0;

        ctrl.romBankRegs[bank] = value;

        ctrl.composeDE32();
        ctrl.romBankRegs[0] = ctrl.de32Raw;

        (void)applyMappingAfterLoad();
        return;
    }

    // DS1302 RTC
    if (address == RTC_Address)
    {
        if (!ctrl.killed)
            rtc.writeByte(value);

        return;
    }

    if (address >= IDE64_Ctrl_Cfg_Start && address <= IDE64_Ctrl_Cfg_End)
    {
        if (ctrl.killed)
            return;

        switch (address)
        {
            case 0xDEFB:
            {
                // Preserve the full kill-port byte in existing state.
                ctrl.memCfg[1] = value;
                ctrl.killed = (value & 0x01) != 0;

                // Bit 0 clear: latch only; mapping does not change.
                if (!ctrl.killed)
                    return;

                // Killing IDE64 selects standard/no-ROM configuration.
                ctrl.memCfg[0] = 2;
                ctrl.game = true;
                ctrl.exrom = true;
                break;
            }

            case 0xDEFC:
                ctrl.memCfg[0] = 1;
                ctrl.game = false;
                ctrl.exrom = false;
                break;

            case 0xDEFD:
                ctrl.memCfg[0] = 0;
                ctrl.game = true;
                ctrl.exrom = false;
                break;

            case 0xDEFE:
                ctrl.memCfg[0] = 3;
                ctrl.game = false;
                ctrl.exrom = true;
                break;

            case 0xDEFF:
                ctrl.memCfg[0] = 2;
                ctrl.game = true;
                ctrl.exrom = true;
                break;

            default:
                return;
        }

        ctrl.composeDE32();
        ctrl.romBankRegs[0] = ctrl.de32Raw;

        (void)applyMappingAfterLoad();
        return;
    }
}

bool IDE64Mapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart)
        return false;

    (void)bank; // IDE64 control bits select the bank.

    if (rom.empty() && !initializeROM())
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    if (ctrl.killed)
    {
        cart->setGameLine(true);
        cart->setExROMLine(true);
        return true;
    }

    cart->setGameLine(ctrl.game);
    cart->setExROMLine(ctrl.exrom);

    const size_t bankNumber =
        (ctrl.romAddr14 ? 1u : 0u) |
        (ctrl.romAddr15 ? 2u : 0u);

    const size_t romBase = bankNumber * 0x4000;

    // No cartridge ROM.
    if (ctrl.game && ctrl.exrom)
        return true;

    // ROML is visible in 8K, 16K, and Ultimax modes.
    for (size_t i = 0; i < 0x2000; ++i)
    {
        mem->writeCartridge(
            static_cast<uint16_t>(i),
            rom[romBase + i],
            cartLocation::LO);
    }

    // 8K mode: ROML only.
    if (ctrl.game && !ctrl.exrom)
        return true;

    // 16K mode: second half at $A000.
    if (!ctrl.game && !ctrl.exrom)
    {
        for (size_t i = 0; i < 0x2000; ++i)
        {
            mem->writeCartridge(
                static_cast<uint16_t>(i),
                rom[romBase + 0x2000 + i],
                cartLocation::HI);
        }

        return true;
    }

    // Ultimax mode: second half at $E000.
    if (!ctrl.game && ctrl.exrom)
    {
        for (size_t i = 0; i < 0x2000; ++i)
        {
            mem->writeCartridge(
                static_cast<uint16_t>(i),
                rom[romBase + 0x2000 + i],
                cartLocation::HI_E000);
        }
    }

    return true;
}

bool IDE64Mapper::initializeROM()
{
    rom.assign(0x10000, 0xFF);

    for (const auto& section : cart->getChipSections())
    {
        const size_t offset =
            static_cast<size_t>(section.bankNumber) * 0x4000;

        if (offset + section.data.size() > rom.size())
            return false;

        std::copy(section.data.begin(),
                  section.data.end(),
                  rom.begin() + offset);
    }

    return true;
}

bool IDE64Mapper::loadIDE64Image(uint32_t index, const std::string& path, bool readOnly)
{
    if (index == 0)
        return device0.loadImage(path, readOnly);

    if (index == 1)
        return device1.loadImage(path, readOnly);

    return false;
}

bool IDE64Mapper::createIDE64Image(uint32_t index, const std::string& path, uint32_t sectors)
{
    IDE64ImageDevice* dev = nullptr;

    if (index == 0)
        dev = &device0;
    else if (index == 1)
        dev = &device1;
    else
        return false;

    if (!dev->createImage(sectors))
        return false;

    return dev->saveImage(path);
}

bool IDE64Mapper::saveIDE64Image(uint32_t index)
{
    if (index == 0)
        return device0.flush();

    if (index == 1)
        return device1.flush();

    return false;
}

bool IDE64Mapper::ejectIDE64Image(uint32_t index)
{
    if (index == 0)
    {
        if (!device0.flush())
            return false;

        device0.clear();
        return true;
    }

    if (index == 1)
    {
        if (!device1.flush())
            return false;

        device1.clear();
        return true;
    }

    return false;
}

bool IDE64Mapper::savePersistence(const std::string& path) const
{
    return true;
}

bool IDE64Mapper::loadPersistence(const std::string& path)
{
    return true;
}

const char* IDE64Mapper::getIDE64DeviceName(uint32_t index) const
{
    switch(index)
    {
        case 0:
            return "Device 0";
        case 1:
            return "Device 1";
        default:
            return "Invalid";
    }
}

bool IDE64Mapper::isIDE64DevicePresent(uint32_t index) const
{
    switch (index)
    {
        case 0:
            return device0.isPresent();
        case 1:
            return device1.isPresent();
        default:
            return false;
    }
}

bool IDE64Mapper::isIDE64DeviceReadOnly(uint32_t index) const
{
    switch (index)
    {
        case 0: return device0.isReadOnly();
        case 1: return device1.isReadOnly();
        default: return false;
    }
}

bool IDE64Mapper::isIDE64DeviceDirty(uint32_t index) const
{
    switch (index)
    {
        case 0: return device0.isDirty();
        case 1: return device1.isDirty();
        default: return false;
    }
}

uint32_t IDE64Mapper::getIDE64DeviceSectorCount(uint32_t index) const
{
    switch (index)
    {
        case 0: return device0.getSectorCount();
        case 1: return device1.getSectorCount();
        default: return 0;
    }
}

const char* IDE64Mapper::getButtonName(uint32_t buttonIndex) const
{
    switch (buttonIndex)
    {
        case 0: return "Reset";
        default: return "";
    }
}

void IDE64Mapper::pressButton(uint32_t buttonIndex)
{
    switch (buttonIndex)
    {
        case 0:
            pressReset();
            break;

        default:
            break;
    }
}

void IDE64Mapper::pressReset()
{
    controller.reset();

    // Reset only the RTC interface/wire state, not battery-backed time.
    rtc.reset();

    ctrl.killed = false;
    ctrl.de32Raw = 0x12;
    ctrl.decodeDE32();
    ctrl.romBankRegs[0] = ctrl.de32Raw;

    for (int i = 1; i < 4; ++i)
        ctrl.romBankRegs[i] = 0x00;

    for (int i = 0; i < 4; ++i)
        ctrl.memCfg[i] = 0x00;

    applyMappingAfterLoad();
}

bool IDE64Mapper::cpuMemoryHandledByMapper(uint16_t address) const
{
    if (ctrl.killed)
        return false;

    // These IDE64 mappings are only active in Ultimax mode.
    if (!ctrl.game && ctrl.exrom)
    {
        if ((address >= 0x1000 && address <= 0x7FFF) ||
            (address >= 0xC000 && address <= 0xCFFF))
        {
            return true;
        }

        if (address >= 0xA000 && address <= 0xBFFF)
            return true;
    }

    return false;
}

bool IDE64Mapper::applyMappingAfterLoad()
{
    return loadIntoMemory(0);
}
