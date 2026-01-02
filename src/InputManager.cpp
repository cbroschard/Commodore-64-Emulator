// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "InputManager.h"

InputManager::InputManager() :
    joystick1Attached(false),
    joystick2Attached(false)
{

}

InputManager::~InputManager() = default;

bool InputManager::handleEvent(const SDL_Event& ev)
{
    if (monitorCtl && monitorCtl->handleEvent(ev)) return true;

    if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && !ev.key.repeat)
    {
        auto sc   = ev.key.keysym.scancode;
        bool down = (ev.type == SDL_KEYDOWN);

        auto mods = SDL_GetModState();
        const auto* ks = SDL_GetKeyboardState(nullptr);

        if ((mods & KMOD_ALT) && (sc == SDL_SCANCODE_J || sc == SDL_SCANCODE_1 || sc == SDL_SCANCODE_2))
        {
            if (down && ks[SDL_SCANCODE_J])  // only on key-down of 1 or 2
            {
                if (sc == SDL_SCANCODE_1)
                {
                    setJoystickAttached(1, !joystick1Attached);
                }
                else if (sc == SDL_SCANCODE_2)
                {
                    setJoystickAttached(2, !joystick2Attached);
                }
            }
            return true; // never pass J,1,2 through to the C-64
        }

        for (int port = 1; port <= 2; ++port)
        {
            auto& joyPtr = (port == 1 ? joy1 : joy2);
            if (!joyPtr) continue;

            // If a controller is assigned to this port, DO NOT consume joystick key mappings.
            // Let the key fall through to the C64 keyboard instead.
            if (findPadByInstanceId(portPadId[port]) != nullptr)
                continue;

            auto it = joyMap[port].find(sc);
            if (it != joyMap[port].end())
            {
                uint8_t state = joyPtr->getState();
                if (down) state &= ~it->second;
                else      state |=  it->second;
                joyPtr->setState(state);
                return true;
            }
        }

        if (keyb)
        {
            if (down) keyb->handleKeyDown(sc);
            else      keyb->handleKeyUp(sc);
        }
        return true;
    }

    return false;
}

void InputManager::tick()
{
    SDL_GameControllerUpdate();

    auto drivePort = [&](int port, std::unique_ptr<Joystick>& joyPtr)
    {
        if (!joyPtr) return;

        SDL_GameController* pad = findPadByInstanceId(portPadId[port]);
        if (!pad) return; // no pad assigned or removed

        updateJoystickFromController(pad, joyPtr.get());
    };

    if (joystick1Attached) drivePort(1, joy1);
    if (joystick2Attached) drivePort(2, joy2);
}

void InputManager::setJoystickAttached(int port, bool flag)
{
    if (!cia1object) return;

    switch (port)
    {
        case 1:
        {
            if (flag)
            {
                joystick1Attached = true;
                if (!joy1)
                {
                    joy1 = std::make_unique<Joystick>(1);
                    cia1object->attachJoystickInstance(joy1.get());
                }
            }
            else
            {
                joystick1Attached = false;
                try
                {
                    if (joy1) cia1object->detachJoystickInstance(joy1.get());
                }
                catch (const std::runtime_error& e)
                {
                    std::cout << "Caught exception: " << e.what() << "\n";
                }
                catch (...)
                {
                    std::cout << "Caught unknown Joystick exception!\n";
                }
                joy1.reset();
            }
            break;
        }
        case 2:
        {
            if (flag)
            {
                joystick2Attached = true;
                if (!joy2)
                {
                    joy2 = std::make_unique<Joystick>(2);
                    cia1object->attachJoystickInstance(joy2.get());
                }
            }
            else
            {
                joystick2Attached = false;
                try
                {
                    if (joy2) cia1object->detachJoystickInstance(joy2.get());
                }
                catch (const std::runtime_error& e)
                {
                        std::cout << "Caught exception: " << e.what() << "\n";
                }
                catch (...)
                {
                    std::cout << "Caught unknown Joystick exception!\n";
                }
                joy2.reset();
            }
            break;
        }
        default:
            break;
    }
}

void InputManager::setJoystickConfig(int port, JoystickMapping& cfg)
{
    if (port != 1 && port != 2) return;

    joyMap[port].clear();
    joyMap[port] = {
        { cfg.up,    Joystick::direction::up },
        { cfg.down,  Joystick::direction::down },
        { cfg.left,  Joystick::direction::left },
        { cfg.right, Joystick::direction::right },
        { cfg.fire,  Joystick::direction::button }
    };

    if (port == 1) joy1Config = cfg;
    else           joy2Config = cfg;
}

void InputManager::updateJoystickFromController(SDL_GameController* pad, Joystick* joy)
{
    if (!pad || !joy) return;

    uint8_t state = 0xFF;

    bool up    = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
    bool down  = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    bool left  = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    bool right = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

    int16_t lx = deadzone(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
    int16_t ly = deadzone(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));

    if (ly < 0) up = true;
    if (ly > 0) down = true;
    if (lx < 0) left = true;
    if (lx > 0) right = true;

    bool fire =
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) ||
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B) ||
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X) ||
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y) ||
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ||
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);

    const int lt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    const int rt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    if (lt > 8000 || rt > 8000) fire = true;

    if (up)    state &= ~Joystick::direction::up;
    if (down)  state &= ~Joystick::direction::down;
    if (left)  state &= ~Joystick::direction::left;
    if (right) state &= ~Joystick::direction::right;
    if (fire)  state &= ~Joystick::direction::button;

    joy->setState(state);
}

SDL_JoystickID InputManager::getInstanceId(SDL_GameController* pad)
{
    if (!pad) return -1;
    SDL_Joystick* j = SDL_GameControllerGetJoystick(pad);
    return SDL_JoystickInstanceID(j);
}

SDL_GameController* InputManager::findPadByInstanceId(SDL_JoystickID id)
{
    if (id < 0) return nullptr;
    if (pad1 && getInstanceId(pad1) == id) return pad1;
    if (pad2 && getInstanceId(pad2) == id) return pad2;
    return nullptr;
}

void InputManager::assignPadToPort(SDL_GameController* pad, int port)
{
    if (!pad) return;
    if (port != 1 && port != 2) return;

    setJoystickAttached(port, true);

    portPadId[port] = getInstanceId(pad);
}

void InputManager::unassignPadFromPorts(SDL_JoystickID id)
{
    for (int port = 1; port <= 2; ++port)
    {
        if (portPadId[port] == id)
            portPadId[port] = -1;
    }
}

void InputManager::clearPortPad(int port)
{
    if (port != 1 && port != 2) return;
    portPadId[port] = -1;
}

void InputManager::swapPortPads()
{
    std::swap(portPadId[1], portPadId[2]);
}

void InputManager::onControllerAdded(int deviceIndex)
{
    if (!SDL_IsGameController(deviceIndex)) return;

    SDL_GameController* c = SDL_GameControllerOpen(deviceIndex);
    if (!c) return;

    if (!pad1) pad1 = c;
    else if (!pad2) pad2 = c;
    else { SDL_GameControllerClose(c); return; }

    if (c == pad1 && portPadId[2] == -1) assignPadToPort(pad1, 2);
    else if (c == pad2 && portPadId[1] == -1) assignPadToPort(pad2, 1);
}

void InputManager::onControllerRemoved(SDL_JoystickID instanceId)
{
    // Unassign from ports
    unassignPadFromPorts(instanceId);

    // Close pad slot if it matches
    if (pad1 && getInstanceId(pad1) == instanceId) { SDL_GameControllerClose(pad1); pad1 = nullptr; }
    if (pad2 && getInstanceId(pad2) == instanceId) { SDL_GameControllerClose(pad2); pad2 = nullptr; }
}
