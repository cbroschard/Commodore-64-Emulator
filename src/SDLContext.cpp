// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <SDL3/SDL.h>
#include <stdexcept>
#include <string>
#include "SDLContext.h"

SDLContext::SDLContext()
{
    const SDL_InitFlags flags =
        SDL_INIT_VIDEO |
        SDL_INIT_AUDIO |
        SDL_INIT_GAMEPAD |
        SDL_INIT_HAPTIC;

    if (!SDL_Init(flags))
    {
        throw std::runtime_error(
            std::string("SDL initialization failed: ") +
            SDL_GetError()
        );
    }
}

SDLContext::~SDLContext()
{
    SDL_Quit();
}
