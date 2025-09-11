// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef TAPEIMAGE_H
#define TAPEIMAGE_H

#include <cstring>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Logging.h"

class TapeImage
{
    public:
        TapeImage();
        virtual ~TapeImage();
        virtual bool loadTape(const std::string& filePath) = 0;
        virtual void simulateLoading() = 0;
        virtual bool currentBit() const = 0;
        virtual bool isT64() const;

    protected:

    private:
        virtual bool loadFile(const std::string& path, std::vector<uint8_t>& buffer) = 0;
        virtual bool validateHeader() = 0;
};

#endif // TAPEIMAGE_H
