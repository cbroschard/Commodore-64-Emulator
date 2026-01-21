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
        int         prgDelay    = 140;
        std::string prgPath;
    };

public:
    MediaManager(std::unique_ptr<Cartridge>& cartSlot,
                 std::unique_ptr<D1541>& drive8Slot,
                 IECBUS& bus,
                 Memory& mem,
                 PLA& pla,
                 CPU& cpu,
                 Vic& vic,
                 TraceManager& traceMgr,
                 Cassette& cass,
                 Logging& logger,
                 std::string d1541LoROM,
                 std::string d1541HiROM,
                 std::function<void()> coldResetCallback);

    ~MediaManager() = default;

    const State& getState() const { return state_; }

    void setVideoMode(VideoMode mode) { videoMode_ = mode; }

    // Optional setters if you want UI to set paths separately:
    void setDiskPath(const std::string& p) { state_.diskPath = p; }
    void setPrgPath(const std::string& p)  { state_.prgPath  = p; }
    void setCartPath(const std::string& p) { state_.cartPath = p; }
    void setTapePath(const std::string& p) { state_.tapePath = p; }

    // Attach operations (these mirror your Computer methods)
    void attachD64Image();
    void attachPRGImage();
    void attachCRTImage();
    void attachT64Image();
    void attachTAPImage();

    // Call once per frame
    void tick();

private:
    bool loadPrgImage();
    void loadPrgIntoMem();
    void recreateCartridge();

private:
    // Slots we need to (re)create
    std::unique_ptr<Cartridge>& cart_;
    std::unique_ptr<D1541>&     drive8_;

    // System references
    IECBUS&       bus_;
    Memory&       mem_;
    PLA&          pla_;
    CPU&          cpu_;
    Vic&          vic_;
    TraceManager& traceMgr_;
    Cassette&     cass_;
    Logging&      logger_;

    // Own ROM paths (copy = safe)
    std::string d1541LoROM_;
    std::string d1541HiROM_;

    VideoMode videoMode_{}; // you will set this from Computer

    State state_;
    std::vector<uint8_t> prgImage_;

    std::function<void()> coldReset_;
};

#endif // MEDIAMANAGER_H
