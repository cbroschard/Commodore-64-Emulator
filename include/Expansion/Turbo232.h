// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef TURBO232_H
#define TURBO232_H

#include <cstdint>
#include "Serial/MOS6551.h"
#include "Serial/RS232Device.h"

class NMILine;
class RS232Endpoint;

class Turbo232
{
    public:
        Turbo232(uint16_t baseAddress);
        virtual ~Turbo232();

        inline void attachNMILineInstance(NMILine* nmiLine) { this->nmiLine = nmiLine; }
        void attachEndpoint(RS232Endpoint* endpoint);
        void detachEndpoint();

        void reset();
        void tick(uint32_t cycles);

        uint8_t read(uint16_t address);
        void write(uint16_t address, uint8_t value);

        bool handlesAddress(uint16_t address) const;

        // ML Monitor
        std::string dumpDebugOutput(const std::string& subCommand) const;
        std::string dumpDebugGeneral() const;
        std::string dumpDebugACIA() const;
        std::string dumpDebugRS232() const;

    private:
        RS232Device serial;
        MOS6551 acia;
        NMILine* nmiLine;

        uint16_t baseAddress;

        uint8_t enhancedSpeedRegister;

        uint8_t readEnhancedSpeedRegister() const;
        void writeEnhancedSpeedRegister(uint8_t value);
        uint32_t decodeEnhancedBaud() const;
        bool enhancedModeEnabled() const;
        void updateBaudRate();
};

#endif // TURBO232_H
