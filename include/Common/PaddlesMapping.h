// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef PADDLESMAPPING_H_INCLUDED
#define PADDLESMAPPING_H_INCLUDED

#include <SDL3/SDL.h>

// Struct to hold the paddle 1 and 2 mappings from configuration file
struct PaddlesMapping
{
    SDL_Scancode decreaseX;
    SDL_Scancode increaseX;
    SDL_Scancode decreaseY;
    SDL_Scancode increaseY;
    SDL_Scancode buttonX;
    SDL_Scancode buttonY;
};

#endif // PADDLESMAPPING_H_INCLUDED
