// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef ICPUATTACHABLE_H_INCLUDED
#define ICPUATTACHABLE_H_INCLUDED

class CPU; // forward declaration

struct ICPUAttachable
{
    virtual ~ICPUAttachable() = default;
    virtual void attachCPUInstance(CPU* cpu) = 0;
};

#endif // ICPUATTACHABLE_H_INCLUDED
