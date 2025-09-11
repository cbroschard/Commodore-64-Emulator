// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Tape/TapeImageFactory.h"

std::unique_ptr<TapeImage> createTapeImage(const std::string& filePath)
{
    if (filePath.size() >= 4 && filePath.substr(filePath.size() - 4) == ".tap")
    {
        return std::make_unique<TAP>();
    }
    else if (filePath.size() >= 4 && filePath.substr(filePath.size() - 4) == ".t64")
    {
        return std::make_unique<T64>();
    }
    return nullptr;
}
