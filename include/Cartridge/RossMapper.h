// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef ROSSMAPPER_H
#define ROSSMAPPER_H

#include "CartridgeMapper.h"

class RossMapper : public CartridgeMapper
{
    public:
        RossMapper();
        virtual ~RossMapper();

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

    protected:

    private:
};

#endif // ROSSMAPPER_H
