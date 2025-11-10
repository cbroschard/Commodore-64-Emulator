// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1581_H
#define D1581_H

#include "Drive.h"
#include "D81.h"

class D1581 : public Drive
{
    public:
        D1581(int deviceNumber);
        virtual ~D1581();

        // Compatibility check
        bool canMount(DiskFormat fmt) const override;

        // ML Monitor
        const char* getDriveTypeName() const noexcept override { return "1581"; }

    protected:

    private:
};

#endif // D1581_H
