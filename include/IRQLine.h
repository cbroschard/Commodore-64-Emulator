// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IRQLINE_H
#define IRQLINE_H

#include <cstdint>

class IRQLine
{
    public:
        IRQLine();
        virtual ~IRQLine();

        enum Source : uint32_t
        {
            NONE        = 0,
            CIA1        = 1u << 0,
            D1541_IRQ   = 1u << 1,
            D1571_IRQ   = 1u << 2,
            D1581_IRQ   = 1u << 3,
            VICII       = 1u << 4,
            MONITOR     = 1u << 5,
            REU         = 1u << 6,
            D2031_IRQ   = 1u << 7,

            D4040_IRQ   = 1u << 8,
            D8050_IRQ   = 1u << 9,
            D8250_IRQ   = 1u << 10
        };

        void raiseIRQ(Source source);
        void clearIRQ(Source source);
        inline bool isIRQActive() { return IRQActive; }
        inline uint32_t getActiveSources() { return IRQSources; }

    protected:

    private:

        bool IRQActive;
        uint8_t IRQSources;

        inline void updateIRQ() { IRQActive = (IRQSources != 0); }

};

#endif // IRQLINE_H
