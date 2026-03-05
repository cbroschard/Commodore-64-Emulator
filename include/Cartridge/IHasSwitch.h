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

        virtual uint32_t getSwitchCount() const = 0;
        virtual const char* getSwitchName(uint32_t switchIndex) const = 0;

        virtual uint32_t getSwitchPositionCount(uint32_t switchIndex) const = 0;
        virtual uint32_t getSwitchPosition(uint32_t switchIndex) const = 0;
        virtual const char* getSwitchPositionLabel(uint32_t switchIndex, uint32_t pos) const = 0;

        virtual void setSwitchPosition(uint32_t switchIndex, uint32_t pos) = 0;
};

#endif // IHASSWITCH_H_INCLUDED
