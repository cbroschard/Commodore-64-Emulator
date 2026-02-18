// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef INPUTMANAGER_H
#define INPUTMANAGER_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sdl2/sdl.h>
#include <unordered_map>
#include "CIA1.h"
#include "Common/JoystickMapping.h"
#include "Joystick.h"
#include "Keyboard.h"
#include "MonitorController.h"

class InputManager
{
    public:
        InputManager();
        virtual ~InputManager();

        inline void attachCIA1Instance(CIA1* cia1object) { this->cia1object = cia1object; }
        inline void attachKeyboardInstance(Keyboard* keyb) { this->keyb = keyb; }
        inline void attachMonitorControllerInstance(MonitorController* monitorCtl) { this->monitorCtl = monitorCtl; }

        inline Joystick* getJoy1() const { return joy1.get(); }
        inline Joystick* getJoy2() const { return joy2.get(); }
        inline bool isJoy1Attached() const { return joystick1Attached; }
        inline bool isJoy2Attached() const { return joystick2Attached; }
        inline void handleControllerDeviceAdded(int deviceIndex) { onControllerAdded(deviceIndex); }
        inline void handleControllerDeviceRemoved(SDL_JoystickID instanceId) {  onControllerRemoved(instanceId); }

        bool handleEvent(const SDL_Event& ev);
        void tick();

        void setJoystickAttached(int port, bool flag);
        void setJoystickConfig(int port, JoystickMapping& cfg);

        void assignPadToPort(SDL_GameController* pad, int port);
        void unassignPadFromPorts(SDL_JoystickID id);

        void clearPortPad(int port);
        void swapPortPads();

        SDL_GameController* getPad1() const { return pad1; }
        SDL_GameController* getPad2() const { return pad2; }

    protected:

    private:

        // Non-owning pointers
        CIA1* cia1object;
        Keyboard* keyb;
        MonitorController* monitorCtl;

        // Joystick pointers
        std::unique_ptr<Joystick> joy1;
        std::unique_ptr<Joystick> joy2;

        // Joystick state
        bool joystick1Attached;
        bool joystick2Attached;

        JoystickMapping joy1Config;
        JoystickMapping joy2Config;

        std::unordered_map<SDL_Scancode, Joystick::direction> joyMap[3];

        // Controller routing
        SDL_GameController* pad1 = nullptr;
        SDL_GameController* pad2 = nullptr;

        SDL_JoystickID portPadId[3] = { -1, -1, -1 }; // [1]=port1, [2]=port2

        void updateJoystickFromController(SDL_GameController* pad, Joystick* joy);
        SDL_JoystickID getInstanceId(SDL_GameController* pad);
        SDL_GameController* findPadByInstanceId(SDL_JoystickID id);

        // Controller hot plug handling
        void onControllerAdded(int deviceIndex);
        void onControllerRemoved(SDL_JoystickID instanceId);

        inline int16_t deadzone(int16_t v, int16_t dz = 8000) { return (std::abs((int)v) < dz) ? 0 : v; }
};

#endif // INPUTMANAGER_H
