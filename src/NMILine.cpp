// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "NMILine.h"

NMILine::NMILine() :
    nmiActive(false),
    nmiSources(NONE)
{

}

NMILine::~NMILine() = default;

void NMILine::raiseNMI(Source source)
{
    nmiSources |= source;
    updateNMI();
}

void NMILine::clearNMI(Source source)
{
    nmiSources &= static_cast<uint8_t>(~source);
    updateNMI();
}
