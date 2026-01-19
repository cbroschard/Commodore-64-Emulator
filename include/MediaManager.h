// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MEDIAMANAGER_H
#define MEDIAMANAGER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
class Cartridge;
class Cassette;
class CPU;
class D1541;
class IECBUS;
class Logging;
class Memory;
class PLA;
class TraceManager;
class Vic;

enum class VideoMode;

class MediaManager
{
    public:
        MediaManager(const std::string& d1541LoROM,
                           const std::string& d1541HiROM,
                           std::function<void()> coldResetCallback);
        virtual ~MediaManager();

        struct State
        {
            // Disk
            bool        diskAttached = false;
            std::string diskPath;

            // Cartridge
            bool        cartAttached = false;
            std::string cartPath;

            // Tape
            bool        tapeAttached = false;
            std::string tapePath;

            // PRG
            bool        prgAttached = false;
            bool        prgLoaded   = false;
            int         prgDelay    = 0;
            std::string prgPath;
        };

        // state
        const State& getState() const { return state_; }
        void setVideoMode(VideoMode mode) { videoMode_ = mode; }

        // attach operations
        void attachDisk(const std::string& path);
        void attachPRG(const std::string& path);
        void attachCRT(const std::string& path);
        void attachT64(const std::string& path);
        void attachTAP(const std::string& path);

        void tick();

    protected:

    private:

        const std::string& d1541LoROM_;
        const std::string& d1541HiROM_;

        VideoMode videoMode_;

        State state_;
        std::vector<uint8_t> prgImage_;

        bool loadPrgImage();
        void loadPrgIntoMem();
        void recreateCartridge();

        std::function<void()> coldReset_;
};

#endif // MEDIAMANAGER_H
