// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef RS232ENDPOINT_H_INCLUDED
#define RS232ENDPOINT_H_INCLUDED

#include <cstdint>

class RS232Endpoint
{
public:
    virtual ~RS232Endpoint() = default;

    virtual void reset() = 0;
    virtual void tick() = 0;

    virtual bool hasByte() const = 0;
    virtual bool readByte(uint8_t& value) = 0;
    virtual void writeByte(uint8_t value) = 0;
};

#endif // RS232ENDPOINT_H_INCLUDED
