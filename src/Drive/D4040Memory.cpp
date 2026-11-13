// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D4040Memory.h"

D4040Memory::D4040Memory() :
    parentPeripheral(nullptr)
{
    D4040RAM.resize(RAM_SIZE);
    D4040ROM1.resize(ROM_SIZE, 0);
    D4040ROM2.resize(ROM_SIZE, 0);
    D4040ROM3.resize(ROM_SIZE, 0);
}

D4040Memory::~D4040Memory() = default;

bool D4040Memory::initialize(const std::string& D4040LoROM, const std::string& D4040HiROM1, const std::string& D4040HiROM2)
{
    // Initialize Drive RAM to 0's
    std::fill(D4040RAM.begin(), D4040RAM.end(), 0x00);

    // Initialize both ROMS to 0's before loading
    std::fill(D4040ROM1.begin(), D4040ROM1.end(), 0x00);
    std::fill(D4040ROM2.begin(), D4040ROM2.end(), 0x00);
    std::fill(D4040ROM3.begin(), D4040ROM3.end(), 0x00);

    // Try to load in ROM files passed in via config
    if (!loadROM(D4040LoROM, D4040ROM1, ROM_SIZE, "4040 ROM Lo") || !loadROM(D4040HiROM1, D4040ROM2, ROM_SIZE, "4040 ROM Hi1")
        || !loadROM(D4040HiROM2, D4040ROM3, ROM_SIZE, "4040 ROM Hi2"));
    {
        return false;
    }

    return true;
}


bool D4040Memory::loadROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    std::streamsize fileSize = file.tellg();
    if (static_cast<size_t>(fileSize) != expectedSize)
        return false;

    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(targetBuffer.data()), expectedSize))
        return false;

    file.close();
    return true;
}
