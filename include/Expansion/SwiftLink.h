// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef SWIFTLINK_H
#define SWIFTLINK_H

#include <cstdint>
#include "Serial/MOS6551.h"
#include "Serial/RS232Device.h"

class NMILine;
class RS232Endpoint;

class SwiftLink
{
    public:
        explicit SwiftLink(uint16_t baseAddress = 0xDE00);
        virtual ~SwiftLink();

        inline void attachNMILineInstance(NMILine* nmiLine) { this->nmiLine = nmiLine; }
        void attachEndpoint(RS232Endpoint* endpoint);
        void detachEndpoint();

        inline bool hasEndpoint() const { return acia.hasEndpoint(); }

        void reset();
        void tick(uint32_t cycles);

        uint8_t read(uint16_t address);
        void write(uint16_t address, uint8_t value);

        // Getters
        bool getIRQ() const;

        RS232Device& getSerial();
        MOS6551& getACIA();

        // Helpers
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
};

#endif // SWIFTLINK_H
