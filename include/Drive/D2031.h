// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D2031_H
#define D2031_H

#include <cstdint>
#include "Drive/Drive.h"

class D2031 : public Drive
{
    public:
        D2031();
        ~D2031();

        enum class D2031Model : uint8_t
        {
            HighProfile,
            LowProfile
        };

    private:
};

#endif // D2031_H
