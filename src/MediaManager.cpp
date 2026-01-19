// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "MediaManager.h"

MediaManager::MediaManager(const std::string& d1541LoROM,
                           const std::string& d1541HiROM,
                           std::function<void()> coldResetCallback) :
    d1541LoROM_(d1541LoROM),
    d1541HiROM_(d1541HiROM)
{

}

MediaManager::~MediaManager() = default;
