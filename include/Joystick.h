// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <cstdint>
#include <stdexcept>
#include "Logging.h"

class Joystick
{
    public:
        Joystick(int port);
        virtual ~Joystick();

        inline void attachLogInstance(Logging* logger) { this->logger = logger; }

        enum direction
        {
            up = 0x01,
            down = 0x02,
            right = 0x08,
            left = 0x04,
            button = 0x10,
        };

        // Joystick state methods
        uint8_t getState() const;
        void setState(uint8_t newState);
        bool isDirectionPressed(direction dir) const;
        bool isButtonPressed() const;

        // Port method
        int getPort() const;

        // ML Monitor logging
        inline void setLog(bool enable) { setLogging = enable; }

    protected:

    private:

        // Non-owning pointers
        Logging* logger;

        int port; // Joystick 1 or 2
        uint8_t state; // Joystick state (active-low, 0 means pressed)

        // ML Monitor logging
        bool setLogging;
};

#endif // JOYSTICK_H
