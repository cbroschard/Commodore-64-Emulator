// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "DataBusLatch.h"

DataBusLatch::DataBusLatch() :
    latchedValue(0xFF),
    lastDriver(Driver::None)
{

}

DataBusLatch::~DataBusLatch() = default;

void DataBusLatch::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("OBUS");
    wrtr.writeU32(1); // version

    wrtr.writeU8(latchedValue);
    wrtr.writeU8(static_cast<uint8_t>(lastDriver));

    wrtr.endChunk();
}

bool DataBusLatch::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "OBUS", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(latchedValue))  { rdr.exitChunkPayload(chunk); return false; }

        uint8_t ld = 0;
        if (!rdr.readU8(ld))            { rdr.exitChunkPayload(chunk); return false; }
        lastDriver = static_cast<Driver>(ld);

        rdr.exitChunkPayload(chunk);

        return true;
    }

    return false;
}

void DataBusLatch::drive(uint8_t value, Driver driver)
{
    latchedValue = value;
    lastDriver = driver;
}

uint8_t DataBusLatch::sample() const
{
    return latchedValue;
}

DataBusLatch::Driver DataBusLatch::getLastDriver() const
{
    return lastDriver;
}

void DataBusLatch::reset()
{
    latchedValue = 0xFF;
    lastDriver = Driver::None;
}
