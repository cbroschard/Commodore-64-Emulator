// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef ICARTRIDGEHOST_H_INCLUDED
#define ICARTRIDGEHOST_H_INCLUDED

class ICartridgeHost
{
    public:
        virtual ~ICartridgeHost() = default;

        virtual void requestWarmReset() = 0;
        virtual void requestColdReset() = 0;
        virtual void requestCartridgeNMI() = 0;
};

#endif // ICARTRIDGEHOST_H_INCLUDED
