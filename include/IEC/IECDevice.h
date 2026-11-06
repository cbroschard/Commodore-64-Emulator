// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IECDEVICE_H
#define IECDEVICE_H

#include <cstdint>

class IECBus;

class IECDevice
{
    friend class IECBus;

    public:
        IECDevice();
        virtual ~IECDevice();

        IECDevice(const IECDevice&) = delete;
        IECDevice& operator=(const IECDevice&) = delete;

        void attachIECBusInstance(IECBus* iecBus);
        void detachIECBusInstance();

        // Incoming IEC signal callbacks
        virtual void atnChanged(bool asserted) = 0;
        virtual void clkChanged(bool asserted) = 0;
        virtual void dataChanged(bool asserted) = 0;

        // IEC protocol commands
        virtual void onListen() = 0;
        virtual void onUnListen() = 0;
        virtual void onTalk() = 0;
        virtual void onUnTalk() = 0;
        virtual void onSecondaryAddress(uint8_t sa) = 0;

        virtual bool isSRQAsserted() const = 0;
        virtual void setSRQAsserted(bool state) = 0;

        int getIECDeviceNumber() const { return iecDeviceNumber; }

    protected:
        IECBus* iecBus;

        bool assertClk;
        bool assertData;
        bool assertAtn;
        bool assertSrq;

        bool listening;
        bool talking;

        uint8_t shiftReg;
        int bitsProcessed;

        virtual uint8_t nextOutputByte();

        void peripheralAssertClk(bool state);
        void peripheralAssertData(bool state);
        void peripheralAssertAtn(bool state);
        void peripheralAssertSrq(bool state);

    private:
        int iecDeviceNumber;
};

#endif // IECDEVICE_H
