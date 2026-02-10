// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "InputRouter.h"
#include "MonitorController.h"
#include "InputManager.h"
#include "MediaManager.h"

InputRouter::InputRouter(std::atomic<bool>& uiPaused,
                         MonitorController* monitorCtl,
                         InputManager* input,
                         MediaManager* media,
                         VoidFn warmReset,
                         VoidFn coldReset)
    : uiPaused_(uiPaused),
      monitorCtl_(monitorCtl),
      input_(input),
      media_(media),
      warmReset_(std::move(warmReset)),
      coldReset_(std::move(coldReset))
{

}

InputRouter::~InputRouter() = default;

bool InputRouter::handleEvent(const SDL_Event& ev)
{
    // 1) Controller hotplug should be handled explicitly (matches your boot loop intent)
    if (handleControllerHotplug_(ev))
        return true;

    // 2) Global hotkeys BEFORE monitorCtl->handleEvent (this matches your comment)
    if (handleGlobalHotkeys_(ev))
        return true;

    // 3) Let monitor consume events next
    if (monitorCtl_ && monitorCtl_->handleEvent(ev))
        return true;

    // 4) Feed InputManager last (keyboard/joystick mapping)
    if (input_)
        return input_->handleEvent(ev);

    return false;
}

bool InputRouter::handleControllerHotplug_(const SDL_Event& ev)
{
    if (!input_) return false;

    if (ev.type == SDL_CONTROLLERDEVICEADDED)
    {
        input_->handleControllerDeviceAdded(ev.cdevice.which);
        return true;
    }

    if (ev.type == SDL_CONTROLLERDEVICEREMOVED)
    {
        input_->handleControllerDeviceRemoved((SDL_JoystickID)ev.cdevice.which);
        return true;
    }

    return false;
}

bool InputRouter::handleGlobalHotkeys_(const SDL_Event& ev)
{
    // Only act on first KEYDOWN, like your current code
    if (ev.type != SDL_KEYDOWN || ev.key.repeat)
        return false;

    const SDL_Scancode sc = ev.key.keysym.scancode;
    const SDL_Keymod mods = static_cast<SDL_Keymod>(ev.key.keysym.mod);

    // F12 global monitor toggle
    if (sc == SDL_SCANCODE_F12)
    {
        if (monitorCtl_) monitorCtl_->toggle();
        return true;
    }

    // CTRL-SPACE pause
    if ((mods & KMOD_CTRL) && sc == SDL_SCANCODE_SPACE)
    {
        uiPaused_ = !uiPaused_.load();
        return true;
    }

    // CTRL+W warm reset
    if ((mods & KMOD_CTRL) && sc == SDL_SCANCODE_W)
    {
        if (warmReset_) warmReset_();
        return true;
    }

    // CTRL+SHIFT+R cold reset
    if ((mods & KMOD_CTRL) && (mods & KMOD_SHIFT) && sc == SDL_SCANCODE_R)
    {
        if (coldReset_) coldReset_();
        return true;
    }

    // ALT cassette controls (matches your current mappings)
    if ((mods & KMOD_ALT) && media_)
    {
        if (sc == SDL_SCANCODE_P) { media_->tapePlay();   return true; }
        if (sc == SDL_SCANCODE_S) { media_->tapeStop();   return true; }
        if (sc == SDL_SCANCODE_R) { media_->tapeRewind(); return true; }
        if (sc == SDL_SCANCODE_E) { media_->tapeEject();  return true; }
    }

    return false;
}
