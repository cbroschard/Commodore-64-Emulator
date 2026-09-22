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
    lastDriver(Driver::None),
    lastUpdateCycle(0),
    decayRemaining{}
{

}

DataBusLatch::~DataBusLatch() = default;

void DataBusLatch::reset()
{
    latchedValue    = 0xFF;
    lastDriver      = Driver::None;

    lastUpdateCycle = 0;

    for (int i = 0; i < 8; ++i)
        decayRemaining[i] = 0;
}

void DataBusLatch::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("OBUS");
    wrtr.writeU32(2); // version

    wrtr.writeU8(latchedValue);

    wrtr.writeU8(static_cast<uint8_t>(lastDriver));

    for (int i = 0; i < 8; ++i)
        wrtr.writeU64(decayRemaining[i]);

    wrtr.endChunk();
}

bool DataBusLatch::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "OBUS", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                      { rdr.exitChunkPayload(chunk); return false; }
    if (ver < 1 || ver > 2)                               { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(latchedValue))              { rdr.exitChunkPayload(chunk); return false; }

    uint8_t ld = 0;
    if (!rdr.readU8(ld))                        { rdr.exitChunkPayload(chunk); return false; }
    lastDriver = static_cast<Driver>(ld);

    if (ver == 1)
    {
        for (auto& decay : decayRemaining)
            decay = 0;
    }
    else
    {
        for (int i = 0; i < 8; ++i)
            if (!rdr.readU64(decayRemaining[i]))    { rdr.exitChunkPayload(chunk); return false; }
    }

    lastUpdateCycle = 0;

    rdr.exitChunkPayload(chunk);
    return true;
}

void DataBusLatch::drive(uint8_t value, Driver driver, uint64_t cycle)
{
    drive(value, 0xFF, driver, cycle);
}

void DataBusLatch::drive(uint8_t value, uint8_t driveMask, Driver driver, uint64_t cycle)
{
    updateDecay(cycle);

    latchedValue = static_cast<uint8_t>((latchedValue & ~driveMask) | (value & driveMask));

    for (int bit = 0; bit < 8; ++bit)
    {
        const uint8_t mask = static_cast<uint8_t>(1u << bit);

        if ((driveMask & mask) == 0)
            continue;

        if ((value & mask) != 0)
            decayRemaining[bit] = 0;
        else
            decayRemaining[bit] = DEFAULT_DECAY_CYCLES;
    }

    lastDriver = driver;
}

uint8_t DataBusLatch::sample() const
{
    return latchedValue;
}

uint8_t DataBusLatch::sample(uint64_t cycle)
{
    updateDecay(cycle);
    return latchedValue;
}

DataBusLatch::Driver DataBusLatch::getLastDriver() const
{
    return lastDriver;
}

void DataBusLatch::updateDecay(uint64_t cycle)
{
    if (cycle <= lastUpdateCycle)
        return;

    const uint64_t elapsed = cycle - lastUpdateCycle;

    for (int bit = 0; bit < 8; ++bit)
    {
        const uint8_t mask = static_cast<uint8_t>(1u << bit);

        // This model decays retained low levels toward high.
        if ((latchedValue & mask) != 0)
            continue;

        // Zero means no decay is currently pending.
        if (decayRemaining[bit] == 0)
            continue;

        if (elapsed >= decayRemaining[bit])
        {
            latchedValue |= mask;
            decayRemaining[bit] = 0;
        }
        else
            decayRemaining[bit] -= elapsed;
    }

    lastUpdateCycle = cycle;
}
