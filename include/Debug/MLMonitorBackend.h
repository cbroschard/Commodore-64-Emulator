// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MLMONITORBACKEND_H
#define MLMONITORBACKEND_H

#include "Computer.h"

class MLMonitorBackend
{
    public:
        MLMonitorBackend();
        virtual ~MLMonitorBackend();

        // Pointer functions
        inline void attachCartridgeInstance(Cartridge* cart) { this->cart = cart; }
        inline void attachCassetteInstance(Cassette* cass) { this->cass = cass; }
        inline void attachCIA1Instance(CIA1* cia1object) { this->cia1object = cia1object; }
        inline void attachCIA2Instance(CIA2* cai2object) { this->cia2object = cia2object; }
        inline void attachProcessorInstance(CPU* processor) { this->processor = processor; }
        inline void attachIECBusInstance(IECBUS* bus) { this->bus = bus; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachPLAInstance(PLA* pla) { this->pla = pla; }
        inline void attachSIDInstance(SID* sidobject) { this->sidobject = sidobject; }
        inline void attachVICInstace(Vic* vicII) { this->vicII = vicII; }

    protected:

    private:

        // Non-owning pointers
        Cartridge* cart = nullptr;
        Cassette* cass = nullptr;
        CIA1* cia1object = nullptr;
        CIA2* cia2object = nullptr;
        CPU* processor = nullptr;
        IECBUS* bus = nullptr;
        Logging* logger = nullptr;
        PLA* pla = nullptr;
        SID* sidobject = nullptr;
        Vic* vicII = nullptr;

};

#endif // MLMONITORBACKEND_H
