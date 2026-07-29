// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/RexEP256Mapper.h"
#include "Memory.h"

RexEP256Mapper::RexEP256Mapper() :
    selectedSocket(0),
    selectedSlice(0),
    disabled(false)
{

}

RexEP256Mapper::~RexEP256Mapper() = default;

void RexEP256Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("RE25");
    wrtr.writeU32(1);
    wrtr.writeU8(selectedSocket);
    wrtr.writeU8(selectedSlice);
    wrtr.writeBool(disabled);
    wrtr.endChunk();
}

bool RexEP256Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "RE25", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t version = 0;

    if (!rdr.readU32(version))                      { rdr.exitChunkPayload(chunk); return false; }

    if (version != 1)                               { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedSocket))                { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedSlice))                 { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(disabled))                    { rdr.exitChunkPayload(chunk); return false; }

    if (selectedSocket > 8 || selectedSlice > 3)    { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

uint8_t RexEP256Mapper::read(uint16_t address)
{
    if (!cart || !mem)
        return 0xFF;

    if (address == 0xDFC0)
    {
        disabled = true;
        cart->setGameLine(true);
        cart->setExROMLine(true);
    }
    else if (address == 0xDFE0)
    {
        disabled = false;
        cart->setGameLine(true);
        cart->setExROMLine(false);
    }

    return mem->getLastBus();
}

void RexEP256Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart || !mem)
        return;

    if (address != 0xDFA0)
        return;

    const uint8_t newSocket = static_cast<uint8_t>((value & 0x07) + 1);

    const uint8_t newSlice = static_cast<uint8_t>((value >> 4) & 0x03);

    if (!loadSocketSlice(newSocket, newSlice))
        return;

    selectedSocket = newSocket;
    selectedSlice = newSlice;
}

bool RexEP256Mapper::loadIntoMemory(uint8_t bank)
{
    return loadSocketSlice(bank, 0);
}

bool RexEP256Mapper::loadSocketSlice(uint8_t socket, uint8_t slice)
{
    if (!cart || !mem)
        return false;

    if (socket > 8)
        return false;

    slice &= 0x03;

    const Cartridge::chipSection* selectedSection = nullptr;

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != socket)
            continue;

        if (section.loadAddress != CART_LO_START)
            continue;

        if (section.data.size() != 0x2000 && section.data.size() != 0x4000 && section.data.size() != 0x8000)
            continue;

        selectedSection = &section;
        break;
    }

    if (!selectedSection)
    {
        std::cerr
            << "Rex EP256: Socket "
            << static_cast<unsigned>(socket)
            << " not found.\n";

        return false;
    }

    size_t offset = 0;

    switch (selectedSection->data.size())
    {
        case 0x2000:
            // 8K EPROM: all four slice values select the same data.
            offset = 0;
            break;

        case 0x4000:
            // 16K EPROM:
            // slices 0 and 2 select lower 8K;
            // slices 1 and 3 select upper 8K.
            offset =
                static_cast<size_t>(slice & 0x01) *
                0x2000;
            break;

        case 0x8000:
            // 32K EPROM: four distinct 8K slices.
            offset =
                static_cast<size_t>(slice) *
                0x2000;
            break;

        default:
            return false;
    }

    if (offset + 0x2000 > selectedSection->data.size())
        return false;

    cart->clearCartridge(cartLocation::LO);

    for (size_t i = 0; i < 0x2000; ++i)
        mem->writeCartridge(static_cast<uint16_t>(i), selectedSection->data[offset + i], cartLocation::LO);

    return true;
}

bool RexEP256Mapper::applyMappingAfterLoad()
{
    if (!cart || !mem)
        return false;

    if (!loadSocketSlice(selectedSocket, selectedSlice))
        return false;

    cart->setGameLine(true);
    cart->setExROMLine(disabled);

    return true;
}

void RexEP256Mapper::reset()
{
    selectedSocket = 0;
    selectedSlice = 0;
    disabled = false;

    if (!cart || !mem)
        return;

    if (!loadSocketSlice(selectedSocket, selectedSlice))
        return;

    cart->setGameLine(true);
    cart->setExROMLine(false);
}
