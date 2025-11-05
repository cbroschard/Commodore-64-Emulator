// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "D1571CIA.h"

D1571CIA::D1571CIA() :
    timerACounter(0),
    timerALatch(0),
    timerBCounter(0),
    timerBLatch(0),
    timerARunning(false),
    timerBRunning(false)
{
    //ctor
}

D1571CIA::~D1571CIA()
{
    //dtor
}
