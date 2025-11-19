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
class IECBUS;

#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include "IECTypes.h"

class Peripheral
{
    public:
        Peripheral();
        virtual ~Peripheral();

        // Pointer attachment
        void attachBusInstance(IECBUS* bus);
        void detachBusInstance();

        // Signal handlers
        virtual void atnChanged(bool atnAsserted) = 0;
        virtual void clkChanged(bool clkState)  = 0;
        virtual void dataChanged(bool dataState) = 0;

        // reset function
        virtual void reset() = 0;

        // Getters
        int getDeviceNumber() const { return deviceNumber; }
        virtual bool isSRQAsserted() const = 0;

        // Helper methods for derived classes to control bus lines
        inline void setDeviceNumber(int num) { deviceNumber = num; }
        virtual void setSRQAsserted(bool state) = 0;

        // Connection to IECBUS
        void peripheralAssertClk(bool state);
        void peripheralAssertData(bool state);
        void peripheralAssertAtn(bool state);
        void peripheralAssertSrq(bool state);

        virtual void iecClkEdge(bool data, bool clk) = 0;

        // IEC BUS commands
        void onListen();
        inline void onUnListen() { listening = false; }
        void onTalk();
        inline void onUnTalk() { talking = false;}

        // ML Monitor
        virtual bool isDrive() const { return false; }
        virtual const std::string& getLoadedDiskName() const = 0;
        virtual const char* getDriveTypeName() const noexcept = 0;

        // Non-owning pointer
        IECBUS* bus;

    protected:

        int deviceNumber;

        // Assert status
        bool assertClk;
        bool assertData;
        bool assertAtn;
        bool assertSrq;

        bool listening;
        bool talking;
        uint8_t shiftReg;
        int bitsProcessed;

        virtual uint8_t nextOutputByte();

    private:

};

#endif // IECPERIPHERAL_H
