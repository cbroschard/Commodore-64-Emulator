// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IHASIDE64STORAGE_H_INCLUDED
#define IHASIDE64STORAGE_H_INCLUDED

#include <string>

class IHasIDE64Storage
{
public:
    virtual ~IHasIDE64Storage() = default;

    virtual uint32_t getIDE64DeviceCount() const = 0;

    virtual const char* getIDE64DeviceName(uint32_t index) const = 0;
    virtual bool isIDE64DevicePresent(uint32_t index) const = 0;
    virtual bool isIDE64DeviceReadOnly(uint32_t index) const = 0;
    virtual bool isIDE64DeviceDirty(uint32_t index) const = 0;
    virtual uint32_t getIDE64DeviceSectorCount(uint32_t index) const = 0;

    virtual bool loadIDE64Image(uint32_t index, const std::string& path, bool readOnly) = 0;
    virtual bool createIDE64Image(uint32_t index, const std::string& path, uint32_t sectors) = 0;
    virtual bool saveIDE64Image(uint32_t index) = 0;
    virtual bool ejectIDE64Image(uint32_t index) = 0;
};

#endif // IHASIDE64STORAGE_H_INCLUDED
