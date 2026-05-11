// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "REU.h"

REU::REU() :
    model(REUModel::None)
{

}

REU::~REU() = default;

void REU::reset()
{

}

uint8_t REU::readIO(uint16_t address)
{
    return 0xFF;
}

void REU::writeIO(uint16_t address, uint8_t value)
{

}

void REU::setModel(REUModel reuModel)
{
    model = reuModel;

    const std::size_t bytes = bytesForREUModel(model);

    ram.clear();
    ram.resize(bytes, 0x00);

    reset();
}
