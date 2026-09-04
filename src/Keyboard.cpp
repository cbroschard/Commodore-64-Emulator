// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Keyboard.h"
#include "CIA1.h"
#include "NMILine.h"

Keyboard::Keyboard() :
    nmiLine(nullptr),
    keyProcessed(false),
    shiftPressed(false)
{
    initKeyboard();
}

Keyboard::~Keyboard() = default;

void Keyboard::initKeyboard()
{
    //Initialize the keyboard so that no keys are pressed
     for(size_t i=0 ; i < 8 ; i++)
    {
        keyMatrix[i] = 0xff;
    }

    // Make the ordered pairs according to the c64 keyboard matrix layout
    // Letters
    keyMap[SDL_SCANCODE_A] = std::make_pair(1,2);
    keyMap[SDL_SCANCODE_B] = std::make_pair(3,4);
    keyMap[SDL_SCANCODE_C] = std::make_pair(2,4);
    keyMap[SDL_SCANCODE_D] = std::make_pair(2,2);
    keyMap[SDL_SCANCODE_E] = std::make_pair(1,6);
    keyMap[SDL_SCANCODE_F] = std::make_pair(2,5);
    keyMap[SDL_SCANCODE_G] = std::make_pair(3,2);
    keyMap[SDL_SCANCODE_H] = std::make_pair(3,5);
    keyMap[SDL_SCANCODE_I] = std::make_pair(4,1);
    keyMap[SDL_SCANCODE_J] = std::make_pair(4,2);
    keyMap[SDL_SCANCODE_K] = std::make_pair(4,5);
    keyMap[SDL_SCANCODE_L] = std::make_pair(5,2);
    keyMap[SDL_SCANCODE_M] = std::make_pair(4,4);
    keyMap[SDL_SCANCODE_N] = std::make_pair(4,7);
    keyMap[SDL_SCANCODE_O] = std::make_pair(4,6);
    keyMap[SDL_SCANCODE_P] = std::make_pair(5,1);
    keyMap[SDL_SCANCODE_Q] = std::make_pair(7,6);
    keyMap[SDL_SCANCODE_R] = std::make_pair(2,1);
    keyMap[SDL_SCANCODE_S] = std::make_pair(1,5);
    keyMap[SDL_SCANCODE_T] = std::make_pair(2,6);
    keyMap[SDL_SCANCODE_U] = std::make_pair(3,6);
    keyMap[SDL_SCANCODE_V] = std::make_pair(3,7);
    keyMap[SDL_SCANCODE_W] = std::make_pair(1,1);
    keyMap[SDL_SCANCODE_X] = std::make_pair(2,7);
    keyMap[SDL_SCANCODE_Y] = std::make_pair(3,1);
    keyMap[SDL_SCANCODE_Z] = std::make_pair(1,4);

    // NUMBER KEYS (0-9)
    keyMap[SDL_SCANCODE_0] = std::make_pair(4,3);
    keyMap[SDL_SCANCODE_1] = std::make_pair(7,0);
    keyMap[SDL_SCANCODE_2] = std::make_pair(7,3);
    keyMap[SDL_SCANCODE_3] = std::make_pair(1,0);
    keyMap[SDL_SCANCODE_4] = std::make_pair(1,3);
    keyMap[SDL_SCANCODE_5] = std::make_pair(2,0);
    keyMap[SDL_SCANCODE_6] = std::make_pair(2,3);
    keyMap[SDL_SCANCODE_7] = std::make_pair(3,0);
    keyMap[SDL_SCANCODE_8] = std::make_pair(3,3);
    keyMap[SDL_SCANCODE_9] = std::make_pair(4,0);

    // FUNCTION KEYS
    keyMap[SDL_SCANCODE_F1] = std::make_pair(0,4);
    keyMap[SDL_SCANCODE_F3] = std::make_pair(0,5);
    keyMap[SDL_SCANCODE_F5] = std::make_pair(0,6);
    keyMap[SDL_SCANCODE_F7] = std::make_pair(0,3);

    // CONTROL KEYS
    keyMap[SDL_SCANCODE_LSHIFT] = std::make_pair(1,7);  // LEFT SHIFT
    keyMap[SDL_SCANCODE_RSHIFT] = std::make_pair(6,4);  // RIGHT SHIFT
    keyMap[SDL_SCANCODE_GRAVE] = std::make_pair(7,1);   // LEFT ARROW
    keyMap[SDL_SCANCODE_LCTRL] = std::make_pair(7,5);    // CONTROL KEY

    // SPECIAL KEYS
    keyMap[SDL_SCANCODE_BACKSPACE] = std::make_pair(0,0);  // DELETE
    keyMap[SDL_SCANCODE_RETURN] = std::make_pair(0,1);     // RETURN
    keyMap[SDL_SCANCODE_SPACE] = std::make_pair(7,4);      // SPACE
    keyMap[SDL_SCANCODE_HOME] = std::make_pair(6,3);       // HOME KEY
    keyMap[SDL_SCANCODE_ESCAPE] = std::make_pair(7,7);     // RUN/STOP

    // PUNCTUATION & SYMBOLS
    keyMap[SDL_SCANCODE_EQUALS] = std::make_pair(6,5);      // =
    keyMap[SDL_SCANCODE_KP_PLUS] = std::make_pair(5,0);     // +
    keyMap[SDL_SCANCODE_KP_MULTIPLY] = std::make_pair(6,1); // *
    keyMap[SDL_SCANCODE_PERIOD] = std::make_pair(5,4);      // .
    keyMap[SDL_SCANCODE_MINUS] = std::make_pair(5,3);       // -
    keyMap[SDL_SCANCODE_SLASH] = std::make_pair(6,7);       // /
    keyMap[SDL_SCANCODE_COMMA] = std::make_pair(5,7);       // ,
    keyMap[SDL_SCANCODE_LEFTBRACKET] = std::make_pair(5,5); // :
    keyMap[SDL_SCANCODE_SEMICOLON] = std::make_pair(6,2);   // ;
    keyMap[SDL_SCANCODE_APOSTROPHE] = std::make_pair(5,6);  // "
    keyMap[SDL_SCANCODE_RCTRL] = std::make_pair(7,2);       // RIGHT CTRL
}

void Keyboard::processKey(SDL_Scancode scancode, bool isKeyDown)
{
    keyProcessed = false;

    auto it = keyMap.find(scancode);

    if (it == keyMap.end())
        return;

    const auto [row, col] = it->second;

    if (isKeyDown)
        keyMatrix[row] &= ~(1 << col);
    else
        keyMatrix[row] |= (1 << col);

    keyProcessed = true;
}

void Keyboard::handleKeyDown(SDL_Scancode key)
{
    // C64 RESTORE directly asserts the CPU NMI line.
    if (key == SDL_SCANCODE_PAGEUP)
    {
        if (nmiLine)
            nmiLine->raiseNMI(NMILine::RESTORE);

        return;
    }

    // C64 cursor-right key.
    if (key == SDL_SCANCODE_RIGHT)
    {
        keyMatrix[0] &= ~(1 << 2);
        return;
    }

    // C64 cursor-left is Shift + cursor-right.
    if (key == SDL_SCANCODE_LEFT)
    {
        keyMatrix[0] &= ~(1 << 2);
        keyMatrix[1] &= ~(1 << 7);
        return;
    }

    // C64 cursor-down key.
    if (key == SDL_SCANCODE_DOWN)
    {
        keyMatrix[0] &= ~(1 << 7);
        return;
    }

    // C64 cursor-up is Shift + cursor-down.
    if (key == SDL_SCANCODE_UP)
    {
        keyMatrix[0] &= ~(1 << 7);
        keyMatrix[1] &= ~(1 << 7);
        return;
    }

    // C64 even function keys are Shift + preceding odd function key.
    if (key == SDL_SCANCODE_F2)
    {
        keyMatrix[0] &= ~(1 << 4); // F1
        keyMatrix[1] &= ~(1 << 7); // Left Shift
        return;
    }

    if (key == SDL_SCANCODE_F4)
    {
        keyMatrix[0] &= ~(1 << 5); // F3
        keyMatrix[1] &= ~(1 << 7); // Left Shift
        return;
    }

    if (key == SDL_SCANCODE_F6)
    {
        keyMatrix[0] &= ~(1 << 6); // F5
        keyMatrix[1] &= ~(1 << 7); // Left Shift
        return;
    }

    if (key == SDL_SCANCODE_F8)
    {
        keyMatrix[0] &= ~(1 << 3); // F7
        keyMatrix[1] &= ~(1 << 7); // Left Shift
        return;
    }

    if (key == SDL_SCANCODE_LSHIFT || key == SDL_SCANCODE_RSHIFT)
    {
        shiftPressed = true;

        if (keyMap.find(key) != keyMap.end())
        {
            auto [row, col] = keyMap[key];
            keyMatrix[row] &= ~(1 << col);
        }

        return;
    }

    processKey(key, true);
}

void Keyboard::handleKeyUp(SDL_Scancode key)
{
    if (key == SDL_SCANCODE_PAGEUP)
    {
        if (nmiLine)
            nmiLine->clearNMI(NMILine::RESTORE);

        return;
    }

    const bool* keyboardState = SDL_GetKeyboardState(nullptr);

    if (key == SDL_SCANCODE_F2)
    {
        // F2 shares the physical C64 F1 matrix position.
        if (!keyboardState[SDL_SCANCODE_F1] &&
            !keyboardState[SDL_SCANCODE_F2])
        {
            keyMatrix[0] |= (1 << 4);
        }

        // Release virtual Shift only if nothing else still needs it.
        if (!keyboardState[SDL_SCANCODE_F4] &&
            !keyboardState[SDL_SCANCODE_F6] &&
            !keyboardState[SDL_SCANCODE_F8] &&
            !keyboardState[SDL_SCANCODE_LSHIFT] &&
            !keyboardState[SDL_SCANCODE_LEFT] &&
            !keyboardState[SDL_SCANCODE_UP])
        {
            keyMatrix[1] |= (1 << 7);
        }

        return;
    }

    if (key == SDL_SCANCODE_F4)
    {
        // F4 shares the physical C64 F3 matrix position.
        if (!keyboardState[SDL_SCANCODE_F3] &&
            !keyboardState[SDL_SCANCODE_F4])
        {
            keyMatrix[0] |= (1 << 5);
        }

        if (!keyboardState[SDL_SCANCODE_F2] &&
            !keyboardState[SDL_SCANCODE_F6] &&
            !keyboardState[SDL_SCANCODE_F8] &&
            !keyboardState[SDL_SCANCODE_LSHIFT] &&
            !keyboardState[SDL_SCANCODE_LEFT] &&
            !keyboardState[SDL_SCANCODE_UP])
        {
            keyMatrix[1] |= (1 << 7);
        }

        return;
    }

    if (key == SDL_SCANCODE_F6)
    {
        // F6 shares the physical C64 F5 matrix position.
        if (!keyboardState[SDL_SCANCODE_F5] &&
            !keyboardState[SDL_SCANCODE_F6])
        {
            keyMatrix[0] |= (1 << 6);
        }

        if (!keyboardState[SDL_SCANCODE_F2] &&
            !keyboardState[SDL_SCANCODE_F4] &&
            !keyboardState[SDL_SCANCODE_F8] &&
            !keyboardState[SDL_SCANCODE_LSHIFT] &&
            !keyboardState[SDL_SCANCODE_LEFT] &&
            !keyboardState[SDL_SCANCODE_UP])
        {
            keyMatrix[1] |= (1 << 7);
        }

        return;
    }

    if (key == SDL_SCANCODE_F8)
    {
        // F8 shares the physical C64 F7 matrix position.
        if (!keyboardState[SDL_SCANCODE_F7] &&
            !keyboardState[SDL_SCANCODE_F8])
        {
            keyMatrix[0] |= (1 << 3);
        }

        if (!keyboardState[SDL_SCANCODE_F2] &&
            !keyboardState[SDL_SCANCODE_F4] &&
            !keyboardState[SDL_SCANCODE_F6] &&
            !keyboardState[SDL_SCANCODE_LSHIFT] &&
            !keyboardState[SDL_SCANCODE_LEFT] &&
            !keyboardState[SDL_SCANCODE_UP])
        {
            keyMatrix[1] |= (1 << 7);
        }

        return;
    }

    if (key == SDL_SCANCODE_RIGHT || key == SDL_SCANCODE_LEFT)
    {
        // Release the C64 horizontal cursor key only when neither
        // host horizontal-arrow key remains held.
        if (!keyboardState[SDL_SCANCODE_RIGHT] && !keyboardState[SDL_SCANCODE_LEFT])
           keyMatrix[0] |= (1 << 2);

        // Release the virtual left shift only when no shifted cursor
        // key or physical left shift remains held.
        if (!keyboardState[SDL_SCANCODE_LEFT] && !keyboardState[SDL_SCANCODE_UP] && !keyboardState[SDL_SCANCODE_LSHIFT])
            keyMatrix[1] |= (1 << 7);

        return;
    }

    if (key == SDL_SCANCODE_DOWN ||
        key == SDL_SCANCODE_UP)
    {
        // Release the C64 vertical cursor key only when neither
        // host vertical-arrow key remains held.
        if (!keyboardState[SDL_SCANCODE_DOWN] && !keyboardState[SDL_SCANCODE_UP])
            keyMatrix[0] |= (1 << 7);

        if (!keyboardState[SDL_SCANCODE_LEFT] && !keyboardState[SDL_SCANCODE_UP] && !keyboardState[SDL_SCANCODE_LSHIFT])
            keyMatrix[1] |= (1 << 7);

        return;
    }

    if (key == SDL_SCANCODE_LSHIFT || key == SDL_SCANCODE_RSHIFT)
    {
        if (keyMap.find(key) != keyMap.end())
        {
            auto [row, col] = keyMap[key];
            keyMatrix[row] |= (1 << col);
        }

        const bool* keyboardState = SDL_GetKeyboardState(nullptr);

        shiftPressed = keyboardState[SDL_SCANCODE_LSHIFT] || keyboardState[SDL_SCANCODE_RSHIFT];

        return;
    }

    processKey(key, false);
}

uint8_t Keyboard::readRow(uint8_t rowIndex) const
{
    return rowIndex < 8 ? keyMatrix[rowIndex] : 0xFF;
}

void Keyboard::resetKeyboard()
{
    for (size_t i = 0; i < 8; ++i)
        keyMatrix[i] = 0xFF;

    shiftPressed = false;

    if (nmiLine)
        nmiLine->clearNMI(NMILine::RESTORE);
}
