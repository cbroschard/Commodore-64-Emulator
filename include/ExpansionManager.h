// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef EXPANSIONMANAGER_H
#define EXPANSIONMANAGER_H

class Computer;

class ExpansionManager
{
    public:
        ExpansionManager(Computer& host);
        virtual ~ExpansionManager();

        // User Port virtual modem
        void attachVirtualModem();
        void detachVirtualModem();

        bool isVirtualModemAttached() const;
        bool isVirtualModemOnline() const;

        void setRS232Baud(uint32_t baud);
        uint32_t getRS232Baud() const;

        // SwiftLink
        void enableSwiftLink(uint16_t baseAddress);
        void disableSwiftLink();

        void enableSwiftLink();
        bool isSwiftLinkEnabled() const;

        void setSwiftLinkBaseAddress(uint16_t address);
        uint16_t getSwiftLinkBaseAddress() const;

        void attachSwiftLinkVirtualModem();
        void detachSwiftLinkVirtualModem();

        bool isSwiftLinkVirtualModemAttached() const;
        bool isSwiftLinkVirtualModemOnline() const;

        // Turbo232
        void enableTurbo232(uint16_t baseAddress);
        void disableTurbo232();

        void enableTurbo232();
        bool isTurbo232Enabled() const;

        void setTurbo232BaseAddress(uint16_t address);
        uint16_t getTurbo232BaseAddress() const;

        void attachTurbo232VirtualModem();
        void detachTurbo232VirtualModem();

        bool isTurbo232VirtualModemAttached() const;
        bool isTurbo232VirtualModemOnline() const;

    private:
        Computer& host_;
};

#endif // EXPANSIONMANAGER_H
