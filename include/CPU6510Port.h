// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef CPU6510PORT_H
#define CPU6510PORT_H

#include "StateReader.h"
#include "StateWriter.h"

class Cassette;
class PLA;

class CPU6510Port
{
    public:
        CPU6510Port();
        virtual ~CPU6510Port();

        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        uint8_t readDDR() const;
        uint8_t readPort() const;

        void writeDDR(uint8_t value);
        void writePort(uint8_t value);

        void setCassetteSenseLow(bool low);

        uint8_t getEffectivePort() const;

        void attachPLAInstance(PLA* pla) { this->pla = pla; }
        void attachCassetteInstance(Cassette* cass) { this->cass = cass; }

        // Cassette API
        inline bool getCassetteSenseLow() const { return cassetteSenseLow; }
        inline bool isCassetteMotorOn() const { return (getEffectivePort() & 0x20) == 0; }

    private:
        // Non-owning pointers
        Cassette* cass;
        PLA* pla;

        uint8_t dataDirectionRegister;
        uint8_t outputLatch;

        bool cassetteSenseLow;

        uint8_t computeEffectivePort() const;
        void applySideEffects();
};

#endif // CPU6510PORT_H
