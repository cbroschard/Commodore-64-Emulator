// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IDRIVEPOSITIONVIEW_H_INCLUDED
#define IDRIVEPOSITIONVIEW_H_INCLUDED

class IDrivePositionView
{
    public:
        virtual ~IDrivePositionView() = default;
        virtual bool hasTrackSector() const = 0;
        virtual int getTrack() const = 0;
        virtual int getSector() const = 0;
};

#endif // IDRIVEPOSITIONVIEW_H_INCLUDED
