// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef PLAMAPPER_H
#define PLAMAPPER_H

#include <cstdint>
#include <vector>
#include "PLA.h"

class PLAMapper
{
    public:
        PLAMapper();
        virtual ~PLAMapper();

        struct regionMapping
        {
            uint16_t start;       // starting address (inclusive)
            uint16_t end;         // ending address (inclusive)
            PLA::memoryBank bank;      // which bank to use in this region
            uint16_t offsetBase;  // value to subtract from the CPU address for bank access
        };

        struct modeMapping
        {
            std::vector<regionMapping> regions;
        };

        static const modeMapping* getMappings();

    protected:

    private:
};

#endif // PLAMAPPER_H
