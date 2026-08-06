// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CIA1.h"
#include "InputManager.h"
#include "Keyboard.h"
#include "MonitorController.h"

InputManager::InputManager() :
    cia1(nullptr),
    keyb(nullptr),
    monitorCtl(nullptr),
    joystick1Attached(false),
    joystick2Attached(false)
{

}

InputManager::~InputManager()
{
    if (pad1)
    {
        SDL_CloseGamepad(pad1);
        pad1 = nullptr;
    }

    if (pad2)
    {
        SDL_CloseGamepad(pad2);
        pad2 = nullptr;
    }
}

void InputManager::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("INPT");
    wrtr.writeU32(1);

    wrtr.writeU8(joystick1Attached ? 1 : 0);
    wrtr.writeU8(joystick2Attached ? 1 : 0);

    // Port 1 mapping
    wrtr.writeI32(static_cast<int32_t>(joy1Config.up));
    wrtr.writeI32(static_cast<int32_t>(joy1Config.down));
    wrtr.writeI32(static_cast<int32_t>(joy1Config.left));
    wrtr.writeI32(static_cast<int32_t>(joy1Config.right));
    wrtr.writeI32(static_cast<int32_t>(joy1Config.fire));

    // Port 2 mapping
    wrtr.writeI32(static_cast<int32_t>(joy2Config.up));
    wrtr.writeI32(static_cast<int32_t>(joy2Config.down));
    wrtr.writeI32(static_cast<int32_t>(joy2Config.left));
    wrtr.writeI32(static_cast<int32_t>(joy2Config.right));
    wrtr.writeI32(static_cast<int32_t>(joy2Config.fire));

    wrtr.endChunk();
}

bool InputManager::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "INPT", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver)) { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1) { rdr.exitChunkPayload(chunk); return false; }

    uint8_t j1 = 0, j2 = 0;
    if (!rdr.readU8(j1)) { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(j2)) { rdr.exitChunkPayload(chunk); return false; }

    int32_t tmp = 0;

    // joy1
    if (!rdr.readI32(tmp)) { rdr.exitChunkPayload(chunk); return false; }
    joy1Config.up = static_cast<SDL_Scancode>(tmp);

    if (!rdr.readI32(tmp)) { rdr.exitChunkPayload(chunk); return false; }
    joy1Config.down = static_cast<SDL_Scancode>(tmp);

    if (!rdr.readI32(tmp)) { rdr.exitChunkPayload(chunk); return false; }
    joy1Config.left = static_cast<SDL_Scancode>(tmp);

    if (!rdr.readI32(tmp)) { rdr.exitChunkPayload(chunk); return false; }
    joy1Config.right = static_cast<SDL_Scancode>(tmp);

    if (!rdr.readI32(tmp)) { rdr.exitChunkPayload(chunk); return false; }
    joy1Config.fire = static_cast<SDL_Scancode>(tmp);

    // joy2
    if (!rdr.readI32(tmp)) { rdr.exitChunkPayload(chunk); return false; }
    joy2Config.up = static_cast<SDL_Scancode>(tmp);

    if (!rdr.readI32(tmp)) { rdr.exitChunkPayload(chunk); return false; }
    joy2Config.down = static_cast<SDL_Scancode>(tmp);

    if (!rdr.readI32(tmp)) { rdr.exitChunkPayload(chunk); return false; }
    joy2Config.left = static_cast<SDL_Scancode>(tmp);

    if (!rdr.readI32(tmp)) { rdr.exitChunkPayload(chunk); return false; }
    joy2Config.right = static_cast<SDL_Scancode>(tmp);

    if (!rdr.readI32(tmp)) { rdr.exitChunkPayload(chunk); return false; }
    joy2Config.fire = static_cast<SDL_Scancode>(tmp);

    setJoystickConfig(1, joy1Config);
    setJoystickConfig(2, joy2Config);

    setJoystickAttached(1, j1 != 0);
    setJoystickAttached(2, j2 != 0);

    clearPortPad(1);
    clearPortPad(2);

    if (pad1 && joystick2Attached)
        assignPadToPort(pad1, 2);

    if (pad2 && joystick1Attached)
        assignPadToPort(pad2, 1);

    rdr.exitChunkPayload(chunk);
    return true;
}

bool InputManager::handleEvent(const SDL_Event& ev)
{
    if (monitorCtl && monitorCtl->handleEvent(ev)) return true;

    if ((ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) && !ev.key.repeat)
    {
        const SDL_Scancode sc = ev.key.scancode;
        const bool down = (ev.type == SDL_EVENT_KEY_DOWN);

        auto mods = SDL_GetModState();
        const auto* ks = SDL_GetKeyboardState(nullptr);

        if ((mods & SDL_KMOD_ALT) && (sc == SDL_SCANCODE_J || sc == SDL_SCANCODE_1 || sc == SDL_SCANCODE_2))
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
    auto drivePort = [&](int port, std::unique_ptr<Joystick>& joyPtr)
    {
        if (!joyPtr) return;

        SDL_Gamepad* pad = findPadByInstanceId(portPadId[port]);
        if (!pad) return; // no pad assigned or removed

        updateJoystickFromGamepad(pad, joyPtr.get());
    };

    if (joystick1Attached) drivePort(1, joy1);
    if (joystick2Attached) drivePort(2, joy2);
}

void InputManager::resetInputState()
{
    if (keyb)
        keyb->resetKeyboard();

    if (joy1)
        joy1->setState(0xFF);

    if (joy2)
        joy2->setState(0xFF);
}

void InputManager::setJoystickAttached(int port, bool flag)
{
    if (!cia1) return;

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
                    cia1->attachJoystickInstance(joy1.get());
                }
            }
            else
            {
                joystick1Attached = false;
                try
                {
                    if (joy1) cia1->detachJoystickInstance(joy1.get());
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
                    cia1->attachJoystickInstance(joy2.get());
                }
            }
            else
            {
                joystick2Attached = false;
                try
                {
                    if (joy2) cia1->detachJoystickInstance(joy2.get());
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

void InputManager::setJoystickConfig(int port, const JoystickMapping& cfg)
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

void InputManager::updateJoystickFromGamepad(SDL_Gamepad* pad, Joystick* joy)
{
    if (!pad || !joy) return;

    uint8_t state = 0xFF;

    bool up    = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    bool down  = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    bool left  = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    bool right = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

    int16_t lx = deadzone(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX));
    int16_t ly = deadzone(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));

    if (ly < 0) up = true;
    if (ly > 0) down = true;
    if (lx < 0) left = true;
    if (lx > 0) right = true;

    bool fire =
        SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH) ||
        SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST) ||
        SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST) ||
        SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH) ||
        SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ||
        SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);

    const int lt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    const int rt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    if (lt > 8000 || rt > 8000) fire = true;

    if (up)    state &= ~Joystick::direction::up;
    if (down)  state &= ~Joystick::direction::down;
    if (left)  state &= ~Joystick::direction::left;
    if (right) state &= ~Joystick::direction::right;
    if (fire)  state &= ~Joystick::direction::button;

    joy->setState(state);
}

SDL_JoystickID InputManager::getInstanceId(SDL_Gamepad* pad)
{
    if (!pad)
        return 0;

    return SDL_GetGamepadID(pad);
}

SDL_Gamepad* InputManager::findPadByInstanceId(SDL_JoystickID id)
{
    if (id == 0) return nullptr;
    if (pad1 && getInstanceId(pad1) == id) return pad1;
    if (pad2 && getInstanceId(pad2) == id) return pad2;
    return nullptr;
}

void InputManager::assignPadToPort(SDL_Gamepad* pad, int port)
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
            portPadId[port] = 0;
    }
}

void InputManager::clearPortPad(int port)
{
    if (port != 1 && port != 2) return;
    portPadId[port] = 0;
}

void InputManager::swapPortPads()
{
    std::swap(portPadId[1], portPadId[2]);
}

void InputManager::onGamepadAdded(SDL_JoystickID instanceId)
{
    if (!SDL_IsGamepad(instanceId))
        return;

    SDL_Gamepad* gamepad = SDL_OpenGamepad(instanceId);

    if (!gamepad)
    {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Unable to open gamepad: %s", SDL_GetError());
        return;
    }

    if (!pad1)
        pad1 = gamepad;
    else if (!pad2)
        pad2 = gamepad;
    else
    {
        SDL_CloseGamepad(gamepad);
        return;
    }

    if (gamepad == pad1 && portPadId[2] == 0)
        assignPadToPort(pad1, 2);
    else if (gamepad == pad2 && portPadId[1] == 0)
        assignPadToPort(pad2, 1);
}

void InputManager::onGamepadRemoved(SDL_JoystickID instanceId)
{
    // Unassign from ports
    unassignPadFromPorts(instanceId);

    // Close pad slot if it matches
    if (pad1 && getInstanceId(pad1) == instanceId) { SDL_CloseGamepad(pad1); pad1 = nullptr; }
    if (pad2 && getInstanceId(pad2) == instanceId) { SDL_CloseGamepad(pad2); pad2 = nullptr; }
}
