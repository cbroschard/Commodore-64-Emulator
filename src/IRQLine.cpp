// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "IRQLine.h"

IRQLine::IRQLine() : IRQActive(false), IRQSources(NONE)
{

}

IRQLine::~IRQLine() = default;

void IRQLine::raiseIRQ(Source source)
{
    IRQSources |= source;
    updateIRQ();
}

void IRQLine::clearIRQ(Source source)
{
    IRQSources &= ~source;
    updateIRQ();
}

uint8_t IRQLine::getHighestPrioritySource()
{
    if (IRQSources & VICII)         return VICII;
    if (IRQSources & CIA1_TIMER_A)  return CIA1_TIMER_A;
    if (IRQSources & CIA1_TIMER_B)  return CIA1_TIMER_B;
    if (IRQSources & CIA1_TOD)      return CIA1_TOD;
    if (IRQSources & CIA1_SERIAL)   return CIA1_SERIAL;
    if (IRQSources & CIA1_FLAG)     return CIA1_FLAG;
    return NONE;
}
