// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IDRIVEUIVIEW_H_INCLUDED
#define IDRIVEUIVIEW_H_INCLUDED

#include <string>

class IDriveUiView
{
    public:
        virtual ~IDriveUiView() = default;
        virtual const char* getDriveModelName() const = 0;
        virtual bool hasDiskInserted() const = 0;
        virtual std::string getMountedImagePath() const = 0;
};

#endif // IDRIVEUIVIEW_H_INCLUDED
