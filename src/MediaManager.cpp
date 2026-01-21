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

// Real includes (adjust paths to match your project)
#include "Cartridge.h"
#include "cassette.h"
#include "CPU.h"
#include "Drive/D1541.h"
#include "IECBUS.h"
#include "Logging.h"
#include "Memory.h"
#include "PLA.h"
#include "Debug/TraceManager.h"
#include "Vic.h"

MediaManager::MediaManager(std::unique_ptr<Cartridge>& cartSlot,
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
                           std::function<void()> coldResetCallback)
    : cart_(cartSlot),
      drive8_(drive8Slot),
      bus_(bus),
      mem_(mem),
      pla_(pla),
      cpu_(cpu),
      vic_(vic),
      traceMgr_(traceMgr),
      cass_(cass),
      logger_(logger),
      d1541LoROM_(std::move(d1541LoROM)),
      d1541HiROM_(std::move(d1541HiROM)),
      coldReset_(std::move(coldResetCallback))
{
    // Keep your default behavior
    state_.prgDelay = 140;
}

void MediaManager::attachD64Image()
{
    if (state_.diskPath.empty())
    {
        std::cout << "No disk image selected.\n";
        return;
    }

    if (!drive8_)
    {
        drive8_ = std::make_unique<D1541>(8, d1541LoROM_, d1541HiROM_);
        bus_.registerDevice(8, drive8_.get());
        drive8_->forceSyncIEC();
        bus_.reset();
    }

    if (drive8_) drive8_->loadDisk(state_.diskPath);

    state_.diskAttached = true;
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
        std::cout << "Unable to load tape: " << state_.tapePath << "\n";
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
        std::cout << "Unable to load tape: " << state_.tapePath << "\n";
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
}
