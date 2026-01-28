// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef INPUTROUTER_H
#define INPUTROUTER_H

#include <atomic>
#include <functional>
#include <sdl2/sdl.h>

// Forward declarations
class MonitorController;
class InputManager;
class MediaManager;

class InputRouter
{
    public:
        using VoidFn = std::function<void()>;

        InputRouter(std::atomic<bool>& uiPaused,
                    MonitorController* monitorCtl,
                    InputManager* input,
                    MediaManager* media,
                    VoidFn warmReset,
                    VoidFn coldReset);

        virtual ~InputRouter();

        // Returns true if consumed (caller should "continue")
        bool handleEvent(const SDL_Event& ev);

    protected:

    private:
        std::atomic<bool>& uiPaused_;
        MonitorController* monitorCtl_ = nullptr;
        InputManager* input_ = nullptr;
        MediaManager* media_ = nullptr;

        VoidFn warmReset_;
        VoidFn coldReset_;

        bool handleGlobalHotkeys_(const SDL_Event& ev);
        bool handleControllerHotplug_(const SDL_Event& ev);
};

#endif // INPUTROUTER_H
