// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MONITORCONTROLLER_H
#define MONITORCONTROLLER_H

#include <atomic>
#include <memory>
#include <sdl2/sdl.h>
#include <string>
#include "Debug/MLMonitor.h"
#include "SDLMonitorWindow.h"

class MonitorController
{
    public:
        MonitorController(std::atomic<bool>& uiPausedRef);
        virtual ~MonitorController() noexcept;

        inline void attachMonitorInstance(MLMonitor* monitor) { this->monitor = monitor; }

        void open();     // ensure open + pause
        void close();    // close + resume if we paused
        void toggle();   // open if closed, close if open

        inline bool isOpen() const { return win && win->isOpen(); }

        bool handleEvent(const SDL_Event& ev);
        void tick();
        void appendLine(const std::string& line);

    protected:

    private:

        MLMonitor* monitor;
        std::unique_ptr<SDLMonitorWindow> win;

        std::atomic<bool>& uiPaused;
        std::atomic<bool> pausedByThis;

        void ensureWindow();
        void drainAsyncLines();
        void onClosed();
};

#endif // MONITORCONTROLLER_H
