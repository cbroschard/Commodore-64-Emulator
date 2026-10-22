// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "IEEE488/IEEE488Drive.h"

IEEE488Drive::IEEE488Drive() = default;

IEEE488Drive::~IEEE488Drive() = default;

void IEEE488Drive::reset()
{
    IEEE488Device::reset();
}
