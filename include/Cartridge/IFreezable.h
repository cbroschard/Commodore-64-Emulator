// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IFREEZABLE_H_INCLUDED
#define IFREEZABLE_H_INCLUDED

struct IFreezable
{
    virtual ~IFreezable() = default;
    virtual void pressFreeze() = 0;
};

#endif // IFREEZABLE_H_INCLUDED
