// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef KEYBOARD_H
#define KEYBOARD_H

#define SDL_MAIN_HANDLED

#include <sdl2/sdl.h>
#include <unordered_map>
#include <vector>
#include <bitset>
#include <iostream>
#include "CIA1.h"
#include "Logging.h"

class Keyboard
{
    public:
        Keyboard();
        virtual ~Keyboard();

        // Define the 8x8 matrix to represent the keyboard (8 rows, 8 columns)
        uint8_t keyMatrix[8];

        // Map SDL keys to matrix positions (row, column)
        std::unordered_map<SDL_Scancode,std::pair<int,int>> keyMap;
        std::unordered_map<char,std::vector<SDL_Scancode>> charMap;


        uint8_t readRow(uint8_t rowIndex);

        // temp debugging
        void simulateKeyPress(uint8_t row, uint8_t col);

        // Pointers
        inline void attachLogInstance(Logging* logger) { this->logger=logger; }

        void resetKeyboard();
        void handleKeyDown(SDL_Scancode key);
        void handleKeyUp(SDL_Scancode key);

        // ML Monitor logging
        inline void setLog(bool enable) { setLogging = enable; }

    protected:

    private:

        // non-owning pointers
        Logging* logger;

        // ML Monitor logging
        bool setLogging;

        void initKeyboard();
        void processKey(SDL_Keycode keycode, SDL_Scancode scancode, bool isKeyDown); // Helper for Key up and Key down methods
        bool keyProcessed; // Track whether or not there's something to do in key up/key down
        bool shiftPressed; // Handle shift + key combinations

        char getShiftVariant(SDL_Keycode keycode);
};

#endif // KEYBOARD_H
