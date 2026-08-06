// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef INPUTMANAGER_H
#define INPUTMANAGER_H

class CIA1;
class Keyboard;
class MonitorController;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <SDL3/SDL.h>
#include <unordered_map>
#include "Joystick.h"
#include "Common/JoystickMapping.h"
#include "StateReader.h"
#include "StateWriter.h"

class InputManager
{
    public:
        InputManager();
        virtual ~InputManager();

        inline void attachCIA1Instance(CIA1* cia1) { this->cia1 = cia1; }
        inline void attachKeyboardInstance(Keyboard* keyb) { this->keyb = keyb; }
        inline void attachMonitorControllerInstance(MonitorController* monitorCtl) { this->monitorCtl = monitorCtl; }

        inline Joystick* getJoy1() const { return joy1.get(); }
        inline Joystick* getJoy2() const { return joy2.get(); }
        inline bool isJoy1Attached() const { return joystick1Attached; }
        inline bool isJoy2Attached() const { return joystick2Attached; }
        inline void handleGamepadDeviceAdded(SDL_JoystickID instanceId) { onGamepadAdded(instanceId); }
        inline void handleGamepadDeviceRemoved(SDL_JoystickID instanceId) {  onGamepadRemoved(instanceId); }

        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        bool handleEvent(const SDL_Event& ev);
        void tick();

        void resetInputState();

        void setJoystickAttached(int port, bool flag);
        void setJoystickConfig(int port, const JoystickMapping& cfg);

        void assignPadToPort(SDL_Gamepad* pad, int port);
        void unassignPadFromPorts(SDL_JoystickID id);

        void clearPortPad(int port);
        void swapPortPads();

        SDL_Gamepad* getPad1() const { return pad1; }
        SDL_Gamepad* getPad2() const { return pad2; }

    protected:

    private:
        // Non-owning pointers
        CIA1* cia1;
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

        // Gamepad routing
        SDL_Gamepad* pad1 = nullptr;
        SDL_Gamepad* pad2 = nullptr;

        SDL_JoystickID portPadId[3] = { 0, 0, 0 }; // [1]=port1, [2]=port2

        void updateJoystickFromGamepad(SDL_Gamepad* pad, Joystick* joy);
        SDL_JoystickID getInstanceId(SDL_Gamepad* pad);
        SDL_Gamepad* findPadByInstanceId(SDL_JoystickID id);

        // Gamepad hot plug handling
        void onGamepadAdded(SDL_JoystickID instanceId);
        void onGamepadRemoved(SDL_JoystickID instanceId);

        inline int16_t deadzone(int16_t v, int16_t dz = 8000) { return (std::abs((int)v) < dz) ? 0 : v; }
};

#endif // INPUTMANAGER_H
