// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "MonitorController.h"

MonitorController::MonitorController(std::atomic<bool>& uiPausedRef) :
    monitor(nullptr),
    win(nullptr),
    uiPaused(uiPausedRef),
    pausedByThis(false)
{

}

MonitorController::~MonitorController()
{
    try
    {
        close();
    }
    catch(...)
    {

    }
}

void MonitorController::ensureWindow()
{
    if (!win)
        win = std::make_unique<SDLMonitorWindow>();
}

void MonitorController::open()
{
    ensureWindow();

    // Do not restart the monitor session if it is already open.
    if (win->isOpen())
        return;

    // Start a fresh monitor session. This resets the disassembly cursor.
    if (monitor)
        monitor->enterMonitor();

    uiPaused = true;
    pausedByThis = true;

    const bool opened =
        win->open(
            "ML Monitor",
            900,
            550,
            [this](const std::string& cmd) -> std::string
            {
                if (!monitor)
                    return "Monitor not available\n";

                return monitor->executeAndCapture(cmd);
            },
            [this]() -> std::string
            {
                if (!monitor)
                    return "> ";

                return monitor->getPrompt();
            }
        );

    if (!opened)
    {
        if (monitor)
            monitor->leaveMonitor();

        if (pausedByThis.exchange(false))
            uiPaused = false;

        return;
    }

    // Show anything queued before the UI opened.
    drainAsyncLines();
}

void MonitorController::close()
{
    if (win && win->isOpen())
        win->close();

    onClosed();
}

void MonitorController::toggle()
{
    if (isOpen()) close();
    else open();
}

void MonitorController::onClosed()
{
    // End the monitor session and reset stateful commands such as "d".
    if (monitor)
        monitor->leaveMonitor();

    // Only resume if this controller paused the emulator.
    if (pausedByThis.exchange(false))
        uiPaused = false;
}

void MonitorController::drainAsyncLines()
{
    if (!monitor || !win || !win->isOpen())
        return;

    for (const auto& line : monitor->drainAsyncLines())
        win->appendLine(line);
}

void MonitorController::appendLine(const std::string& line)
{
    if (win && win->isOpen())
        win->appendLine(line);
}

bool MonitorController::handleEvent(const SDL_Event& ev)
{
    if (!isOpen())
        return false;

    win->handleEvent(ev);

    // If a monitor command requested "exit" (g/quit/etc.), close it.
    if (monitor && !monitor->getRunningFlag() && isOpen())
        win->close();

    // If the monitor closed (X/ESC/flag), unpause cleanly.
    if (!isOpen())
        onClosed();

    // While monitor is open, swallow keyboard/text so it doesn't hit the emulator.
    if (ev.type == SDL_EVENT_TEXT_INPUT || ev.type == SDL_EVENT_TEXT_EDITING || ev.type == SDL_EVENT_KEY_DOWN ||
          ev.type == SDL_EVENT_KEY_UP)
    {
        return true;
    }

    return false;
}

void MonitorController::tick()
{
    if (!isOpen())
        return;

    // Close if monitor requested exit.
    if (monitor && !monitor->getRunningFlag())
    {
        win->close();
        onClosed();
        return;
    }

    drainAsyncLines();
    win->render();

    if (!isOpen())
        onClosed();
}
