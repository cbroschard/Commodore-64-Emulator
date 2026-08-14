// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef NMILINE_H
#define NMILINE_H

#include <cstdint>

class NMILine
{
    public:
        NMILine();
        virtual ~NMILine();

        enum Source
        {
            NONE        = 0x00,
            CIA2        = 0x01,
            SWIFTLINK   = 0x02,
            CARTRIDGE   = 0x04,
            RESTORE     = 0x08,
            TURBO232    = 0x10
        };

        void raiseNMI(Source source);
        void clearNMI(Source source);

        inline bool isNMIActive() const { return nmiActive; }
        inline uint8_t getActiveSources() const { return nmiSources; }

    private:
        bool nmiActive;
        uint8_t nmiSources;

        inline void updateNMI() { nmiActive = (nmiSources != 0); }
};

#endif // NMILINE_H
