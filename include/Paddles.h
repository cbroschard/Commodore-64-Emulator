// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef PADDLES_H
#define PADDLES_H

#include <cstdint>

class Paddles
{
    public:
        Paddles();
        virtual ~Paddles();

        void reset();

        void setX(int port, uint8_t value);
        void setY(int port, uint8_t value);

        uint8_t getX(int port) const;
        uint8_t getY(int port) const;

        void setButtonX(int port, bool pressed);
        void setButtonY(int port, bool pressed);

        bool getButtonX(int port) const;
        bool getButtonY(int port) const;

    private:
        static constexpr int NUM_PORTS = 2;

        struct PaddlePair
        {
            uint8_t x = 0x80;
            uint8_t y = 0x80;
            bool buttonX = false;
            bool buttonY = false;
        };

        PaddlePair ports[2];
};

#endif // PADDLES_H
