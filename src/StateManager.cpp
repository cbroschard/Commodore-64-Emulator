// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CPUTiming.h"
#include "Drive/Drive.h"
#include "MachineComponents.h"
#include "MachineRuntimeState.h"
#include "StateManager.h"

StateManager::StateManager(MachineComponents& components, MachineRuntimeState& runtime) :
      components_(components),
      runtime_(runtime)
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
    wrtr.writeU8(static_cast<uint8_t>(runtime_.videoMode));

    // Dump SID model
    wrtr.writeU8(static_cast<uint8_t>(runtime_.sidModel));

    // Dump CPU timing ID
    const uint8_t cpuTimingId = (runtime_.videoMode == VideoMode::NTSC) ? 0 : 1;
    wrtr.writeU8(cpuTimingId);

    // Dump UI pause state
    wrtr.writeBool(runtime_.uiPaused.load());

    // Dump Bus priming flags
    wrtr.writeBool(runtime_.pendingBusPrime);
    wrtr.writeBool(runtime_.busPrimedAfterBoot);

    // Dump Drive config
    wrtr.writeU8(16);
    for (int i = 0; i < 16; ++i)
    {
        const bool present = (components_.drives[i] != nullptr);
        wrtr.writeBool(present);

        if (present)
        {
            wrtr.writeU8(static_cast<uint8_t>(components_.drives[i]->getDriveModel()));
            wrtr.writeU8(static_cast<uint8_t>(components_.drives[i]->getDeviceNumber()));
        }
    }

    wrtr.endChunk(); // end SYS0

    // -------------------------
    // Device chunks (next)
    // -------------------------
    components_.cpu->saveState(wrtr);
    components_.cpu6510Port->saveState(wrtr);
    components_.cia1->saveState(wrtr);
    components_.cia2->saveState(wrtr);
    components_.dataBus->saveState(wrtr);
    components_.vic->saveState(wrtr);
    components_.sid->saveState(wrtr);
    components_.mem->saveState(wrtr);
    components_.iecBus->saveState(wrtr);

    components_.userPortRS232Adapter->saveState(wrtr);
    components_.rs232Device->saveState(wrtr);

    // Save all installed disk drives
    for (const auto& drive : components_.drives)
    {
        if (drive)
            drive->saveState(wrtr);
    }

    // Save joystick state
    components_.inputMgr->saveState(wrtr);

    // Save media state
    components_.media->saveState(wrtr);

    // Save cartridge state if attached
    if (components_.media->getState().cartAttached) components_.cart->saveState(wrtr);

    // Save Cassette and tape state if attached
    if (components_.media->getState().tapeAttached) components_.cass->saveState(wrtr);

    // Save REU state if attached
    if (components_.media->getState().reuEnabled) components_.reu->saveState(wrtr);

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
    runtime_.videoMode = static_cast<VideoMode>(mode);

    // Restore SID Model
    uint8_t model = 0;
    if (!rdr.readU8(model)) return false;
    runtime_.sidModel = static_cast<SIDModel>(model);

    // Restore CPU timing
    uint8_t cpuTimingID = 0;
    if (!rdr.readU8(cpuTimingID)) return false;
    runtime_.cpuCfg = cpuTimingID ? &PAL_CPU : &NTSC_CPU;

    // Restore uiPaused
    bool tmpPaused = false;
    if (!rdr.readBool(tmpPaused)) return false;
    runtime_.uiPaused.store(tmpPaused);

    // Restore bus pending status
    if (!rdr.readBool(runtime_.pendingBusPrime)) return false;
    if (!rdr.readBool(runtime_.busPrimedAfterBoot)) return false;

    // Restore drive config from SYS0
    uint8_t driveCount = 0;
    if (!rdr.readU8(driveCount)) return false;

    // Remove the current drive configuration before recreating the saved one
    for (int dev = 8; dev <= 11; ++dev)
    {
        if (!components_.drives[dev])
            continue;

        components_.iecBus->unregisterDevice(dev);
        components_.drives[dev].reset();
    }

    // Clamp to our fixed array size just in case
    const uint8_t maxDrives = (driveCount > 16) ? 16 : driveCount;

    for (uint8_t i = 0; i < maxDrives; ++i)
    {
        bool present = false;
        if (!rdr.readBool(present)) return false;

        if (present)
        {
            uint8_t modelU8 = 0;
            uint8_t deviceNumber = 0;

            if (!rdr.readU8(modelU8))                           return false;
            if (!rdr.readU8(deviceNumber))                      return false;

            if (deviceNumber >= components_.drives.size())      return false;

            const DriveModel driveModel =
                static_cast<DriveModel>(modelU8);

            if (driveModel == DriveModel::None)                 return false;

            if (!components_.media->ensureDriveExists
                (
                    static_cast<int>(deviceNumber),
                    driveModel
                ))
            {
                return false;
            }
        }
    }

    // Consume any unsupported records beyond our 16 drive slots
    for (uint8_t i = maxDrives; i < driveCount; ++i)
    {
        bool present = false;
        if (!rdr.readBool(present)) return false;

        if (present)
        {
            uint8_t modelU8 = 0;
            uint8_t deviceNumber = 0;

            if (!rdr.readU8(modelU8))                           return false;
            if (!rdr.readU8(deviceNumber))                      return false;
        }
    }

    rdr.exitChunkPayload(chunk);

    // Track which reconstructed drive slots have already consumed a state chunk.
    // This allows multiple drives of the same model to restore correctly.
    std::array<bool, 16> driveStateRestored{};
    driveStateRestored.fill(false);

    auto restoreDriveState =
        [&](DriveModel expectedModel,
            const StateReader::Chunk& driveChunk) -> bool
    {
        for (size_t i = 0; i < components_.drives.size(); ++i)
        {
            if (driveStateRestored[i])
                continue;

            if (!components_.drives[i])
                continue;

            if (components_.drives[i]->getDriveModel() != expectedModel)
                continue;

            if (!components_.drives[i]->loadState(driveChunk, rdr))
                return false;

            driveStateRestored[i] = true;
            return true;
        }

        return false;
    };

    while (rdr.nextChunk(chunk))
    {
        const bool isCPU = (std::memcmp(chunk.tag, "CPU0", 4) == 0) ||
                           (std::memcmp(chunk.tag, "CPUX", 4) == 0);

        const bool isCPUPort = (std::memcmp(chunk.tag, "CPUP", 4) == 0);

        const bool isCIA1 = (std::memcmp(chunk.tag, "CIA1", 4) == 0) ||
                            (std::memcmp(chunk.tag, "CI1X", 4) == 0);

        const bool isCIA2 = (std::memcmp(chunk.tag, "CIA2", 4) == 0) ||
                            (std::memcmp(chunk.tag, "CI2X", 4) == 0);

        const bool isDataBus = std::memcmp(chunk.tag, "OBUS", 4) == 0;

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
            if (!components_.cpu->loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded processor\n";
            #endif
        }
        else if (isCPUPort)
        {
            if (!components_.cpu6510Port->loadState(chunk, rdr))
                return false;

            #ifdef Debug
            std::cout << "Loaded CPU 6510 port\n";
            #endif
        }
        else if (isCIA1)
        {
            if (!components_.cia1->loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded CIA1\n";
            #endif
        }
        else if (isCIA2)
        {
            if (!components_.cia2->loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded CIA2\n";
            #endif
        }
        else if (isDataBus)
        {
            if (!components_.dataBus->loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded DataBusLatch\n";
            #endif
        }
        else if (isVIC)
        {
            if (!components_.vic->loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded VIC\n";
            #endif
        }
        else if (isSID)
        {
            if (!components_.sid->loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded SID\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "MEM0", 4) == 0)
        {
            if (!components_.mem->loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded memory\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "IEC0", 4) == 0)
        {
            if (!components_.iecBus->loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded IECBUS\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "INPT", 4) == 0)
        {
            if (!components_.inputMgr->loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded Input\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "MED0", 4) == 0)
        {
            if (!components_.media->loadState(chunk, rdr)) return false;
            #ifdef Debug
            std::cout << "Loaded Media Manager\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "CART", 4) == 0)
        {
            components_.bus->setCartridgeAttached(true);
            components_.pla->setCartridgeAttached(true);

            if (!components_.cart->loadState(chunk, rdr)) return false;

            #ifdef Debug
            std::cout << "Loaded Cartridge\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "CASS", 4) == 0)
        {
            if (components_.media->isTapeAttached())
                components_.media->restoreTapeMountOnlyFromState();

            if (!components_.cass->loadState(chunk, rdr)) return false;

            #ifdef Debug
            std::cout << "Loaded Cassette\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "REU0", 4) == 0)
        {
            if (!components_.reu->loadState(chunk, rdr)) return false;

            #ifdef Debug
            std::cout << "Loaded REU\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "D541", 4) == 0)
        {
            if (!restoreDriveState(DriveModel::D1541, chunk))
                return false;

            #ifdef Debug
            std::cout << "Loaded 1541 drive\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "D157", 4) == 0)
        {
            if (!restoreDriveState(DriveModel::D1571, chunk))
                return false;

            #ifdef Debug
            std::cout << "Loaded 1571 drive\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "D158", 4) == 0)
        {
            if (!restoreDriveState(DriveModel::D1581, chunk))
                return false;

            #ifdef Debug
            std::cout << "Loaded 1581 drive\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "UR23", 4) == 0)
        {
            if (!components_.userPortRS232Adapter->loadState(chunk, rdr))
                return false;

            #ifdef Debug
            std::cout << "Loaded User Port RS232 Adapter\n";
            #endif
        }
        else if (std::memcmp(chunk.tag, "RS23", 4) == 0)
        {
            if (!components_.rs232Device->loadState(chunk, rdr))
                return false;

            #ifdef Debug
            std::cout << "Loaded RS232 Device\n";
            #endif
        }
        else
        {
            rdr.skipChunk(chunk);
        }
    }

    components_.cpu->postLoadState();
    components_.userPortRS232Adapter->postLoadState();

    return true;
}
