// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef PERIPHERAL_H
#define PERIPHERAL_H

// Forward declarations
class Drive;

#include <cstdint>
#include <vector>
#include <string>
#include <iostream>

class Peripheral
{
    public:
        Peripheral();
        virtual ~Peripheral();

        // reset function
        virtual void reset() = 0;

        // Getters
        int getDeviceNumber() const { return deviceNumber; }

        // Helper methods for derived classes to control bus lines
        inline void setDeviceNumber(int num) { deviceNumber = num; }

        // ML Monitor
        virtual bool isDrive() const { return false; }
        virtual Drive* asDrive() { return nullptr; }
        virtual const std::string& getLoadedDiskName() const = 0;
        virtual const char* getDriveTypeName() const noexcept = 0;

    protected:
        int deviceNumber;
};

#endif // IECPERIPHERAL_H
