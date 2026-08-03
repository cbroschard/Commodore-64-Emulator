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
