// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D4040.h"

D4040::D4040()
{
    reset();
}

D4040::~D4040()
{

}

void D4040::reset()
{
    motorOn     = false;
}

void D4040::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("4040");
    wrtr.writeU32(1); // Version
    wrtr.writeU8(static_cast<uint8_t>(deviceNumber));

    wrtr.endChunk();
}

bool D4040::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "4040", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))              { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                       { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

void D4040::tick(uint32_t cycles)
{

}

void D4040::updateIRQ()
{

}
