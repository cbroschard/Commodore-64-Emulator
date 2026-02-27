// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MEDIAMANAGER_H
#define MEDIAMANAGER_H

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "Common/DriveTypes.h"
#include "StateReader.h"
#include "StateWriter.h"

// Forward declarations
class Cartridge;
class Cassette;
class CPU;
class D1541;
class D1571;
class D1581;
class Drive;
class IECBUS;
class Logging;
class Memory;
class MLMonitorBackend;
class PLA;
class TraceManager;
class Vic;

enum class VideoMode;

class MediaManager
{
public:
    MediaManager(std::unique_ptr<Cartridge>& cartSlot,
                 std::array<std::unique_ptr<Drive>, 16>& driveSlots,
                 IECBUS& bus,
                 Memory& mem,
                 PLA& pla,
                 CPU& cpu,
                 Vic& vic,
                 MLMonitorBackend& monbackend,
                 TraceManager& traceMgr,
                 Cassette& cass,
                 Logging& logger,
                 std::string D1541LoROM,
                 std::string D1541HiROM,
                 std::string D1571ROM,
                 std::string D1581ROM,
                 std::function<void()> requestBusPrimeCallback,
                 std::function<void()> coldResetCallback);

    ~MediaManager() = default;

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

    // State management
    void saveState(StateWriter& wrtr) const;
    bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

    const State& getState() const { return state_; }

    void setVideoMode(VideoMode mode) { videoMode_ = mode; }

    // Helpers
    std::string lowerExt(const std::string& path);
    bool isExtCompatible(DriveModel model, const std::string& ext);

    // Setters
    void setCartAttached(bool b) { state_.cartAttached = b; }
    void setTapeAttached(bool b) { state_.tapeAttached = b; }
    void setPrgAttached(bool b)  { state_.prgAttached  = b; }
    void setD1541LoROM(const std::string& p) { D1541LoROM_ = p; }
    void setD1541HiROM(const std::string& p) { D1541HiROM_ = p; }
    void setD1571ROM(const std::string& p) { D1571ROM_ = p; }
    void setD1581ROM(const std::string& p) { D1581ROM_ = p; }
    void setDiskPath(const std::string& p) { state_.diskPath = p; }
    void setPrgPath(const std::string& p)  { state_.prgPath  = p; }
    void setCartPath(const std::string& p) { state_.cartPath = p; }
    void setTapePath(const std::string& p) { state_.tapePath = p; }

    // Attachments
    void attachDiskImage(int deviceNum, DriveModel model, const std::string& path);
    void attachPRGImage();
    void attachCRTImage();
    void attachT64Image();
    void attachTAPImage();

    // Load state effects
    inline const Cartridge* getCartridge() const { return cart_.get(); }
    inline bool isCartridgeAttached() const { return state_.cartAttached; }
    inline bool isTapeAttached() const { return state_.tapeAttached; }
    bool canFreeze() const;
    void pressFreeze();
    void restoreCartridgeFromState();
    void restoreTapeMountOnlyFromState();

    // Cassette control
    void tapePlay();
    void tapeStop();
    void tapeRewind();
    void tapeEject();

    // Command line autostart
    void applyBootAttachments();

    // Call once per frame
    void tick();

private:

    std::unique_ptr<Cartridge>& cart_;
    std::array<std::unique_ptr<Drive>, 16>& drives_;

    // System references
    IECBUS&             bus_;
    Memory&             mem_;
    PLA&                pla_;
    CPU&                cpu_;
    Vic&                vic_;
    MLMonitorBackend&   monbackend_;
    TraceManager&       traceMgr_;
    Cassette&           cass_;
    Logging&            logger_;

    std::string D1541LoROM_;
    std::string D1541HiROM_;
    std::string D1571ROM_;
    std::string D1581ROM_;

    VideoMode videoMode_{};

    State state_;
    std::vector<uint8_t> prgImage_;

    std::function<void()> requestBusPrime_;
    std::function<void()> coldReset_;

    bool loadPrgImage();
    void loadPrgIntoMem();
    void recreateCartridge();
};

#endif // MEDIAMANAGER_H
