// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IHASSWITCH_H_INCLUDED
#define IHASSWITCH_H_INCLUDED

#include <cstdint>

class IHasSwitch
{
    public:
        virtual ~IHasSwitch() = default;

        // How many positions does the switch have? (2 for ON/OFF, 3 for OFF/ON/PRG, etc.)
        virtual uint32_t getSwitchPositionCount() const = 0;

        // Current position as an index [0..count-1]
        virtual uint32_t getSwitchPosition() const = 0;

        // Set position (UI calls this)
        virtual void setSwitchPosition(uint32_t pos) = 0;

        // nice UI label per position
        virtual const char* getSwitchPositionLabel(uint32_t pos) const = 0;

        // name shown in UI (“Mode”, “Switch”, “Expert Switch”, etc.)
        virtual const char* getSwitchName() const = 0;
};

#endif // IHASSWITCH_H_INCLUDED
