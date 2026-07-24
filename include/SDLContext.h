// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef SDLCONTEXT_H
#define SDLCONTEXT_H


class SDLContext
{
    public:
        SDLContext();
        virtual ~SDLContext();

        SDLContext(const SDLContext&) = delete;
        SDLContext& operator=(const SDLContext&) = delete;

        SDLContext(SDLContext&&) = delete;
        SDLContext& operator=(SDLContext&&) = delete;

    protected:

    private:
};

#endif // SDLCONTEXT_H
