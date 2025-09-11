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

        enum Source
        {
            NONE,
            CIA1_TIMER_A,
            CIA1_TIMER_B,
            CIA1_TOD,
            CIA1_SERIAL,
            CIA1_FLAG,
            D1541_IRQ,
            VICII
        };

        void raiseIRQ(Source source);
        void clearIRQ(Source source);
        inline bool isIRQActive() { return IRQActive; }
        inline uint8_t getActiveSources() { return IRQSources; }
        uint8_t getHighestPrioritySource();

    protected:

    private:

        bool IRQActive;
        uint8_t IRQSources;

        inline void updateIRQ() { IRQActive = (IRQSources != 0); }

};

#endif // IRQLINE_H
