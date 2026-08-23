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
#include <string>
#include <vector>
#include "Common/DriveTypes.h"
#include "EmulatorUI.h"
#include "Floppy/DiskFactory.h"
#include "Common/REUModel.h"
#include "StateReader.h"
#include "StateWriter.h"

// Forward declarations
class Cartridge;
class ICartridgeHost;

enum class VideoMode;

struct MachineComponents;

class MediaManager
{
public:
    MediaManager(MachineComponents& components,
                 ICartridgeHost* host,
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

        // REU
        bool     reuEnabled = false;
        REUModel reuModel   = REUModel::None;
    };

    enum class PRGLoadMode
    {
        Standalone,
        KeepCartridge
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
    void attachPRGImage(PRGLoadMode mode);
    void attachCRTImage();
    void attachT64Image();
    void attachTAPImage();
    void attachREU(REUModel model);

    // Allow user to select entry to load if T64
    bool loadSelectedT64Entry();
    void fillT64EntryViews(std::vector<EmulatorUI::T64EntryView>& out, size_t& selected) const;

    bool selectT64Entry(size_t index);
    bool queueSelectedT64Entry();

    // Blank disk creation
    void createBlankDisk(int deviceNum, DriveModel model, const std::string& path);

    // Detachments
    void detachDiskImage(int dev);
    void detachCRTImage();
    void detachREU();

    // IDE64 Specific
    bool loadIDE64Image(uint32_t deviceIndex, const std::string& path, bool readOnly);
    bool createIDE64Image(uint32_t deviceIndex, const std::string& path, uint32_t sectors);
    bool saveIDE64Image(uint32_t deviceIndex);
    bool ejectIDE64Image(uint32_t deviceIndex);

    // Load state effects
    inline bool isCartridgeAttached() const { return state_.cartAttached; }
    inline bool isTapeAttached() const { return state_.tapeAttached; }
    const Cartridge* getCartridge() const;
    Cartridge* getCartridge();
    void pressButton(uint32_t index);
    void setCartSwitch(uint32_t switchIndex, uint32_t switchPos);
    void restoreCartridgeFromState();
    void restoreTapeMountOnlyFromState();
    bool ensureDriveExists(int deviceNum, DriveModel model);

    // Cassette control
    void tapePlay();
    void tapeStop();
    void tapeRewind();
    void tapeFastForward();
    void tapeEject();

    uint64_t getTapePosition() const;

    // Command line autostart
    void applyBootAttachments();

    // Call once per frame
    void tick();

    void fillDriveStatusViews(std::vector<EmulatorUI::DriveStatusView>& out) const;

    void flushAndSaveMedia();

private:
    MachineComponents& components_;

    // System references
    ICartridgeHost*     host_;

    std::string D1541LoROM_;
    std::string D1541HiROM_;
    std::string D1571ROM_;
    std::string D1581ROM_;

    VideoMode videoMode_{};

    State state_;
    std::vector<uint8_t> prgImage_;

    bool t64LoadPending_;
    int t64LoadDelay_;

    PRGLoadMode prgLoadMode_ = PRGLoadMode::Standalone;

    std::function<void()> requestBusPrime_;
    std::function<void()> coldReset_;

    bool loadPrgImage();
    void loadPrgIntoMem();
    void recreateCartridge();

    UiCommand::DriveType toUiDriveType(DriveModel model) const;
    DiskFormat diskFormatForDriveModel(DriveModel model);
};

#endif // MEDIAMANAGER_H
