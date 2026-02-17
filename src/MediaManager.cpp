// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "MediaManager.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "Cartridge.h"
#include "cassette.h"
#include "CPU.h"
#include "Drive/D1541.h"
#include "Drive/D1571.h"
#include "Drive/D1581.h"
#include "Drive/Drive.h"
#include "IECBUS.h"
#include "Logging.h"
#include "Memory.h"
#include "Debug/MLMonitorBackend.h"
#include "PLA.h"
#include "Debug/TraceManager.h"
#include "Vic.h"

MediaManager::MediaManager(std::unique_ptr<Cartridge>& cartSlot,
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
                           std::function<void()> coldResetCallback)
    : cart_(cartSlot),
      drives_(driveSlots),
      bus_(bus),
      mem_(mem),
      pla_(pla),
      cpu_(cpu),
      vic_(vic),
      monbackend_(monbackend),
      traceMgr_(traceMgr),
      cass_(cass),
      logger_(logger),
      D1541LoROM_(std::move(D1541LoROM)),
      D1541HiROM_(std::move(D1541HiROM)),
      D1571ROM_(std::move(D1571ROM)),
      D1581ROM_(std::move(D1581ROM)),
      requestBusPrime_(std::move(requestBusPrimeCallback)),
      coldReset_(std::move(coldResetCallback))
{
    state_.prgDelay = 140;
}

void MediaManager::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("MED0");

    // chunk version
    wrtr.writeU32(1);

    // ---- Cartridge ----
    wrtr.writeBool(state_.cartAttached);
    wrtr.writeString(state_.cartPath);

    // Dump PRG
    wrtr.writeBool(state_.prgAttached);
    wrtr.writeString(state_.prgPath);
    wrtr.writeU32(static_cast<uint32_t>(state_.prgDelay));
    wrtr.writeBool(state_.prgLoaded);

    const bool hasPrgImage = !prgImage_.empty();
    wrtr.writeBool(hasPrgImage);
    if (hasPrgImage)
        wrtr.writeVectorU8(prgImage_);

    // Dump Tape
    wrtr.writeBool(state_.tapeAttached);
    wrtr.writeString(state_.tapePath);

    // Dump Drive mount table (8..11)
    constexpr uint8_t kFirstDev = 8;
    constexpr uint8_t kLastDev  = 11;

    wrtr.writeU8(kFirstDev);
    wrtr.writeU8(kLastDev);

    for (uint8_t dev = kFirstDev; dev <= kLastDev; ++dev)
    {
        const bool present = (drives_[dev] != nullptr);
        wrtr.writeBool(present);

        uint8_t modelId = 0;
        bool hasDisk = false;
        std::string diskPath;

        if (present)
        {
            modelId = static_cast<uint8_t>(drives_[dev]->getDriveModel());
            hasDisk = drives_[dev]->isDiskLoaded();
            diskPath = hasDisk ? drives_[dev]->getCurrentDiskPath() : std::string{};
        }

        wrtr.writeU8(modelId);
        wrtr.writeBool(hasDisk);
        wrtr.writeString(diskPath);
    }

    wrtr.endChunk();
}

bool MediaManager::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "MED0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver)) return false;
    if (ver != 1) return false;

    // Cartridge
    if (!rdr.readBool(state_.cartAttached)) return false;
    if (!rdr.readString(state_.cartPath)) return false;

    // PRG
    if (!rdr.readBool(state_.prgAttached)) return false;
    if (!rdr.readString(state_.prgPath)) return false;

    uint32_t delayU32 = 0;
    if (!rdr.readU32(delayU32)) return false;
    state_.prgDelay = static_cast<int>(delayU32);

    if (!rdr.readBool(state_.prgLoaded)) return false;

    bool hasPrgImage = false;
    if (!rdr.readBool(hasPrgImage)) return false;

    prgImage_.clear();
    if (hasPrgImage)
    {
        if (!rdr.readVectorU8(prgImage_)) return false;
    }

    // Tape
    if (!rdr.readBool(state_.tapeAttached)) return false;
    if (!rdr.readString(state_.tapePath)) return false;

    // Drive mount table (8..11)
    uint8_t firstDev = 0, lastDev = 0;
    if (!rdr.readU8(firstDev)) return false;
    if (!rdr.readU8(lastDev)) return false;

    // We only expect 8..11 from your saver, but tolerate other ranges safely.
    for (uint8_t dev = firstDev; dev <= lastDev; ++dev)
    {
        bool present = false;
        if (!rdr.readBool(present)) return false;

        uint8_t modelId = 0;
        bool hasDisk = false;
        std::string diskPath;

        if (!rdr.readU8(modelId)) return false;
        if (!rdr.readBool(hasDisk)) return false;
        if (!rdr.readString(diskPath)) return false;

        if (!present) continue;

        if (dev < 8 || dev > 11) continue;

        // Validate modelId to avoid corrupted-file crashes
        if (!isValidDriveModelId(modelId)) continue;

        const DriveModel model = static_cast<DriveModel>(modelId);
        if (model == DriveModel::None) continue;

        if (hasDisk && !diskPath.empty())
        {
            // This will create the drive if missing and register it
            attachDiskImage(static_cast<int>(dev), model, diskPath);
        }
    }

    return true;
}

std::string MediaManager::lowerExt(const std::string& path)
{
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}

bool MediaManager::isExtCompatible(DriveModel model, const std::string& ext)
{
    switch (model)
    {
        case DriveModel::None:
            return false;
        case DriveModel::D1541:
            return (ext == ".d64" || ext == ".g64");
        case DriveModel::D1571:
            return (ext == ".d64" || ext == ".g64" || ext == ".d71");
        case DriveModel::D1581:
            return (ext == ".d81");
    }
    return false;
}

void MediaManager::attachDiskImage(int deviceNum, DriveModel model, const std::string& path)
{
    if (path.empty()) return;
    if (deviceNum < 8 || deviceNum > 11) return;

    const std::string ext = lowerExt(path);
    if (!isExtCompatible(model, ext))
    {
        std::cout << "Incompatible disk image for selected drive type.\n";
        std::cout << "Drive " << deviceNum << " model=" << (int)model
                  << " path=" << path << "\n";
        return;
    }

    if (!drives_[deviceNum])
    {
        switch (model)
        {
            case DriveModel::None:
                return;
            case DriveModel::D1541:
                drives_[deviceNum] = std::make_unique<D1541>(deviceNum, D1541LoROM_, D1541HiROM_);
                break;
            case DriveModel::D1571:
                drives_[deviceNum] = std::make_unique<D1571>(deviceNum, D1571ROM_);
                break;
            case DriveModel::D1581:
                drives_[deviceNum] = std::make_unique<D1581>(deviceNum, D1581ROM_);
                break;
            default:
                return;
        }

        if (!drives_[deviceNum]) return;
        bus_.registerDevice(deviceNum, drives_[deviceNum].get());

        // Sync all existing devices so nobody has stale cached bus state
        for (int dev = 8; dev <= 11; ++dev)
        {
            if (drives_[dev]) drives_[dev]->forceSyncIEC();
        }

        // Defer bus priming to the emulator (safe point)
        if (requestBusPrime_) requestBusPrime_();
    }

    if (!drives_[deviceNum]->insert(path))
    {
        std::cout << "Disk insert failed: " << path << "\n";
        return;
    }

    // Keep UI simple: reflect “last attached disk”
    state_.diskAttached = true;
    state_.diskPath     =  "Drive " + std::to_string(deviceNum) + ": " + path;
}

void MediaManager::attachPRGImage()
{
    if (state_.prgPath.empty()) return;

    state_.prgAttached = true;
    state_.prgLoaded   = false;
    state_.prgDelay    = 140;

    if (!loadPrgImage())
    {
        std::cout << "Unable to load program: " << state_.prgPath << "\n";
        state_.prgAttached = false;
    }
    else
    {
        std::cout << "Queued program: " << state_.prgPath << "\n";
    }
}

void MediaManager::attachCRTImage()
{
    if (state_.cartPath.empty()) return;

    recreateCartridge();

    state_.cartAttached = true;

    if (!cart_->loadROM(state_.cartPath))
    {
        std::cout << "Unable to load cartridge: " << state_.cartPath << "\n";
        state_.cartAttached = false;
        return;
    }

    mem_.setCartridgeAttached(true);
    pla_.setCartridgeAttached(true);

    if (coldReset_) coldReset_();

    std::cout << "Cartridge attached: " << state_.cartPath << "\n";
}

void MediaManager::attachT64Image()
{
    if (state_.tapePath.empty()) return;

    state_.tapeAttached = true;

    if (!cass_.loadCassette(state_.tapePath, videoMode_))
    {
        #ifdef Debug
        std::cout << "Unable to load tape: " << state_.tapePath << "\n";
        #endif
        state_.tapeAttached = false;
        return;
    }

    if (cass_.isT64())
    {
        T64LoadResult result = cass_.t64LoadPrgIntoMemory();
        if (result.success)
        {
            uint16_t scan = 0x0801;
            uint16_t nextLine;
            do {
                nextLine = mem_.read(scan) | (mem_.read(scan + 1) << 8);
                if (nextLine == 0) break;
                scan = nextLine;
            } while (true);

            uint16_t basicEnd = scan + 2;

            mem_.writeDirect(0x2B, 0x01); mem_.writeDirect(0x2C, 0x08);
            mem_.writeDirect(0x2D, basicEnd & 0xFF); mem_.writeDirect(0x2E, basicEnd >> 8);
            mem_.writeDirect(0x2F, basicEnd & 0xFF); mem_.writeDirect(0x30, basicEnd >> 8);
            mem_.writeDirect(0x31, basicEnd & 0xFF); mem_.writeDirect(0x32, basicEnd >> 8);

            const uint8_t runKeys[4] = { 0x52, 0x55, 0x4E, 0x0D };
            mem_.writeDirect(0xC6, 4);
            for (int i = 0; i < 4; ++i) mem_.writeDirect(0x0277 + i, runKeys[i]);
        }
    }
}

void MediaManager::attachTAPImage()
{
    if (state_.tapePath.empty()) return;

    state_.tapeAttached = true;

    if (!cass_.loadCassette(state_.tapePath, videoMode_))
    {
        #ifdef Debug
        std::cout << "Unable to load tape: " << state_.tapePath << "\n";
        #endif
        state_.tapeAttached = false;
    }
}

void MediaManager::tapePlay()
{
    cass_.play();
}

void MediaManager::tapeStop()
{
    cass_.stop();
}

void MediaManager::tapeRewind()
{
    cass_.rewind();
}

void MediaManager::tapeEject()
{
    cass_.stop();
    cass_.eject();
    state_.tapeAttached = false;
    state_.tapePath.clear();
}

void MediaManager::applyBootAttachments()
{
    if (state_.cartAttached && !state_.cartPath.empty())
    {
        attachCRTImage();
        return;
    }

    if (state_.tapeAttached && !state_.tapePath.empty())
    {
        const std::string ext = lowerExt(state_.tapePath);
        if (ext == ".t64") attachT64Image();
        else               attachTAPImage();
        return;
    }

    if (state_.prgAttached && !state_.prgPath.empty())
    {
        attachPRGImage();
        return;
    }
}

void MediaManager::tick()
{
    if (state_.prgAttached && !state_.prgLoaded && state_.prgDelay <= 0)
    {
        loadPrgIntoMem();
        state_.prgLoaded = true;
    }
    else if (state_.prgAttached && !state_.prgLoaded && state_.prgDelay > 0)
    {
        --state_.prgDelay;
    }
}

bool MediaManager::loadPrgImage()
{
    std::ifstream prgFile(state_.prgPath, std::ios::binary | std::ios::ate);
    if (!prgFile) return false;

    std::streamsize size = prgFile.tellg();
    if (size < 3) return false;

    prgFile.seekg(0);
    prgImage_.resize(static_cast<size_t>(size));
    prgFile.read(reinterpret_cast<char*>(prgImage_.data()), size);
    return true;
}

void MediaManager::loadPrgIntoMem()
{
    size_t pos = 0;

    // Skip the .P00 header if present ("C64File")
    if (prgImage_.size() >= 26 && std::memcmp(prgImage_.data(), "C64File", 7) == 0)
        pos = 26;

    if (pos + 2 > prgImage_.size())
        throw std::runtime_error("PRG/P00 image is too small.");

    const uint16_t loadAddr = prgImage_[pos] | (prgImage_[pos + 1] << 8);
    pos += 2;

    const size_t programData = prgImage_.size() - pos;
    const uint32_t endProgramData = static_cast<uint32_t>(loadAddr) + static_cast<uint32_t>(programData);

    if (endProgramData > 0x10000)
        throw std::runtime_error("Error: Program is too large for 64k RAM!");

    for (size_t i = 0; i < programData; ++i)
        mem_.writeDirect(loadAddr + static_cast<uint16_t>(i), prgImage_[pos + i]);

    if (loadAddr == BASIC_PRG_START)
    {
        uint16_t scan = loadAddr;
        uint16_t nextLine;
        do
        {
            nextLine = mem_.read(scan) | (mem_.read(scan + 1) << 8);
            if (nextLine == 0) break;
            scan = nextLine;
        } while (true);

        const uint16_t basicEnd = scan + 2;

        mem_.writeDirect(TXTAB,     loadAddr & 0xFF);
        mem_.writeDirect(TXTAB + 1, (loadAddr >> 8));
        mem_.writeDirect(VARTAB,     basicEnd & 0xFF);
        mem_.writeDirect(VARTAB + 1, (basicEnd >> 8));
        mem_.writeDirect(ARYTAB,     basicEnd & 0xFF);
        mem_.writeDirect(ARYTAB + 1, (basicEnd >> 8));
        mem_.writeDirect(STREND,     basicEnd & 0xFF);
        mem_.writeDirect(STREND + 1, (basicEnd >> 8));

        const uint8_t runKeys[4] = { 0x52, 0x55, 0x4E, 0x0D };
        mem_.writeDirect(0xC6, 4);
        for (int i = 0; i < 4; ++i)
            mem_.writeDirect(0x0277 + i, runKeys[i]);
    }
}

void MediaManager::recreateCartridge()
{
    cart_ = std::make_unique<Cartridge>();

    cart_->attachCPUInstance(&cpu_);
    cart_->attachMemoryInstance(&mem_);
    cart_->attachLogInstance(&logger_);
    cart_->attachTraceManagerInstance(&traceMgr_);
    cart_->attachVicInstance(&vic_);

    mem_.attachCartridgeInstance(cart_.get());
    pla_.attachCartridgeInstance(cart_.get());
    traceMgr_.attachCartInstance(cart_.get());

    monbackend_.attachCartridgeInstance(cart_.get());
}
