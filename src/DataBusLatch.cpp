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
    decayRemaining{},
    lastDrivenCycle{},
    maxObservedAge{}
{

}

DataBusLatch::~DataBusLatch() = default;

void DataBusLatch::reset()
{
    latchedValue    = 0xFF;
    lastDriver      = Driver::None;

    lastUpdateCycle = 0;

    for (int bit = 0; bit < 8; ++bit)
    {
        decayRemaining[bit] = 0;
        lastDrivenCycle[bit] = 0;
        maxObservedAge[bit] = 0;
    }
}

void DataBusLatch::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("OBUS");
    wrtr.writeU32(2); // version

    wrtr.writeU8(latchedValue);

    wrtr.writeU8(static_cast<uint8_t>(lastDriver));

    for (int bit = 0; bit < 8; ++bit)
        wrtr.writeU64(decayRemaining[bit]);

    wrtr.endChunk();
}

bool DataBusLatch::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "OBUS", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                          { rdr.exitChunkPayload(chunk); return false; }
    if (ver < 1 || ver > 2)                         { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(latchedValue))                  { rdr.exitChunkPayload(chunk); return false; }

    uint8_t ld = 0;
    if (!rdr.readU8(ld))                            { rdr.exitChunkPayload(chunk); return false; }
    lastDriver = static_cast<Driver>(ld);

    if (ver == 1)
    {
        for (auto& decay : decayRemaining)
            decay = 0;
    }
    else
    {
        for (int bit = 0; bit < 8; ++bit)
            if (!rdr.readU64(decayRemaining[bit]))  { rdr.exitChunkPayload(chunk); return false; }
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

         // This bit was physically driven now.
        lastDrivenCycle[bit] = cycle;

        if ((value & mask) != 0)
            decayRemaining[bit] = 0;
        else
            decayRemaining[bit] = DEFAULT_DECAY_CYCLES;
    }

    if (driveMask != 0)
        lastDriver = driver;
}

uint8_t DataBusLatch::sample() const
{
    return latchedValue;
}

uint8_t DataBusLatch::sample(uint64_t cycle)
{
    updateDecay(cycle);

    for (int bit = 0; bit < 8; ++bit)
    {
        if (cycle < lastDrivenCycle[bit])
            continue;

        const uint64_t age = cycle - lastDrivenCycle[bit];

        if (age > maxObservedAge[bit])
            maxObservedAge[bit] = age;
    }

    return latchedValue;
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

void DataBusLatch::clearDiagnostics()
{
    for (int bit = 0; bit < 8; ++bit)
        maxObservedAge[bit] = 0;
}

uint64_t DataBusLatch::getLastDrivenCycle(int bit) const
{
    if (bit < 0 || bit >= 8)
        return 0;

    return lastDrivenCycle[bit];
}

const char* DataBusLatch::driverToString(Driver driver)
{
    switch (driver)
    {
        case Driver::Cartridge: return "Cartridge";
        case Driver::CIA1:      return "CIA1";
        case Driver::CIA2:      return "CIA2";
        case Driver::CPU:       return "CPU";
        case Driver::Memory:    return "Memory";
        case Driver::REU:       return "REU";
        case Driver::SID:       return "SID";
        case Driver::VIC:       return "VIC";
        case Driver::None:
        default:
            return "None";
    }
}
