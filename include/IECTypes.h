// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IECTYPES_H_INCLUDED
#define IECTYPES_H_INCLUDED

// IEC Bus Line State
struct IECBusLines
{
    bool atn = true; // High = inactive
    bool clk = true; // High = released
    bool data = true; // High = released

    // Method to calculate the actual line state based on who is driving low
    void updateLineState(bool c64DrivesClkLow, bool c64DrivesDataLow,
                         bool peripheralDrivesClkLow, bool peripheralDrivesDataLow,
                         bool c64DrivesAtnLow, bool peripheralDrivesAtnLow)
    {
        atn = !(c64DrivesAtnLow || peripheralDrivesAtnLow);
        clk = !(c64DrivesClkLow || peripheralDrivesClkLow);
        data = !(c64DrivesDataLow || peripheralDrivesDataLow);
    }
};

#endif // IECTYPES_H_INCLUDED
