// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cassette.h"
#include "CIA1.h"
#include "CIA2.h"
#include "CPU.h"
#include "CPUTiming.h"
#include "Drive/Drive.h"
#include "IECBUS.h"
#include "InputManager.h"
#include "Logging.h"
#include "MediaManager.h"
#include "Memory.h"
#include "PLA.h"
#include "SID/SID.h"
#include "StateManager.h"
#include "Vic.h"

StateManager::StateManager(Cartridge& cart,
                     Cassette& cass,
                     CIA1& cia1object,
                     CIA2& cia2object,
                     CPU& processor,
                     IECBUS& bus,
                     InputManager& inputMgr,
                     Logging& logger,
                     MediaManager& media,
                     Memory& mem,
                     PLA& pla,
                     SID& sidchip,
                     Vic& vicII,
                     std::atomic<bool>& uiPaused,
                     VideoMode& videoMode,
                     const CPUConfig*& cpuCfg,
                     bool& pendingBusPrime,
                     bool& busPrimedAfterBoot,
                     std::array<std::unique_ptr<Drive>, 16>& drives) :
      cart_(cart),
      cass_(cass),
      cia1object_(cia1object),
      cia2object_(cia2object),
      processor_(processor),
      bus_(bus),
      inputMgr_(inputMgr),
      logger_(logger),
      media_(media),
      mem_(mem),
      pla_(pla),
      sidchip_(sidchip),
      vicII_(vicII),
      uiPaused_(uiPaused),
      videoMode_(videoMode),
      cpuCfg_(cpuCfg),
      pendingBusPrime_(pendingBusPrime),
      busPrimedAfterBoot_(busPrimedAfterBoot),
      drives_(drives)
{

}

StateManager::~StateManager() = default;

bool StateManager::save(const std::string& path)
{
        // Initialize writer
    StateWriter wrtr(kStateVersion);
    wrtr.beginFile();

    // SYS0 = Core system config
    wrtr.beginChunk("SYS0");

    // Dump SYS0 schema version
    wrtr.writeU32(1);

    // Dump Video mode
    wrtr.writeU8(static_cast<uint8_t>(videoMode_));

    // Dump CPU timing ID
    const uint8_t cpuTimingId = (videoMode_ == VideoMode::NTSC) ? 0 : 1;
    wrtr.writeU8(cpuTimingId);

    // Dump UI pause state
    wrtr.writeBool(uiPaused_.load());

    // Dump Bus priming flags
    wrtr.writeBool(pendingBusPrime_);
    wrtr.writeBool(busPrimedAfterBoot_);

    // Dump Drive config
    wrtr.writeU8(16);
    for (int i = 0; i < 16; ++i)
    {
        const bool present = (drives_[i] != nullptr);
        wrtr.writeBool(present);

        if (present)
        {
            wrtr.writeU8(static_cast<uint8_t>(drives_[i]->getDriveModel()));
            wrtr.writeU8(static_cast<uint8_t>(drives_[i]->getDeviceNumber()));
        }
    }

    wrtr.endChunk(); // end SYS0

    // -------------------------
    // Device chunks (next)
    // -------------------------
    processor_.saveState(wrtr);
    cia1object_.saveState(wrtr);
    cia2object_.saveState(wrtr);
    vicII_.saveState(wrtr);
    sidchip_.saveState(wrtr);
    pla_.saveState(wrtr);
    mem_.saveState(wrtr);
    bus_.saveState(wrtr);

    // Save joystick state
    inputMgr_.saveState(wrtr);

    // Save media state
    media_.saveState(wrtr);

    // Save cartridge state if attached
    if (media_.getState().cartAttached) cart_.saveState(wrtr);

    // Save Cassette and tape state if attached
    if (media_.getState().tapeAttached) cass_.saveState(wrtr);

    // Write file
    return wrtr.writeToFile(path);
}

bool StateManager::load(const std::string& path)
{
    StateReader rdr;

    // Try to read given file
    const bool loaded   = rdr.loadFromFile(path);
    const bool validate = rdr.readFileHeader();

    // Fail if we can't load or validate the file
    if (!loaded || !validate)
    {
        #ifdef Debug
        std::cout << "Unable to load .sav file!\n";
        #endif
        return false;
    }

    // Process the first chunk
    StateReader::Chunk chunk;
    if (!rdr.nextChunk(chunk))
        return false;

    if (std::memcmp(chunk.tag, "SYS0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t sysVer = 0;
    if (!rdr.readU32(sysVer)) return false;
    if (sysVer != 1) return false;

    // Restore Video Mode
    uint8_t mode = 0;
    if (!rdr.readU8(mode)) return false;
    videoMode_ = static_cast<VideoMode>(mode);

    // Restore CPU timing
    uint8_t cpuTimingID = 0;
    if (!rdr.readU8(cpuTimingID)) return false;
    cpuCfg_ = cpuTimingID ? &PAL_CPU : &NTSC_CPU;

    // Restore uiPaused
    bool tmpPaused = false;
    if (!rdr.readBool(tmpPaused)) return false;
    uiPaused_.store(tmpPaused);

    // Restore bus pending status
    if (!rdr.readBool(pendingBusPrime_)) return false;
    if (!rdr.readBool(busPrimedAfterBoot_)) return false;

    // Restore / consume drive config from SYS0
    uint8_t driveCount = 0;
    if (!rdr.readU8(driveCount)) return false;

    // Clamp to our fixed array size just in case
    const uint8_t maxDrives = (driveCount > 16) ? 16 : driveCount;

    for (uint8_t i = 0; i < maxDrives; ++i)
    {
        bool present = false;
        if (!rdr.readBool(present)) return false;

        if (present)
        {
            uint8_t model = 0;
            uint8_t deviceNumber = 0;
            if (!rdr.readU8(model)) return false;
            if (!rdr.readU8(deviceNumber)) return false;

            // TODO:
            // If you later want SYS0 to recreate drive instances/configuration,
            // use 'model' and 'deviceNumber' here.
            //
            // For now we intentionally just consume the bytes so the reader
            // stays aligned with the saved format.
            (void)model;
            (void)deviceNumber;
        }
    }

    // If a future file somehow stores more than 16 drives, consume the extras too
    for (uint8_t i = maxDrives; i < driveCount; ++i)
    {
        bool present = false;
        if (!rdr.readBool(present)) return false;

        if (present)
        {
            uint8_t model = 0;
            uint8_t deviceNumber = 0;
            if (!rdr.readU8(model)) return false;
            if (!rdr.readU8(deviceNumber)) return false;
        }
    }

    rdr.exitChunkPayload(chunk);

    while (rdr.nextChunk(chunk))
    {
        const bool isCPU = (std::memcmp(chunk.tag, "CPU0", 4) == 0) ||
                           (std::memcmp(chunk.tag, "CPUX", 4) == 0);

        const bool isCIA1 = (std::memcmp(chunk.tag, "CIA1", 4) == 0) ||
                            (std::memcmp(chunk.tag, "CI1X", 4) == 0);

        const bool isCIA2 = (std::memcmp(chunk.tag, "CIA2", 4) == 0) ||
                            (std::memcmp(chunk.tag, "CI2X", 4) == 0);

        const bool isVIC = (std::memcmp(chunk.tag, "VIC0", 4) == 0) ||
                           (std::memcmp(chunk.tag, "VICX", 4) == 0);

        const bool isSID = (std::memcmp(chunk.tag, "SID0", 4) == 0) ||
                           (std::memcmp(chunk.tag, "SIDX", 4) == 0);

        #ifdef Debug
        std::cout << "CHUNK: "
                  << char(chunk.tag[0]) << char(chunk.tag[1])
                  << char(chunk.tag[2]) << char(chunk.tag[3])
                  << "\n";
        #endif

        if (isCPU)
        {
            if (!processor_.loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded processor\n";
            #endif
        }
        else if (isCIA1)
        {
            if (!cia1object_.loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded CIA1\n";
            #endif
        }
        else if (isCIA2)
        {
            if (!cia2object_.loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded CIA2\n";
            #endif
        }
        else if (isVIC)
        {
            if (!vicII_.loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded VIC\n";
            #endif
        }
        else if (isSID)
        {
            if (!sidchip_.loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded SID\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "PLA0", 4) == 0)
        {
            if (!pla_.loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded PLA\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "MEM0", 4) == 0)
        {
            if (!mem_.loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded memory\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "IEC0", 4) == 0)
        {
            if (!bus_.loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded IECBUS\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "INPT", 4) == 0)
        {
            if (!inputMgr_.loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded Input\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "MED0", 4) == 0)
        {
            if (!media_.loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded Media Manager\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "CART", 4) == 0)
        {
            mem_.setCartridgeAttached(true);
            pla_.setCartridgeAttached(true);

            if (!cart_.loadState(chunk, rdr)) return false;

            #ifdef Debug
            std::cout << "Loaded Cartridge\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "CASS", 4) == 0)
        {
            if (media_.isTapeAttached())
                media_.restoreTapeMountOnlyFromState();

            if (!cass_.loadState(chunk, rdr)) return false;

            #ifdef Debug
            std::cout << "Loaded Cassette\n";
            #endif
        }
        else
        {
            rdr.skipChunk(chunk);
        }
    }

    return true;
}
