// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "D1571.h"

D1571::D1571(int deviceNumber)
{

}

D1571::~D1571() = default;

bool D1571::canMount(DiskFormat fmt) const
{
    return fmt == DiskFormat::D64 || fmt == DiskFormat::D71;
}
