// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IDRIVEINDICATORVIEW_H_INCLUDED
#define IDRIVEINDICATORVIEW_H_INCLUDED

class IDriveIndicatorView
{
    public:
        struct Indicator
        {
            std::string name;
            bool on = false;
            EmulatorUI::DriveLightColor color = EmulatorUI::DriveLightColor::Green;
        };

        virtual ~IDriveIndicatorView() = default;
        virtual void getDriveIndicators(std::vector<Indicator>& out) const = 0;
};


#endif // IDRIVEINDICATORVIEW_H_INCLUDED
