// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef JOYSTICKMAPPING_H_INCLUDED
#define JOYSTICKMAPPING_H_INCLUDED

// Struct to hold the joystick 1 and 2 mappings from configuration file
struct JoystickMapping
{
    SDL_Scancode up;
    SDL_Scancode down;
    SDL_Scancode left;
    SDL_Scancode right;
    SDL_Scancode fire;
};

#endif // JOYSTICKMAPPING_H_INCLUDED
