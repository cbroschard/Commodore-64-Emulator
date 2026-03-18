// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "EEPROM/SerialEEPROM93C86.h"

SerialEEPROM93C86::SerialEEPROM93C86() :
    cs(false),
    clk(false),
    di(false),
    dout(false),
    shiftReg(0),
    bitCount(0),
    prevClk(false)
{
    // Set to "erased" EEPROM on start
    std::fill(std::begin(data), std::end(data), 0xFF);
}

SerialEEPROM93C86::~SerialEEPROM93C86() = default;

void SerialEEPROM93C86::reset()
{

    // Reset all lines
    cs          = false;
    clk         = false;
    di          = false;
    dout        = false;

    // Reset protocol state
    shiftReg    = 0;
    bitCount    = 0;
    prevClk     = false;
}

void SerialEEPROM93C86::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("EE93");
    wrtr.writeU32(1); // version

    for (uint8_t b : data)
        wrtr.writeU8(b);

    wrtr.writeBool(cs);
    wrtr.writeBool(clk);
    wrtr.writeBool(di);
    wrtr.writeBool(dout);

    wrtr.writeU32(shiftReg);
    wrtr.writeU32(bitCount);
    wrtr.writeBool(prevClk);

    wrtr.endChunk();
}

bool SerialEEPROM93C86::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "EE93", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

        for (auto& b : data)
        {
            if (!rdr.readU8(b))
            {
                rdr.exitChunkPayload(chunk);
                return false;
            }
        }

        if (!rdr.readBool(cs))          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(clk))         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(di))          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(dout))        { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU32(shiftReg))     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU32(bitCount))     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(prevClk))     { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // Not our chunk
    return false;
}

bool SerialEEPROM93C86::savePersistence(const std::string& path) const
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;

    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));

    return out.good();
}

bool SerialEEPROM93C86::loadPersistence(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return false;

    in.read(reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()));

    if (!in.good() && !in.eof())
        return false;

    return true;
}

