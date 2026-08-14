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
#include "Cartridge/ICartridgeHost.h"
#include "Cartridge/IHasButton.h"
#include "Cartridge/IHasIDE64Storage.h"
#include "Cartridge/IHasSwitch.h"
#include "DebugManager.h"
#include "Drive/D1541.h"
#include "Drive/D1571.h"
#include "Drive/D1581.h"
#include "Drive/Drive.h"
#include "Drive/IDriveIndicatorView.h"
#include "Drive/IDrivePositionView.h"
#include "Drive/IDriveUiView.h"
#include "MachineComponents.h"
#include "Debug/MLMonitorBackend.h"

MediaManager::MediaManager(MachineComponents& components,
                           ICartridgeHost* host,
                           std::string D1541LoROM,
                           std::string D1541HiROM,
                           std::string D1571ROM,
                           std::string D1581ROM,
                           std::function<void()> requestBusPrimeCallback,
                           std::function<void()> coldResetCallback)
    : components_(components),
      host_(host),
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
    wrtr.writeU32(1); // version

    // Dump Cartridge
    wrtr.writeBool(state_.cartAttached);
    wrtr.writeString(state_.cartPath);

    // Dump TAP
    wrtr.writeBool(state_.tapeAttached);
    wrtr.writeString(state_.tapePath);

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
        const bool present = (components_.drives[dev] != nullptr);
        wrtr.writeBool(present);

        uint8_t modelId = 0;
        bool hasDisk = false;
        std::string diskPath;

        if (present)
        {
            modelId = static_cast<uint8_t>(components_.drives[dev]->getDriveModel());
            hasDisk = components_.drives[dev]->isDiskLoaded();
            diskPath = hasDisk ? components_.drives[dev]->getCurrentDiskPath() : std::string{};
        }

        wrtr.writeU8(modelId);
        wrtr.writeBool(hasDisk);
        wrtr.writeString(diskPath);
    }

    // Dump REU attachment
    wrtr.writeBool(state_.reuEnabled);
    wrtr.writeU8(static_cast<uint8_t>(state_.reuModel));

    wrtr.endChunk();
}

bool MediaManager::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "MED0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                  { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                           { rdr.exitChunkPayload(chunk); return false; }

        // Cartridge
        if (!rdr.readBool(state_.cartAttached))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readString(state_.cartPath))                   { rdr.exitChunkPayload(chunk); return false; }

        // Tape
        if (!rdr.readBool(state_.tapeAttached))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readString(state_.tapePath))                   { rdr.exitChunkPayload(chunk); return false; }

        // PRG
        if (!rdr.readBool(state_.prgAttached))                  { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readString(state_.prgPath))                    { rdr.exitChunkPayload(chunk); return false; }

        uint32_t delayU32 = 0;
        if (!rdr.readU32(delayU32))                             { rdr.exitChunkPayload(chunk); return false; }
        state_.prgDelay = static_cast<int>(delayU32);

        if (!rdr.readBool(state_.prgLoaded))                    { rdr.exitChunkPayload(chunk); return false; }

        bool hasPrgImage = false;
        if (!rdr.readBool(hasPrgImage))                         { rdr.exitChunkPayload(chunk); return false; }

        prgImage_.clear();
        if (hasPrgImage)
        {
            if (!rdr.readVectorU8(prgImage_))                   { rdr.exitChunkPayload(chunk); return false; }
        }

        // Tape
        if (!rdr.readBool(state_.tapeAttached))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readString(state_.tapePath))                   { rdr.exitChunkPayload(chunk); return false; }

        // Drive mount table (8..11)
        uint8_t firstDev = 0, lastDev = 0;
        if (!rdr.readU8(firstDev))                              { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(lastDev))                               { rdr.exitChunkPayload(chunk); return false; }

        // We only expect 8..11 from your saver, but tolerate other ranges safely.
        for (uint8_t dev = firstDev; dev <= lastDev; ++dev)
        {
            bool present = false;
            if (!rdr.readBool(present))                         { rdr.exitChunkPayload(chunk); return false; }

            uint8_t modelId = 0;
            bool hasDisk = false;
            std::string diskPath;

            if (!rdr.readU8(modelId))                           { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readBool(hasDisk))                         { rdr.exitChunkPayload(chunk); return false; }
            if (!rdr.readString(diskPath))                      { rdr.exitChunkPayload(chunk); return false; }

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

        // REU attachment
        if (!rdr.readBool(state_.reuEnabled))                   { rdr.exitChunkPayload(chunk); return false; }

        uint8_t reuModelId = 0;
        if (!rdr.readU8(reuModelId))                            { rdr.exitChunkPayload(chunk); return false; }

        state_.reuModel = static_cast<REUModel>(reuModelId);

        if (state_.reuEnabled && state_.reuModel != REUModel::None)
        {
            attachREU(state_.reuModel);
        }
        else
        {
            detachREU();
        }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Not our chunk
    return false;
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
        #ifdef Debug
        std::cout << "Incompatible disk image for selected drive type.\n";
        std::cout << "Drive " << deviceNum << " model=" << (int)model
                  << " path=" << path << "\n";
        #endif
        return;
    }

    if (!components_.drives[deviceNum])
    {
        switch (model)
        {
            case DriveModel::None:
                return;
            case DriveModel::D1541:
                components_.drives[deviceNum] = std::make_unique<D1541>(deviceNum, D1541LoROM_, D1541HiROM_);
                break;
            case DriveModel::D1571:
                components_.drives[deviceNum] = std::make_unique<D1571>(deviceNum, D1571ROM_);
                break;
            case DriveModel::D1581:
                components_.drives[deviceNum] = std::make_unique<D1581>(deviceNum, D1581ROM_);
                break;
            default:
                return;
        }

        if (!components_.drives[deviceNum]) return;
        components_.bus->registerDevice(deviceNum, components_.drives[deviceNum].get());

        // Sync all existing devices so nobody has stale cached bus state
        for (int dev = 8; dev <= 11; ++dev)
        {
            if (components_.drives[dev]) components_.drives[dev]->forceSyncIEC();
        }

        // Defer bus priming to the emulator (safe point)
        if (requestBusPrime_) requestBusPrime_();
    }

    // Existing drive must match requested model.
    if (components_.drives[deviceNum]->getDriveModel() != model)
    {
        #ifdef Debug
        std::cout << "Drive " << deviceNum
                  << " already exists as model "
                  << static_cast<int>(components_.drives[deviceNum]->getDriveModel())
                  << ". Eject/remove the drive before changing type.\n";
        #endif

        return;
    }

    if (!components_.drives[deviceNum]->insert(path))
    {
        #ifdef Debug
        std::cout << "Disk insert failed: " << path << "\n";
        #endif
        return;
    }

    // Keep UI simple: reflect “last attached disk”
    state_.diskAttached = true;
    state_.diskPath     =  "Drive " + std::to_string(deviceNum) + ": " + path;
}

void MediaManager::attachPRGImage(PRGLoadMode mode)
{
    if (state_.prgPath.empty())
        return;

    prgLoadMode_ = mode;

    state_.prgAttached = false;
    state_.prgLoaded   = false;
    state_.prgDelay    = 0;

    if (state_.cartAttached)
    {
        if (mode == PRGLoadMode::Standalone)
        {
            // Standalone PRG loading removes the cartridge.
            // detachCRTImage() handles the required reset.
            detachCRTImage();
        }
        else
        {
            // KeepCartridge preserves the current cartridge and
            // running IDEDOS session. Do not reset here.
        }
    }
    else if (coldReset_)
    {
        // No cartridge is attached, so start from a clean machine.
        coldReset_();
    }

    state_.prgAttached = true;
    state_.prgLoaded   = false;
    state_.prgDelay    = 140;

    if (!loadPrgImage())
    {
        #ifdef Debug
        std::cout << "Unable to load program: "
                  << state_.prgPath << "\n";
        #endif

        state_.prgAttached = false;
    }
    else
    {
        #ifdef Debug
        std::cout << "Queued program: "
                  << state_.prgPath << "\n";
        #endif
    }
}

void MediaManager::attachCRTImage()
{
    if (state_.cartPath.empty()) return;

    recreateCartridge();

    state_.cartAttached = true;

    if (!components_.cart->loadROM(state_.cartPath))
    {
        #ifdef Debug
        std::cout << "Unable to load cartridge: " << state_.cartPath << "\n";
        #endif
        state_.cartAttached = false;
        return;
    }

    components_.mem->setCartridgeAttached(true);
    components_.pla->setCartridgeAttached(true);

    if (coldReset_) coldReset_();

    #ifdef Debug
    std::cout << "Cartridge attached: " << state_.cartPath << "\n";
    #endif
}

void MediaManager::attachT64Image()
{
    if (state_.tapePath.empty()) return;

    state_.tapeAttached = true;

    if (!components_.cass->loadCassette(state_.tapePath, videoMode_))
    {
        #ifdef Debug
        std::cout << "Unable to load tape: " << state_.tapePath << "\n";
        #endif
        state_.tapeAttached = false;
        return;
    }

    if (components_.cass->isT64())
    {
        T64LoadResult result = components_.cass->t64LoadPrgIntoMemory();
        if (result.success)
        {
            uint16_t scan = 0x0801;
            uint16_t nextLine;
            do
            {
                nextLine = components_.mem->read(scan) | (components_.mem->read(scan + 1) << 8);
                if (nextLine == 0) break;
                scan = nextLine;
            }
            while (true);

            uint16_t basicEnd = scan + 2;

            components_.mem->writeDirect(0x2B, 0x01); components_.mem->writeDirect(0x2C, 0x08);
            components_.mem->writeDirect(0x2D, basicEnd & 0xFF); components_.mem->writeDirect(0x2E, basicEnd >> 8);
            components_.mem->writeDirect(0x2F, basicEnd & 0xFF); components_.mem->writeDirect(0x30, basicEnd >> 8);
            components_.mem->writeDirect(0x31, basicEnd & 0xFF); components_.mem->writeDirect(0x32, basicEnd >> 8);

            const uint8_t runKeys[4] = { 0x52, 0x55, 0x4E, 0x0D };
            components_.mem->writeDirect(0xC6, 4);
            for (int i = 0; i < 4; ++i) components_.mem->writeDirect(0x0277 + i, runKeys[i]);
        }
    }
}

void MediaManager::attachTAPImage()
{
    if (state_.tapePath.empty()) return;

    state_.tapeAttached = true;

    if (!components_.cass->loadCassette(state_.tapePath, videoMode_))
    {
        #ifdef Debug
        std::cout << "Unable to load tape: " << state_.tapePath << "\n";
        #endif
        state_.tapeAttached = false;
    }
}

void MediaManager::attachREU(REUModel model)
{
    if (model == REUModel::None)
    {
        detachREU();
        return;
    }

    if (state_.reuEnabled && state_.reuModel == model)
        return;

    state_.reuEnabled = true;
    state_.reuModel   = model;

    components_.reu->setModel(model);
    components_.mem->attachREUInstance(components_.reu.get());

    if (coldReset_)
        coldReset_();
}

void MediaManager::createBlankDisk(int deviceNum, DriveModel model, const std::string& path)
{
    if (path.empty()) return;
    if (deviceNum < 8 || deviceNum > 11) return;

    const std::string ext = lowerExt(path);
    if (!isExtCompatible(model, ext))
    {
        #ifdef Debug
        std::cout << "Incompatible disk image for selected drive type.\n";
        std::cout << "Drive " << deviceNum << " model=" << (int)model
                  << " path=" << path << "\n";
        #endif
        return;
    }

    if (!components_.drives[deviceNum])
    {
        switch (model)
        {
            case DriveModel::None:
                return;
            case DriveModel::D1541:
                components_.drives[deviceNum] = std::make_unique<D1541>(deviceNum, D1541LoROM_, D1541HiROM_);
                break;
            case DriveModel::D1571:
                components_.drives[deviceNum] = std::make_unique<D1571>(deviceNum, D1571ROM_);
                break;
            case DriveModel::D1581:
                components_.drives[deviceNum] = std::make_unique<D1581>(deviceNum, D1581ROM_);
                break;
            default:
                return;
        }

        if (!components_.drives[deviceNum]) return;
        components_.bus->registerDevice(deviceNum, components_.drives[deviceNum].get());

        // Sync all existing devices so nobody has stale cached bus state
        for (int dev = 8; dev <= 11; ++dev)
        {
            if (components_.drives[dev]) components_.drives[dev]->forceSyncIEC();
        }

        // Defer bus priming to the emulator (safe point)
        if (requestBusPrime_) requestBusPrime_();
    }

    // Existing drive must match requested model.
    if (components_.drives[deviceNum]->getDriveModel() != model)
    {
        #ifdef Debug
        std::cout << "Drive " << deviceNum
                  << " already exists as model "
                  << static_cast<int>(components_.drives[deviceNum]->getDriveModel())
                  << ". Eject/remove the drive before changing type.\n";
        #endif

        return;
    }

    DiskFormat format = diskFormatForDriveModel(model);

    DiskFactory factory;
    auto disk = factory.createBlank(path, format, "BLANK", "00");

    attachDiskImage(deviceNum, model, path);
}

void MediaManager::detachDiskImage(int dev)
{
    if (dev < 8 || dev > 11) return;

    if (!components_.drives[dev]) return;

    components_.drives[dev]->unloadDisk();

    components_.bus->unregisterDevice(dev);

    components_.drives[dev].reset();
}

void MediaManager::detachCRTImage()
{
    if (!state_.cartAttached)
        return;

    state_.cartAttached = false;
    state_.cartPath.clear();

    components_.mem->setCartridgeAttached(false);
    components_.pla->setCartridgeAttached(false);

    recreateCartridge();

    if (coldReset_)
        coldReset_();

    #ifdef Debug
    std::cout << "Cartridge detached\n";
    #endif
}

void MediaManager::detachREU()
{
    if (!state_.reuEnabled && state_.reuModel == REUModel::None)
        return;

    state_.reuEnabled = false;
    state_.reuModel   = REUModel::None;

    components_.reu->setModel(REUModel::None);

    // Leave Memory attached to the stable REU object.
    // The REU model/state tells Memory whether REU is active.

    if (coldReset_)
        coldReset_();
}

bool MediaManager::loadIDE64Image(uint32_t deviceIndex, const std::string& path, bool readOnly)
{
    if (!components_.cart)
        return false;

    CartridgeMapper* mapper = components_.cart->getMapper();
    if (!mapper)
        return false;

    auto* ide64 = dynamic_cast<IHasIDE64Storage*>(mapper);
    if (!ide64)
        return false;

    return ide64->loadIDE64Image(deviceIndex, path, readOnly);
}

bool MediaManager::createIDE64Image(uint32_t deviceIndex, const std::string& path, uint32_t sectors)
{
    if (!components_.cart)
        return false;

    CartridgeMapper* mapper = components_.cart->getMapper();
    if (!mapper)
        return false;

    auto* ide64 = dynamic_cast<IHasIDE64Storage*>(mapper);
    if (!ide64)
        return false;

    return ide64->createIDE64Image(deviceIndex, path, sectors);
}

bool MediaManager::saveIDE64Image(uint32_t deviceIndex)
{
    if (!components_.cart)
        return false;

    CartridgeMapper* mapper = components_.cart->getMapper();
    if (!mapper)
        return false;

    auto* ide64 = dynamic_cast<IHasIDE64Storage*>(mapper);
    if (!ide64)
        return false;

    return ide64->saveIDE64Image(deviceIndex);
}

bool MediaManager::ejectIDE64Image(uint32_t deviceIndex)
{
    if (!components_.cart)
        return false;

    CartridgeMapper* mapper = components_.cart->getMapper();
    if (!mapper)
        return false;

    auto* ide64 = dynamic_cast<IHasIDE64Storage*>(mapper);
    if (!ide64)
        return false;

    return ide64->ejectIDE64Image(deviceIndex);
}

const Cartridge* MediaManager::getCartridge() const
{
    return components_.cart.get();
}

Cartridge* MediaManager::getCartridge()
{
    return components_.cart.get();
}

void MediaManager::pressButton(uint32_t index)
{
    if (!components_.cart) return;

    auto* mapper = components_.cart->getMapper();
    if (auto* hb = dynamic_cast<IHasButton*>(mapper))
    {
        hb->pressButton(index);
    }
}

void MediaManager::setCartSwitch(uint32_t switchIndex, uint32_t switchPos)
{
    if (!state_.cartAttached) return;

    Cartridge* cart = getCartridge();   // uses the new non-const getter
    if (!cart) return;

    CartridgeMapper* mapper = cart->getMapper(); // assumes you also have non-const getMapper()
    if (!mapper) return;

    if (auto* hs = dynamic_cast<IHasSwitch*>(mapper))
    {
        hs->setSwitchPosition(switchIndex, switchPos);
    }
}

void MediaManager::restoreCartridgeFromState()
{
    // First, make sure "no cart" is the baseline
    components_.mem->setCartridgeAttached(false);
    components_.pla->setCartridgeAttached(false);

    if (!state_.cartAttached || state_.cartPath.empty())
        return;

    recreateCartridge();

    if (!components_.cart->loadROM(state_.cartPath))
    {
        #ifdef Debug
        std::cout << "Restore cartridge failed: " << state_.cartPath << "\n";
        #endif
        state_.cartAttached = false;
        state_.cartPath.clear();
        return;
    }

    components_.mem->setCartridgeAttached(true);
    components_.pla->setCartridgeAttached(true);
}

void MediaManager::restoreTapeMountOnlyFromState()
{
    // Clear first
    if (!state_.tapeAttached || state_.tapePath.empty())
        return;

    // Just mount tape image; DO NOT load PRG into RAM; DO NOT inject RUN
    components_.cass->stop();
    components_.cass->eject();

    if (!components_.cass->loadCassette(state_.tapePath, videoMode_))
    {
        state_.tapeAttached = false;
        state_.tapePath.clear();
        return;
    }
}

bool MediaManager::ensureDriveExists(int deviceNum, DriveModel model)
{
    if (deviceNum < 8 || deviceNum > 11)
        return false;

    if (model == DriveModel::None)
        return false;

    if (components_.drives[deviceNum])
    {
        if (components_.drives[deviceNum]->getDriveModel() == model)
            return true;

        components_.bus->unregisterDevice(deviceNum);
        components_.drives[deviceNum].reset();
    }

    switch (model)
    {
        case DriveModel::D1541:
            components_.drives[deviceNum] = std::make_unique<D1541>
            (
                deviceNum,
                D1541LoROM_,
                D1541HiROM_
            );
            break;

        case DriveModel::D1571:
            components_.drives[deviceNum] = std::make_unique<D1571>
            (
                deviceNum,
                D1571ROM_
            );
            break;

        case DriveModel::D1581:
            components_.drives[deviceNum] = std::make_unique<D1581>
            (
                deviceNum,
                D1581ROM_
            );
            break;

        case DriveModel::None:
        default:
            return false;
    }

    if (!components_.drives[deviceNum])
        return false;

    components_.bus->registerDevice(deviceNum, components_.drives[deviceNum].get());

    for (int dev = 8; dev <= 11; ++dev)
    {
        if (components_.drives[dev])
            components_.drives[dev]->forceSyncIEC();
    }

    if (requestBusPrime_)
        requestBusPrime_();

    return true;
}

void MediaManager::tapePlay()
{
    components_.cass->play();
}

void MediaManager::tapeStop()
{
    components_.cass->stop();
}

void MediaManager::tapeRewind()
{
    components_.cass->rewind();
}

void MediaManager::tapeEject()
{
    components_.cass->stop();
    components_.cass->eject();
    state_.tapeAttached = false;
    state_.tapePath.clear();
}

void MediaManager::applyBootAttachments()
{
    if (state_.cartAttached && !state_.cartPath.empty())
    {
        attachCRTImage();

        if (state_.cartAttached &&
            state_.prgAttached &&
            !state_.prgPath.empty())
        {
            attachPRGImage(PRGLoadMode::KeepCartridge);
        }

        return;
    }

    if (state_.tapeAttached && !state_.tapePath.empty())
    {
        const std::string ext = lowerExt(state_.tapePath);

        if (ext == ".t64")
            attachT64Image();
        else
            attachTAPImage();

        return;
    }

    if (state_.prgAttached && !state_.prgPath.empty())
    {
        attachPRGImage(PRGLoadMode::Standalone);
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

void MediaManager::fillDriveStatusViews(std::vector<EmulatorUI::DriveStatusView>& out) const
{
    out.clear();

    for (int dev = 8; dev <= 11; ++dev)
    {
        EmulatorUI::DriveStatusView ds;
        ds.deviceNum = dev;

        Drive* drive = components_.drives[dev].get();
        if (!drive)
        {
            out.push_back(std::move(ds));
            continue;
        }

        ds.present = true;

        if (auto* ui = dynamic_cast<IDriveUiView*>(drive))
        {
            ds.modelName = ui->getDriveModelName();
            ds.diskInserted = ui->hasDiskInserted();
            ds.imagePath = ui->getMountedImagePath();
            ds.driveType = toUiDriveType(drive->getDriveModel());
        }
        else
        {
            ds.diskInserted = drive->isDiskLoaded();
            ds.imagePath = ds.diskInserted ? drive->getCurrentDiskPath() : std::string{};
        }

        if (auto* pos = dynamic_cast<IDrivePositionView*>(drive))
        {
            ds.hasTrackSector = pos->hasTrackSector();
            if (ds.hasTrackSector)
            {
                ds.track = pos->getTrack();
                ds.sector = pos->getSector();
            }
        }

        if (auto* ind = dynamic_cast<IDriveIndicatorView*>(drive))
        {
            std::vector<IDriveIndicatorView::Indicator> temp;
            ind->getDriveIndicators(temp);

            for (const auto& src : temp)
            {
                EmulatorUI::DriveLightView light;
                light.name = src.name;
                light.on = src.on;

                switch (src.color)
                {
                    case IDriveIndicatorView::DriveIndicatorColor::Green:
                        light.color = EmulatorUI::DriveLightColor::Green;
                        break;
                    case IDriveIndicatorView::DriveIndicatorColor::Red:
                        light.color = EmulatorUI::DriveLightColor::Red;
                        break;
                    case IDriveIndicatorView::DriveIndicatorColor::Yellow:
                        light.color = EmulatorUI::DriveLightColor::Yellow;
                        break;
                    case IDriveIndicatorView::DriveIndicatorColor::Amber:
                        light.color = EmulatorUI::DriveLightColor::Amber;
                        break;
                }

                ds.lights.push_back(std::move(light));
            }
        }

        out.push_back(std::move(ds));
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
    {
        const uint16_t address =
            loadAddr + static_cast<uint16_t>(i);

        if (prgLoadMode_ == PRGLoadMode::KeepCartridge)
            components_.mem->write(address, prgImage_[pos + i]);
        else
            components_.mem->writeDirect(address, prgImage_[pos + i]);
    }

    if (loadAddr == BASIC_PRG_START)
    {
        uint16_t scan = loadAddr;
        uint16_t nextLine;
        do
        {
            nextLine = components_.mem->read(scan) | (components_.mem->read(scan + 1) << 8);
            if (nextLine == 0) break;
            scan = nextLine;
        } while (true);

        const uint16_t basicEnd = scan + 2;

        components_.mem->writeDirect(TXTAB,     loadAddr & 0xFF);
        components_.mem->writeDirect(TXTAB + 1, (loadAddr >> 8));
        components_.mem->writeDirect(VARTAB,     basicEnd & 0xFF);
        components_.mem->writeDirect(VARTAB + 1, (basicEnd >> 8));
        components_.mem->writeDirect(ARYTAB,     basicEnd & 0xFF);
        components_.mem->writeDirect(ARYTAB + 1, (basicEnd >> 8));
        components_.mem->writeDirect(STREND,     basicEnd & 0xFF);
        components_.mem->writeDirect(STREND + 1, (basicEnd >> 8));

        const uint8_t runKeys[4] = { 0x52, 0x55, 0x4E, 0x0D };
        components_.mem->writeDirect(0xC6, 4);
        for (int i = 0; i < 4; ++i)
            components_.mem->writeDirect(0x0277 + i, runKeys[i]);
    }
}

void MediaManager::recreateCartridge()
{
    if (!components_.cart)
        return;

   components_.cart->clear();

    // Reattach all host/system pointers in case reset/load expects them.
    components_.cart->attachHostInstance(host_);
    components_.cart->attachCPUInstance(components_.cpu.get());
    components_.cart->attachMemoryInstance(components_.mem.get());
    components_.cart->attachTraceManagerInstance(&components_.debug->trace());
    components_.cart->attachVicInstance(components_.vic.get());

    // Reattach the same cartridge object everywhere else.
    components_.mem->attachCartridgeInstance(components_.cart.get());
    components_.pla->attachCartridgeInstance(components_.cart.get());
    components_.debug->trace().attachCartInstance(components_.cart.get());
    components_.debug->backend().attachCartridgeInstance(components_.cart.get());
}

UiCommand::DriveType MediaManager::toUiDriveType(DriveModel model) const
{
    switch (model)
    {
        case DriveModel::D1541:
            return UiCommand::DriveType::D1541;

        case DriveModel::D1571:
            return UiCommand::DriveType::D1571;

        case DriveModel::D1581:
            return UiCommand::DriveType::D1581;

        case DriveModel::None:
        default:
            return UiCommand::DriveType::None;
    }
}

DiskFormat MediaManager::diskFormatForDriveModel(DriveModel model)
{
    switch (model)
    {
        case DriveModel::D1541:
            return DiskFormat::D64;

        case DriveModel::D1571:
            return DiskFormat::D71;

        case DriveModel::D1581:
            return DiskFormat::D81;

        case DriveModel::None:
        default:
            return DiskFormat::Unknown;
    }
}

void MediaManager::flushAndSaveMedia()
{
    for (int dev = 8; dev <= 11; ++dev)
    {
        if (!components_.drives[dev])
            continue;

        // Best option if Drive base has/gets a virtual flush method:
        components_.drives[dev]->flushAndSaveDisk();
    }
}
