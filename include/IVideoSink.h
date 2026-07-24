// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IVIDEOSINK_H
#define IVIDEOSINK_H

#include <cstdint>

class IVideoSink
{
    public:
        virtual ~IVideoSink() = default;

        virtual void renderBackgroundLine(int row, uint8_t color, int x0, int x1) = 0;

        virtual void renderBorderLine(int row, uint8_t color, int x0, int x1) = 0;

        virtual void setPixel(int x, int y, uint8_t color) = 0;

        virtual void setPixel(int x, int y, uint8_t color, int hardwareX) = 0;

        virtual void setScreenDimensions(int visibleW, int visibleH, int border) = 0;

    protected:

    private:
};

#endif // IVIDEOSINK_H
