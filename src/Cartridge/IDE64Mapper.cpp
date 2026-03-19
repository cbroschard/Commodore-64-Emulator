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
}

bool IDE64Mapper::ControlState::load(StateReader& rdr)
{
    if (!rdr.readU8(de32Raw)) return false;

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

        if (!ctrl.load(rdr))                { rdr.exitChunkPayload(chunk); return false; }
        ctrl.decodeDE32();

        if (!rdr.readVectorU8(ram))         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU8(rom))         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU8(flashCfg))    { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // not our chunk
    return false;
}

void IDE64Mapper::reset()
{

}

uint8_t IDE64Mapper::read(uint16_t address)
{
    return 0xFF;
}

void IDE64Mapper::write(uint16_t address, uint8_t value)
{

}

bool IDE64Mapper::loadIntoMemory(uint8_t bank)
{
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
    return true;
}

void IDE64Mapper::applyMappingFromControl()
{

}
