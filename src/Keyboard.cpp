// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Keyboard.h"

Keyboard::Keyboard() :
    logger(nullptr),
    setLogging(false),
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

    // Map characters to SDL key map
    charMap['A'] = {SDL_SCANCODE_A};
    charMap['B'] = {SDL_SCANCODE_B};
    charMap['C'] = {SDL_SCANCODE_C};
    charMap['D'] = {SDL_SCANCODE_D};
    charMap['E'] = {SDL_SCANCODE_E};
    charMap['F'] = {SDL_SCANCODE_F};
    charMap['G'] = {SDL_SCANCODE_G};
    charMap['H'] = {SDL_SCANCODE_H};
    charMap['I'] = {SDL_SCANCODE_I};
    charMap['J'] = {SDL_SCANCODE_J};
    charMap['K'] = {SDL_SCANCODE_K};
    charMap['L'] = {SDL_SCANCODE_L};
    charMap['M'] = {SDL_SCANCODE_M};
    charMap['N'] = {SDL_SCANCODE_N};
    charMap['O'] = {SDL_SCANCODE_O};
    charMap['P'] = {SDL_SCANCODE_P};
    charMap['Q'] = {SDL_SCANCODE_Q};
    charMap['R'] = {SDL_SCANCODE_R};
    charMap['S'] = {SDL_SCANCODE_S};
    charMap['T'] = {SDL_SCANCODE_T};
    charMap['U'] = {SDL_SCANCODE_U};
    charMap['V'] = {SDL_SCANCODE_V};
    charMap['W'] = {SDL_SCANCODE_W};
    charMap['X'] = {SDL_SCANCODE_X};
    charMap['Y'] = {SDL_SCANCODE_Y};
    charMap['Z'] = {SDL_SCANCODE_Z};
    charMap['0'] = {SDL_SCANCODE_0};
    charMap['1'] = {SDL_SCANCODE_1};
    charMap['2'] = {SDL_SCANCODE_2};
    charMap['3'] = {SDL_SCANCODE_3};
    charMap['4'] = {SDL_SCANCODE_4};
    charMap['5'] = {SDL_SCANCODE_5};
    charMap['6'] = {SDL_SCANCODE_6};
    charMap['7'] = {SDL_SCANCODE_7};
    charMap['8'] = {SDL_SCANCODE_8};
    charMap['9'] = {SDL_SCANCODE_9};
    charMap['\t'] = {SDL_SCANCODE_TAB};
    charMap['\b'] = {SDL_SCANCODE_BACKSPACE};
    charMap['\n'] = {SDL_SCANCODE_RETURN};
    charMap[' '] = {SDL_SCANCODE_SPACE};
    charMap['='] = {SDL_SCANCODE_EQUALS};
    charMap['.'] = {SDL_SCANCODE_PERIOD};
    charMap['-'] = {SDL_SCANCODE_MINUS};
    charMap['/'] = {SDL_SCANCODE_SLASH};
    charMap[','] = {SDL_SCANCODE_COMMA};
    charMap[';'] = {SDL_SCANCODE_SEMICOLON};
    charMap['\\'] = {SDL_SCANCODE_BACKSLASH};
    charMap['\''] = {SDL_SCANCODE_APOSTROPHE};
    charMap['+'] = {SDL_SCANCODE_KP_PLUS};
    charMap['*'] = {SDL_SCANCODE_KP_MULTIPLY};
    charMap[':'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_SEMICOLON};
    charMap['_'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_MINUS};
    charMap['~'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_GRAVE};
    charMap['<']  = {SDL_SCANCODE_LSHIFT,SDL_SCANCODE_COMMA};
    charMap['>']  = {SDL_SCANCODE_LSHIFT,SDL_SCANCODE_PERIOD};
    charMap['?'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_SLASH};
    charMap['!'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_1};
    charMap['@'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_2};
    charMap['#'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_3};
    charMap['$'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_4};
    charMap['%'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_5};
    charMap['^'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_6};
    charMap['&'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_7};
    charMap['('] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_9};
    charMap[')'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_0};
    charMap['{'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LEFTBRACKET};
    charMap['}'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RIGHTBRACKET};
    charMap['|'] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_BACKSLASH};

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

    // ARROW & CONTROL KEYS
    keyMap[SDL_SCANCODE_UP] = std::make_pair(6,6);      // UP ARROW
    keyMap[SDL_SCANCODE_LEFT] = std::make_pair(7,2);    // LEFT ARROW
    keyMap[SDL_SCANCODE_RIGHT] = std::make_pair(7,6);   // RIGHT ARROW
    keyMap[SDL_SCANCODE_LSHIFT] = std::make_pair(1,7);  // LEFT SHIFT
    keyMap[SDL_SCANCODE_RSHIFT] = std::make_pair(6,4);  // RIGHT SHIFT
    keyMap[SDL_SCANCODE_GRAVE] = std::make_pair(7,1);   // LEFT ARROW
    keyMap[SDL_SCANCODE_LCTRL] = std::make_pair(7,5);    // CONTROL KEY

    // SPECIAL KEYS
    keyMap[SDL_SCANCODE_BACKSPACE] = std::make_pair(0,0);  // DELETE
    keyMap[SDL_SCANCODE_RETURN] = std::make_pair(0,1);     // RETURN
    keyMap[SDL_SCANCODE_TAB] = std::make_pair(0,2);        // TAB
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

void Keyboard::processKey(SDL_Keycode keycode, SDL_Scancode scancode, bool isKeyDown)
{
    keyProcessed = false;
    if (logger && setLogging)
    {
        logger->WriteLog("processKey called: keycode = " + std::to_string(keycode) +
                         ", scancode = " + std::to_string(scancode) +
                         ", isKeyDown = " + (isKeyDown ? "true" : "false") +
                         ", shiftPressed = " + (shiftPressed ? "true" : "false"));
    }
    if (shiftPressed)
    {
        // Get the shifted character variant.
        SDL_Keycode shiftedKey = getShiftVariant(keycode);
        // Try to find the shifted mapping in the charMap.
        if (charMap.find(shiftedKey) != charMap.end())
        {
            for (auto mappedScancode : charMap[shiftedKey])
            {
                if (keyMap.find(mappedScancode) != keyMap.end())
                {
                    auto [row, col] = keyMap[mappedScancode];
                    if (isKeyDown)
                    {
                        keyMatrix[row] &= ~(1 << col);
                    }
                    else
                    {
                        keyMatrix[row] |= (1 << col);
                    }
                    keyProcessed = true;
                }
            }
        }
        else if (keyMap.find(scancode) != keyMap.end())
        {
            // Fallback: if no shifted mapping exists, process as a normal key.
            auto [row, col] = keyMap[scancode];
            if (isKeyDown)
            {
                keyMatrix[row] &= ~(1 << col);
            }
            else
            {
                keyMatrix[row] |= (1 << col);
            }
            keyProcessed = true;
        }
    }
    else if (keyMap.find(scancode) != keyMap.end())
    {
        // Normal key processing if shift is not pressed.
        auto [row, col] = keyMap[scancode];
        if (isKeyDown)
        {
            keyMatrix[row] &= ~(1 << col);
        }
        else
        {
            keyMatrix[row] |= (1 << col);
        }
        keyProcessed = true;
    }
}

void Keyboard::handleKeyDown(SDL_Scancode key)
{
    if (logger && setLogging)
    {
        logger->WriteLog("Key Down Event: SDL Scancode = " + std::to_string(key));
    }

    // Convert the scancode to a keycode so that modifiers (like Shift) are taken into account
    SDL_Keycode keycode = SDL_GetKeyFromScancode(key);

    if (key == SDL_SCANCODE_LSHIFT || key == SDL_SCANCODE_RSHIFT)
    {
        shiftPressed = true;
        if (logger && setLogging)
        {
            logger->WriteLog("Shift Key Pressed");
        }
        // Mark Shift key as pressed in the key matrix
        //if (keyMap.find(SDL_SCANCODE_LSHIFT) != keyMap.end())
        if (keyMap.find(key) != keyMap.end())
        {
            //auto [row, col] = keyMap[SDL_SCANCODE_LSHIFT];
            auto [row, col] = keyMap[key];
            keyMatrix[row] &= ~(1 << col); // Press Shift
        }
        return;
    }
    processKey(keycode, key, true);
}

void Keyboard::handleKeyUp(SDL_Scancode key)
{

    if (logger && setLogging)
    {
        logger->WriteLog("Key Up Event: SDL Scancode = " + std::to_string(key));
    }

    // Convert the scancode to a keycode so that modifiers (like Shift) are taken into account
    SDL_Keycode keycode = SDL_GetKeyFromScancode(key);

    if (key == SDL_SCANCODE_LSHIFT || key == SDL_SCANCODE_RSHIFT)
    {
        shiftPressed = false;
        // Mark Shift key as released in the key matrix
        //if (keyMap.find(SDL_SCANCODE_LSHIFT) != keyMap.end())
        if (keyMap.find(key) != keyMap.end())
        {
            //auto [row, col] = keyMap[SDL_SCANCODE_LSHIFT];
            auto [row, col] = keyMap[key];
            keyMatrix[row] |= (1 << col); // Release Shift
        }
        if (logger && setLogging)
        {
            logger->WriteLog("Shift Key Released");
        }
        return;
    }
    processKey(keycode, key, false);
}

uint8_t Keyboard::readRow(uint8_t rowIndex)
{
    if (rowIndex < 8)
    {
        uint8_t state = keyMatrix[rowIndex];
        if (logger && setLogging)
        {
            logger->WriteLog("Keyboard readRow: rowIndex = " + std::to_string(rowIndex) +
                 ", keyMatrix[rowIndex] = " + std::to_string(keyMatrix[rowIndex]));
        }
        return state;
    }
    else
    {
        return 0xFF; // default no key pressed
    }
}

void Keyboard::simulateKeyPress(uint8_t row, uint8_t col)
{
    if (logger && setLogging)
    {
        logger->WriteLog("Before simulateKeyPress: keyMatrix[" + std::to_string(row) + "] = " + std::to_string(keyMatrix[row]));
    }
    keyMatrix[row] &= ~(1 << col); // Simulate key press by clearing the bit
    if (logger && setLogging)
    {
        logger->WriteLog("After simulateKeyPress: keyMatrix[" + std::to_string(row) + "] = " + std::to_string(keyMatrix[row]));
    }
}

void Keyboard::resetKeyboard()
{
    for (size_t i = 0; i < 8; i++)
    {
        keyMatrix[i] = 0xFF; // Reset all keys to unpressed
    }
    shiftPressed = false;
    if (logger && setLogging)
    {
        logger->WriteLog("Keyboard state reset.");
    }
}

char Keyboard::getShiftVariant(SDL_Keycode keycode)
{
    switch(keycode)
    {
        case ';': return ':';
        case '=': return '+';
        case '-': return '_';
        case '/': return '?';
        case ',': return '<';
        case '.': return '>';
        case '\'': return '"';
        default: return keycode;
    }
}
