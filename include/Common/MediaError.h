// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MEDIAERROR_H
#define MEDIAERROR_H

#include <string>

enum class MediaType
{
    Tape,
    Disk,
    Cartridge,
    Program,
    IDE64
};

struct MediaError
{
    MediaType type = MediaType::Tape;
    std::string path;
    std::string reason;
};

#endif // MEDIAERROR_H
