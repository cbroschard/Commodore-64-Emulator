// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef CASSETTE_H
#define CASSETTE_H

// Forward declarations
class Memory;

#include <iostream>
#include "common.h"
#include "Logging.h"
#include "Memory.h"
#include "Tape/TapeImageFactory.h"

class Cassette
{
    public:
        Cassette();
        virtual ~Cassette();

        // Pointers
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachMemoryInstance(Memory* mem) { this->mem = mem; }

        // Motor control
        inline bool motorOn() const { return motorStatus; }
        inline void stopMotor() { motorStatus = false; }
        void startMotor();

        // Tape control
        void play();
        void stop();
        void rewind();
        void fastForward();
        void record();
        void eject();

        // Tick with the emulator loop
        void tick();

        // Data access
        inline bool getData() const { return data; }
        inline void setData(bool dataLevel) { data = dataLevel; }

        // Cassette handling
        inline bool isCassetteLoaded() const { return cassetteLoaded; }
        inline bool isPlayPressed() const { return playPressed; }
        bool loadCassette(const std::string& path, VideoMode mode);
        void unloadCassette();

        bool isT64() const;
        T64LoadResult t64LoadPrgIntoMemory();

        // Monitor helpers
        std::string dumpPulses(size_t count = 10) const;
        inline void setLog(bool enable) { setLogging = enable; }


    protected:

    private:

        std::unique_ptr<TapeImage> tapeImage;

        // Non-owning pointers
        Logging* logger = nullptr;
        Memory* mem = nullptr;

        // Cassette status
        bool cassetteLoaded;
        bool playPressed;
        bool motorStatus;
        size_t tapePosition;
        uint8_t data;

        // ML Monitor logging
        bool setLogging;
};

#endif // CASSETTE_H
