// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/FunPlayMapper.h"

FunPlayMapper::FunPlayMapper() = default;

FunPlayMapper::~FunPlayMapper() = default;

uint8_t FunPlayMapper::read(uint16_t address)
{
    // Open Bus
    return 0xFF;
}

void FunPlayMapper::write(uint16_t address, uint8_t value)
{
    if (address == 0xDE00)
        switch(value)
        {
            case 0x00:
                loadIntoMemory(0);
                break;
            case 0x08:
                loadIntoMemory(1);
                break;
            case 0x10:
                loadIntoMemory(2);
                break;
            case 0x18:
                loadIntoMemory(3);
                break;
            case 0x20:
                loadIntoMemory(4);
                break;
            case 0x28:
                loadIntoMemory(5);
                break;
            case 0x30:
                loadIntoMemory(6);
                break;
            case 0x38:
                loadIntoMemory(7);
                break;
            case 0x01:
                loadIntoMemory(8);
                break;
            case 0x09:
                loadIntoMemory(9);
                break;
            case 0x11:
                loadIntoMemory(10);
                break;
            case 0x19:
                loadIntoMemory(11);
                break;
            case 0x21:
                loadIntoMemory(12);
                break;
            case 0x29:
                loadIntoMemory(13);
                break;
            case 0x31:
                loadIntoMemory(14);
                break;
            case 0x39:
                loadIntoMemory(15);
                break;
            default:
                break;
        }
}

bool FunPlayMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    // Clear LO
    cart->clearCartridge(cartLocation::LO);

        // Load the selected 8K bank into $8000
    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber == bank)
        {
            size_t size = std::min(section.data.size(), static_cast<size_t>(0x2000));
            for (size_t i = 0; i < size; ++i)
                mem->writeCartridge(i, section.data[i], cartLocation::LO);
            return true;
        }
    }
    return false;
}
