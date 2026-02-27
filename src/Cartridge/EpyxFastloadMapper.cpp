// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "EpyxFastloadMapper.h"

EpyxFastloadMapper::EpyxFastloadMapper() = default;

EpyxFastloadMapper::~EpyxFastloadMapper() = default;

void EpyxFastloadMapper::saveState(StateWriter& wrtr) const
{

}

bool EpyxFastloadMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    // Not our chunk
    return false;
}

uint8_t EpyxFastloadMapper::read(uint16_t address)
{
    return 0xFF; // Open bus
}

void EpyxFastloadMapper::write(uint16_t address, uint8_t value)
{

}

bool EpyxFastloadMapper::loadIntoMemory(uint8_t bank)
{
    bool mapper = false;
    return mapper;
}

bool EpyxFastloadMapper::applyMappingAfterLoad()
{
    return true;
}
