// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "D1571VIA.h"

D1571VIA::D1571VIA() :
    t1Counter(0),
    t1Latch(0),
    t1Running(false),
    t2Counter(0),
    t2Latch(0)
{
    //ctor
}

D1571VIA::~D1571VIA()
{
    //dtor
}
