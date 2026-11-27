// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <bit>
#include <utility>
#include "Drive/D4040.h"
#include "Floppy/CBMImage.h"

D4040::D4040(int deviceNumber, const std::string& fdcROM, const std::string& loROM, const std::string& hiROM1, const std::string& hiROM2) :
    d4040Bus6502(D4040Bus::Processor::DOS6502),
    d4040Bus6504(D4040Bus::Processor::FDC6504)
{
    this->deviceNumber = deviceNumber;

    d4040mem.attachPeripheralInstance(this);

    d4040Bus6502.attachDriveInstance(this);
    d4040Bus6502.attachMemoryInstance(&d4040mem);
    d4040Bus6502.attachIEEE488DeviceInstance(&ieee488Device);

    d4040Bus6504.attachMemoryInstance(&d4040mem);

    driveCPU.attachCPUBusInstance(&d4040Bus6502);
    driveCPU.attachIRQLineInstance(&driveIRQ);

    controllerCPU.attachCPUBusInstance(&d4040Bus6504);
    controllerCPU.attachIRQLineInstance(&controllerIRQ);

    if (!d4040mem.initialize(fdcROM, loROM, hiROM1, hiROM2))
        throw std::runtime_error("Unable to start drive, ROM not loaded!\n");

    ieee488Device.setBusChangeCallback(
    [this](const IEEE488Bus::BusState& oldState,
           const IEEE488Bus::BusState& newState)
    {
        onIEEE488BusChanged(oldState, newState);
    });

    reset();
}

D4040::~D4040()
{
    ieee488Device.detachBus();
}

void D4040::reset()
{
    currentTrack                    = 18;
    currentSector                   = 0;

    controllerCycleBalance          = 0;
    driveCycleBalance               = 0;
    dosInstructionsExecuted         = 0;
    controllerInstructionsExecuted  = 0;
    debugSkipBreakpointCPU          = -1;

     // Reset shared memory-mapped hardware
    d4040mem.reset();

    // Reset drive mechanisms
    for (auto& mechanism : mechanisms)
        mechanism.reset();

    // Clear both CPU interrupt lines
    driveIRQ.clearIRQ(IRQLine::D4040_IRQ);
    controllerIRQ.clearIRQ(IRQLine::D4040_IRQ);

    // Reset the main DOS CPU
    driveCPU.reset();

    // Reset the controller CPU
    controllerCPU.reset();

    // Refresh motor and stepper signals.
    updateMechanisms();

    // Synchronize the IEEE-488 interface following reset.
    d4040Bus6502.updateRIOTInputs();
    d4040Bus6502.updateIEEE488Outputs();

    // Initialize controller input signals.
    updateControllerInputs();

    updateIRQ();
}

void D4040::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("4040");

    // Version 2
    wrtr.writeU32(2);

    wrtr.writeU8(static_cast<uint8_t>(deviceNumber));

    // -----------------------------------------
    // DOS CPU (6502)
    // -----------------------------------------
    driveCPU.saveStatePayload(wrtr);
    driveCPU.saveStateExtendedPayload(wrtr);

    // -----------------------------------------
    // Controller CPU (6504)
    // -----------------------------------------
    controllerCPU.saveStatePayload(wrtr);
    controllerCPU.saveStateExtendedPayload(wrtr);

    // -----------------------------------------
    // Shared drive memory
    // -----------------------------------------
    d4040mem.saveState(wrtr);

    // -----------------------------------------
    // DOS processor hardware
    // -----------------------------------------
    d4040mem.getRIOT1().save(wrtr);
    d4040mem.getRIOT2().save(wrtr);

    // -----------------------------------------
    // Controller processor hardware
    // -----------------------------------------
    d4040mem.getVIA().saveState(wrtr);
    d4040mem.getRRIOT().save(wrtr);

    // -----------------------------------------
    // Both floppy mechanisms
    // -----------------------------------------
    mechanisms[0].saveState(wrtr);
    mechanisms[1].saveState(wrtr);

    // -----------------------------------------
    // CPU scheduling state
    // -----------------------------------------
    wrtr.writeU64(static_cast<uint64_t>(driveCycleBalance));
    wrtr.writeU64(static_cast<uint64_t>(controllerCycleBalance));

    wrtr.writeU64(dosInstructionsExecuted);
    wrtr.writeU64(controllerInstructionsExecuted);

    // -----------------------------------------
    // Drive position
    // -----------------------------------------
    wrtr.writeU8(currentTrack);
    wrtr.writeU8(currentSector);

    // -----------------------------------------
    // Drive status
    // -----------------------------------------
    wrtr.writeU8(static_cast<uint8_t>(lastError));
    wrtr.writeU8(static_cast<uint8_t>(status));

    wrtr.endChunk();
}

bool D4040::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "4040", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    uint8_t savedDeviceNumber = 0;

    bool ok = true;

    // -----------------------------------------
    // Header
    // -----------------------------------------
    if (!rdr.readU32(ver))
        ok = false;

    if (ok && ver != 2)
        ok = false;

    if (ok && !rdr.readU8(savedDeviceNumber))
        ok = false;

    if (ok && savedDeviceNumber !=
        static_cast<uint8_t>(deviceNumber))
        ok = false;

    // -----------------------------------------
    // DOS CPU (6502)
    // -----------------------------------------
    if (ok && !driveCPU.loadStatePayload(rdr))
        ok = false;

    if (ok && !driveCPU.loadStateExtendedPayload(chunk, rdr))
        ok = false;

    // -----------------------------------------
    // Controller CPU (6504)
    // -----------------------------------------
    if (ok && !controllerCPU.loadStatePayload(rdr))
        ok = false;

    if (ok && !controllerCPU.loadStateExtendedPayload(chunk, rdr))
        ok = false;

    // -----------------------------------------
    // Shared drive memory
    // -----------------------------------------
    if (ok && !d4040mem.loadState(rdr))
        ok = false;

    // -----------------------------------------
    // DOS processor hardware
    // -----------------------------------------
    if (ok && !d4040mem.getRIOT1().load(rdr))
        ok = false;

    if (ok && !d4040mem.getRIOT2().load(rdr))
        ok = false;

    // -----------------------------------------
    // Controller processor hardware
    // -----------------------------------------
    if (ok && !d4040mem.getVIA().loadState(rdr))
        ok = false;

    if (ok && !d4040mem.getRRIOT().load(rdr))
        ok = false;

    // -----------------------------------------
    // Both floppy mechanisms
    // -----------------------------------------
    if (ok && !mechanisms[0].loadState(rdr))
        ok = false;

    if (ok && !mechanisms[1].loadState(rdr))
        ok = false;

    // -----------------------------------------
    // CPU scheduling state
    // -----------------------------------------
    uint64_t savedDriveBalance = 0;
    uint64_t savedControllerBalance = 0;

    if (ok && !rdr.readU64(savedDriveBalance))
        ok = false;

    if (ok && !rdr.readU64(savedControllerBalance))
        ok = false;

    if (ok)
    {
        driveCycleBalance =
            std::bit_cast<int64_t>(savedDriveBalance);

        controllerCycleBalance =
            std::bit_cast<int64_t>(savedControllerBalance);
    }

    if (ok && !rdr.readU64(dosInstructionsExecuted))
        ok = false;

    if (ok && !rdr.readU64(controllerInstructionsExecuted))
        ok = false;

    // -----------------------------------------
    // Drive position
    // -----------------------------------------
    if (ok && !rdr.readU8(currentTrack))
        ok = false;

    if (ok && !rdr.readU8(currentSector))
        ok = false;

    // -----------------------------------------
    // Drive status
    // -----------------------------------------
    uint8_t savedError = 0;
    uint8_t savedStatus = 0;

    if (ok && !rdr.readU8(savedError))
        ok = false;

    if (ok && !rdr.readU8(savedStatus))
        ok = false;

    if (ok)
    {
        lastError = static_cast<decltype(lastError)>(savedError);
        status = static_cast<decltype(status)>(savedStatus);
    }

    rdr.exitChunkPayload(chunk);

    if (!ok)
        return false;

    // Recompute interrupt outputs from restored
    // hardware state.
    updateIRQ();

    return true;
}

bool D4040::attachIEEE488Bus(IEEE488Bus* bus)
{
    if (!bus)
        return false;

    const auto id = static_cast<IEEE488Bus::DeviceId>(getDeviceNumber());

    if (!ieee488Device.attachBus(bus, id))
        return false;

    if (!bus->registerDrive(id, this))
    {
        ieee488Device.detachBus();
        return false;
    }

    // Synchronize RIOT inputs with the current
    // IEEE-488 bus state.
    d4040Bus6502.updateRIOTInputs();

    // Apply the drive's current handshake outputs.
    d4040Bus6502.updateIEEE488Outputs();

    return true;

}

void D4040::tick(uint32_t cycles)
{
    driveCycleBalance += static_cast<int64_t>(cycles);

    while (driveCycleBalance > 0)
    {
        // Check DOS 6502 breakpoints before execution.
        if (debugSkipBreakpointCPU != 0 && checkBreakpoint(0))
            return;

        // -----------------------------------------
        // Main DOS CPU (6502)
        // -----------------------------------------
        // Determine whether this clock can begin a new instruction.
        const bool dosAtBoundary = driveCPU.isAtInstructionBoundary();

        driveCPU.tick();

        // Only count clocks that began at an instruction boundary.
        if (dosAtBoundary)
        {
            ++dosInstructionsExecuted;

            // The selected instruction has started executing.
            // Restore breakpoint checking for CPU 0.
            if (debugSkipBreakpointCPU == 0)
                debugSkipBreakpointCPU = -1;
        }

        uint32_t dc = driveCPU.getElapsedCycles();

        if (dc == 0)
            dc = 1;

        driveCycleBalance -= static_cast<int64_t>(dc);

        // Both processors currently use a 1:1
        // emulated clock ratio.
        controllerCycleBalance += dc;

        // -----------------------------------------
        // Advance memory-mapped hardware timers
        // -----------------------------------------
        d4040mem.tick(dc);

        // Refresh interrupt lines.
        updateIRQ();

        // -----------------------------------------
        // Disk controller CPU (6504)
        // -----------------------------------------
        while (controllerCycleBalance > 0)
        {
            // Check controller 6504 breakpoints before execution.
            if (debugSkipBreakpointCPU != 1 && checkBreakpoint(1))
                return;

            // Make external status inputs visible before
            // the next controller instruction.
            updateControllerInputs();

            // Determine whether the controller can begin a new instruction.
            const bool controllerAtBoundary = controllerCPU.isAtInstructionBoundary();

            controllerCPU.tick();

            // Only count clocks that began at an instruction boundary.
            if (controllerAtBoundary)
            {
                ++controllerInstructionsExecuted;

                // Restore breakpoint checking after the controller
                // instruction begins executing.
                if (debugSkipBreakpointCPU == 1)
                    debugSkipBreakpointCPU = -1;
            }

            uint32_t fc = controllerCPU.getElapsedCycles();

            if (fc == 0)
                fc = 1;

            controllerCycleBalance -= static_cast<int64_t>(fc);

            // Pick up VIA motor and stepper changes.
            updateMechanisms();

            // Rotate disks and deliver selected GCR bytes.
            advanceGCR(fc);

            // Refresh status after rotation changes SYNC.
            updateControllerInputs();

            updateIRQ();
        }
    }
}

void D4040::updateIRQ()
{
    // -----------------------------------------
    // DOS CPU (6502)
    // -----------------------------------------
    const bool riotIRQ = d4040mem.getRIOT1().isIRQActive() || d4040mem.getRIOT2().isIRQActive();

    if (riotIRQ)
        driveIRQ.raiseIRQ(IRQLine::D4040_IRQ);
    else
        driveIRQ.clearIRQ(IRQLine::D4040_IRQ);

    // -----------------------------------------
    // Disk Controller CPU (6504)
    // -----------------------------------------
    const bool viaIRQ = d4040mem.getVIA().checkIRQActive();

    const bool rriotIRQ = d4040mem.getRRIOT().isIRQActive();

    const bool controllerInterrupt = viaIRQ || rriotIRQ;

    if (controllerInterrupt)
        controllerIRQ.raiseIRQ(IRQLine::D4040_IRQ);
    else
        controllerIRQ.clearIRQ(IRQLine::D4040_IRQ);
}

void D4040::getDriveIndicators(std::vector<Indicator>& out) const
{
    out.clear();

    Indicator act;
    act.name = "ACT";
    act.on = false;
    act.color = IDriveIndicatorView::DriveIndicatorColor::Red;

    out.push_back(std::move(act));
}

void D4040::loadDisk(const std::string& path)
{
    // The legacy single-disk interface targets mechanism 0.
    // Delegate to the mechanism-aware mounting function.
    if (!mountDisk(0, path))
    {
        throw std::runtime_error(
            "D4040: failed to mount disk in mechanism 0");
    }
}

void D4040::unloadDisk()
{
    if (!tryUnloadAllDisks())
    {
        std::cerr << "[D4040] Failed to eject disks\n";
        return;
    }
}

bool D4040::mountDiskToMechanism(uint32_t mechanismId, const std::string& path)
{
    if (mechanismId >= mechanisms.size())
        return false;

    return mountDisk(static_cast<uint8_t>(mechanismId), path);
}

bool D4040::unmountDiskFromMechanism(uint32_t mechanismId)
{
    if (mechanismId >= mechanisms.size())
        return false;

    const uint8_t unit = static_cast<uint8_t>(mechanismId);

    if (!isDiskLoaded(unit))
        return true;

    if (!tryUnloadDisk(unit))
        return false;

    return !isDiskLoaded(unit);
}

bool D4040::mountDisk(uint8_t unit, const std::string& path)
{
    if (unit >= mechanisms.size() || path.empty())
        return false;

    auto img = DiskFactory::create(path);

    if (!img || !img->loadDisk(path))
    {
        lastError = DriveError::NO_DISK;
        return false;
    }

    D4040Mechanism& mech = mechanisms[unit];

    // Preserve outgoing modified track before replacing it.
    mech.cacheActiveTrack();

    // Save any changes on the old disk first.
    if (!flushMechanism(unit))
    {
        std::cerr << "[D4040] Cannot replace disk in unit "
                  << static_cast<int>(unit)
                  << ": previous disk save failed\n";
        return false;
    }

    // The outgoing disk has already been saved successfully.
    // Install the replacement.
    mech.insertDisk(std::move(img), path);

    lastError = DriveError::NONE;
    status = DriveStatus::READY;

    return true;
}

void D4040::unloadDisk(uint8_t unit)
{
    if (!tryUnloadDisk(unit))
        std::cerr << "[D4040] Failed to eject unit "
                  << static_cast<int>(unit) << '\n';
}

bool D4040::isDiskLoaded(uint8_t unit) const
{
    if (unit >= mechanisms.size())
        return false;

    return mechanisms[unit].hasDisk();
}

std::string D4040::getDiskPath(uint8_t unit) const
{
    if (unit >= mechanisms.size())
        return {};

    return mechanisms[unit].getDiskPath();
}

void D4040::startMotor()
{
    mechanisms[0].setMotorCommand(true);
}

void D4040::stopMotor()
{
    mechanisms[0].setMotorCommand(false);
}

bool D4040::tryUnloadDisk(uint8_t unit)
{
    if (unit >= mechanisms.size())
        return false;

    // Refuse ejection if pending changes cannot be saved.
    if (!flushMechanism(unit))
        return false;

    // Remove the disk only after a successful save.
    mechanisms[unit].removeDisk();

    if (!isDiskLoaded(0) && !isDiskLoaded(1))
        status = DriveStatus::IDLE;

    return true;
}

bool D4040::tryUnloadAllDisks()
{
    // Save both before ejecting either.
    // This avoids a partial ejection if one save fails.
    if (!flushMechanism(0) || !flushMechanism(1))
        return false;

    // Both are now clean, so these should succeed.
    return tryUnloadDisk(0) && tryUnloadDisk(1);
}

bool D4040::tryFlushAndSaveDisk()
{
    // Attempt to save both mechanisms independently.
    // Always attempt unit 1, even if unit 0 fails.
    const bool unit0Saved = flushMechanism(0);
    const bool unit1Saved = flushMechanism(1);

    if (!unit0Saved || !unit1Saved)
    {
        std::cerr
            << "[D4040] WARNING: One or more disk "
               "images could not be saved\n";

        return false;
    }

    return true;
}

void D4040::flushAndSaveDisk()
{
    (void)tryFlushAndSaveDisk();
}

uint8_t D4040::getSelectedMechanism() const
{
    const MOS6530& rriot = d4040mem.getRRIOT();

    // PB0 selects the mechanism when configured as output.
    if ((rriot.getDDRB() & 0x01) == 0)
        return 0;

    return (rriot.getPortBOutput() & 0x01) ? 1 : 0;
}

void D4040::onIEEE488BusChanged(const IEEE488Bus::BusState& oldState, const IEEE488Bus::BusState& newState)
{
    // Apply the exact bus state from this transition.
    // This preserves ATN edges for RIOT interrupt logic.
    d4040Bus6502.updateRIOTInputs(newState);

    if (oldState.atn != newState.atn)
        d4040Bus6502.updateIEEE488Outputs();

    updateIRQ();
}

void D4040::updateMechanisms()
{
    const D4040MotorSignals signals = d4040mem.getVIA().getMotorSignals();

    for (uint8_t unit = 0; unit < 2; ++unit)
    {
        D4040Mechanism& mech = mechanisms[unit];

        const uint8_t newPhase = (unit == 0) ? signals.stepper0 : signals.stepper1;

        // Record the electrical motor command.
        // Physical spindle movement is handled by advanceGCR().
        mech.setMotorCommand((unit == 0) ? signals.motor0On : signals.motor1On);

        const uint8_t oldHalfTrack = mech.getHalfTrack();

        // Apply stepper transition exactly once.
        mech.updateStepperPhase(newPhase);

        if (mech.getHalfTrack() != oldHalfTrack)
        {
            // Preserve an outgoing modified track.
            mech.cacheActiveTrack();

            mech.setGCRValid(false);
            mech.setSyncDetected(false);
            mech.setByteReady(false);
        }
    }
}

bool D4040::flushMechanism(uint8_t unit)
{
    if (unit >= mechanisms.size())
        return false;

    D4040Mechanism& mech = mechanisms[unit];

    if (!mech.hasDisk())
        return true;

    const bool hasGCRWrites = mech.hasUncommittedGCR();

    // No pending changes.
    if (!hasGCRWrites && !mech.getDiskImage()->isDirty())
        return true;

    if (mech.getDiskPath().empty() || mech.isWriteProtected())
        return false;

    // Only sector-backed Commodore images are
    // supported by this writeback path.
    CBMImage* image = dynamic_cast<CBMImage*>(mech.getDiskImage());

    if (!image)
        return false;

    if (hasGCRWrites)
    {
        std::vector<DecodedGCRSector> changes;

        // All dirty tracks must decode successfully
        // before modifying the disk image.
        if (!collectModifiedGCRSectors(unit, changes))
        {
            std::cerr
                << "[D4040] GCR decode failed for unit "
                << static_cast<int>(unit) << '\n';

            return false;
        }

        // Don't silently discard raw GCR changes
        // that cannot be represented as D64 sectors.
        if (changes.empty() && !image->isDirty())
        {
            std::cerr
                << "[D4040] Raw GCR changes have no "
                   "sector differences; refusing flush\n";

            return false;
        }

        // Keep original sectors for rollback if a
        // sector update fails before file saving.
        struct SectorBackup
        {
            uint8_t track;
            uint8_t sector;
            std::vector<uint8_t> data;
        };

        std::vector<SectorBackup> backups;
        backups.reserve(changes.size());

        try
        {
            for (const auto& sector : changes)
            {
                auto original = image->readSector(sector.track, sector.sector);

                if (original.size() != 256)
                    return false;

                backups.push_back({sector.track, sector.sector, std::move(original)});
            }

            const bool wasDirty = image->isDirty();

            size_t applied = 0;

            try
            {
                for (const auto& sector : changes)
                {
                    std::vector<uint8_t> data(sector.data.begin(), sector.data.end());

                    if (!image->writeSector(sector.track, sector.sector, data))
                        break;

                    ++applied;
                }
            }
            catch (const std::exception&)
            {
                // Fall through to rollback below.
            }

            if (applied != changes.size())
            {
                for (size_t i = 0; i < applied; ++i)
                    image->writeSector(backups[i].track, backups[i].sector, backups[i].data);

                if (!wasDirty)
                    image->clearDirty();

                std::cerr
                    << "[D4040] Sector writeback failed "
                       "for unit "
                    << static_cast<int>(unit) << '\n';

                return false;
            }
        }
        catch (const std::exception& e)
        {
            std::cerr
                << "[D4040] GCR writeback error: "
                << e.what() << '\n';
            return false;
        }
    }

    // Save the sector-backed image to disk.
    if (image->isDirty())
    {
        if (!image->saveDisk(mech.getDiskPath()))
        {
            std::cerr
                << "[D4040] Failed saving unit "
                << static_cast<int>(unit)
                << ": " << mech.getDiskPath() << '\n';

            // Keep dirty GCR state for retry.
            return false;
        }

        image->clearDirty();
    }

    // Only release the dirty-track cache after
    // successful persistence.
    if (hasGCRWrites)
        mech.clearDirtyTrackCache();

    return true;
}

void D4040::updateControllerInputs()
{
    MOS6530& rriot = d4040mem.getRRIOT();

    const uint8_t unit = getSelectedMechanism();
    const D4040Mechanism& mech = mechanisms[unit];

    // Default external inputs are high.
    uint8_t pb = 0xFF;

    // PB3: write protection, active high.
    // Treat an absent disk as protected.
    if (mech.hasDisk() && !mech.isWriteProtected())
        pb &= static_cast<uint8_t>(~0x08);

    // PB6: SYNC detection, active low.
    if (mech.hasDisk() && mech.isSyncDetected())
        pb &= static_cast<uint8_t>(~0x40);

    rriot.setPortBInput(pb);
}

bool D4040::rebuildGCRTrack(uint8_t unit)
{
    if (unit >= mechanisms.size())
        return false;

    D4040Mechanism& mech = mechanisms[unit];

    // Preserve any unsaved write data before rebuilding.
    mech.cacheActiveTrack();

    // Invalidate the previous stream.
    mech.getGCRTrack().clear();
    mech.setGCRPosition(0);
    mech.setGCRValid(false);
    mech.setLoadedGCRTrack(0);
    mech.setSyncDetected(false);
    mech.setByteReady(false);

    if (!mech.hasDisk())
        return false;

    CBMImage* image = dynamic_cast<CBMImage*>(mech.getDiskImage());

    if (!image)
        return false;

    // D64 contains complete sectors on whole tracks.
    // Do not pretend an off-track head is aligned.
    if ((mech.getHalfTrack() & 1) != 0)
        return false;

    const uint8_t track = static_cast<uint8_t>(mech.getHalfTrack() / 2);

    if (track < 1 || track > 35)
        return false;

    // Prefer previously modified track data over
    // regenerating the track from the D64.
    if (mech.restoreCachedTrack(track))
        return true;

    const int sectors = gcrCodec.sectorsPerTrack1541(track);

    // Read disk ID from BAM on track 18, sector 0.
    const auto bam = image->readSector(18, 0);

    if (bam.size() != 256)
        return false;

    const uint8_t id1 = bam[0xA2];
    const uint8_t id2 = bam[0xA3];

    auto& data = mech.getGCRTrack().getTrackData();
    auto& sync = mech.getGCRTrack().getSyncMap();

    // Append bytes with sync metadata.
    auto appendBytes = [&](uint8_t value, size_t count, bool isSync)
    {
        data.insert(data.end(), count, value);
        sync.insert(sync.end(), count, isSync ? 1 : 0);
    };

    // Encode four bytes as five GCR bytes.
    auto appendEncoded = [&](const uint8_t* bytes, size_t length)
    {
        for (size_t i = 0; i < length; i += 4)
        {
            uint8_t encoded[5];

            gcrCodec.encode4Bytes(bytes + i, encoded);

            data.insert(data.end(), encoded, encoded + 5);
            sync.insert(sync.end(), 5, 0);
        }
    };

    constexpr int SYNC_LEN = 10;
    constexpr int HEADER_GAP = 9;
    constexpr int DATA_GAP = 9;

    appendBytes(0x55, 64, false);

    for (int sector = 0; sector < sectors; ++sector)
    {
        const auto sectorData = image->readSector(track, static_cast<uint8_t>(sector));

        if (sectorData.size() != 256)
        {
            mech.getGCRTrack().clear();
            return false;
        }

        // Sector header.
        appendBytes(0xFF, SYNC_LEN, true);

        uint8_t header[8] = {};

        header[0] = 0x08;
        header[2] = static_cast<uint8_t>(sector);
        header[3] = track;
        header[4] = id2;
        header[5] = id1;
        header[6] = 0x0F;
        header[7] = 0x0F;

        header[1] = static_cast<uint8_t>(header[2] ^ header[3] ^ header[4] ^ header[5]);

        appendEncoded(header, sizeof(header));
        appendBytes(0x55, HEADER_GAP, false);

        // Sector data block.
        appendBytes(0xFF, SYNC_LEN, true);

        uint8_t block[260] = {};

        block[0] = 0x07;

        uint8_t checksum = 0;

        for (size_t i = 0; i < 256; ++i)
        {
            block[i + 1] = sectorData[i];
            checksum ^= sectorData[i];
        }

        block[257] = checksum;

        appendEncoded(block, sizeof(block));
        appendBytes(0x55, DATA_GAP, false);
    }

    appendBytes(0x55, 128, false);

    mech.setLoadedGCRTrack(track);
    mech.setGCRValid(!mech.getGCRTrack().empty() && sync.size() == data.size());

    // A newly generated GCR stream represents
    // the currently mounted D64 sector data.
    mech.setGCRDirty(false);
    mech.resetGCRWriteStatistics();

    return mech.isGCRValid();
}

void D4040::advanceGCR(uint32_t cycles)
{
    const uint8_t selectedUnit = getSelectedMechanism();

    D4040VIA& via = d4040mem.getVIA();

    // MOS6530 Port B:
    // PB1-PB2 select the controller's GCR density.
    const MOS6530& rriot = d4040mem.getRRIOT();

    const uint8_t densityDDR = rriot.getDDRB();
    const uint8_t densityOutput = rriot.getPortBOutput();

    // Use firmware-selected density when both
    // density control pins are configured as outputs.
    const bool densityConfigured = (densityDDR & 0x06) == 0x06;

    const uint8_t controllerDensity = static_cast<uint8_t>((densityOutput >> 1) & 0x03);


    for (uint8_t unit = 0; unit < 2; ++unit)
    {
        D4040Mechanism& mech = mechanisms[unit];

        // Advance the mechanism's physical spindle state.
        mech.advanceMotor(cycles);

        if (!mech.isMotorRunning() || !mech.hasDisk())
        {
            mech.setGCRCycleAccumulator(0);
            mech.setSyncDetected(false);
            mech.setByteReady(false);

            if (unit == selectedUnit)
            {
                via.clearMechBytePending();
                via.setSyncDetected(false);
            }

            mech.resetGCRDecoder();

            continue;
        }

        const uint8_t physicalTrack = static_cast<uint8_t>(mech.getHalfTrack() / 2);

        if ((mech.getHalfTrack() & 1) != 0)
        {
            // The head is between whole tracks.
            // No valid D64 sector track is available.
            mech.setGCRValid(false);
            mech.setGCRCycleAccumulator(0);
            mech.setSyncDetected(false);
            mech.setByteReady(false);

            if (unit == selectedUnit)
                via.clearMechBytePending();

            mech.resetGCRDecoder();

            continue;
        }

        // Rebuild only if the stream is invalid or
        // represents a different track.
        if (!mech.isGCRValid() || mech.getLoadedGCRTrack() != physicalTrack)
        {
            if (!rebuildGCRTrack(unit))
            {
                mech.setGCRCycleAccumulator(0);
                mech.setSyncDetected(false);
                mech.setByteReady(false);
                continue;
            }
        }

        const size_t trackSize = mech.getGCRTrack().size();

        if (trackSize == 0)
            continue;

        // Default density based on the physical track.
        uint8_t density = (physicalTrack <= 17) ? 3 : (physicalTrack <= 24) ? 2 : (physicalTrack <= 30) ? 1 : 0;

        // The 4040 has one shared disk controller.
        //
        // When this mechanism is selected, use the
        // density programmed through MOS6530 PB1-PB2.
        if (unit == selectedUnit && densityConfigured)
            density = controllerDensity;

        // Approximate emulated cycles per GCR byte.
        // Density 0 = slowest, density 3 = fastest.
        //
        // The exact 4040 controller clock divisors
        // still need verification against the firmware.
        static constexpr uint32_t cyclesPerByte[4] = { 32, 30, 28, 26 };

        const uint32_t interval = cyclesPerByte[density];

        mech.setGCRCycleAccumulator(mech.getGCRCycleAccumulator() + cycles);

        const auto& data = mech.getGCRTrack().getTrackData();
        const auto& sync = mech.getGCRTrack().getSyncMap();

        while (mech.getGCRCycleAccumulator() >= interval)
        {
            mech.setGCRCycleAccumulator(mech.getGCRCycleAccumulator() - interval);

            const size_t pos = mech.getGCRPosition() % trackSize;
            const uint8_t byte = data[pos];
            const bool inSync = pos < sync.size() && sync[pos] != 0;

            // Advance the rotational position.
            mech.setGCRPosition((pos + 1) % trackSize);

            // Only the selected mechanism can exchange
            // data with the controller.
            if (unit != selectedUnit)
            {
                mech.setSyncDetected(inSync);
                mech.setByteReady(!inSync);
                mech.setReadLatch(byte);
                continue;
            }

            // -----------------------------------------
            // Disk write mode
            // -----------------------------------------
            if (via.isDiskWriteMode())
            {
                // No incoming read data while writing.
                mech.setSyncDetected(false);
                mech.setByteReady(false);

                via.clearMechBytePending();
                via.setSyncDetected(false);

                // MOS6530 Port A is the GCR output data latch.
                const MOS6530& rriot = d4040mem.getRRIOT();

                const bool writeSync = via.isSyncFillMode();

                const uint8_t writeByte = writeSync ? 0xFF : rriot.getPortAOutput();

                if (mech.writeGCRByte(pos, writeByte, writeSync))
                {
                    mech.setWriteLatch(writeByte);

                    // Notify the controller that another
                    // disk-byte interval has completed.
                    via.pulseWriteByteReady();
                }

                continue;
            }

            // -----------------------------------------
            // Disk read mode
            // -----------------------------------------
            if (via.isDiskReadMode())
            {
                // -------------------------------------
                // SYNC detection
                // -------------------------------------
                if (inSync)
                {
                    mech.setSyncDetected(true);
                    mech.setByteReady(false);

                    // A SYNC field establishes a new GCR
                    // decoding boundary.
                    mech.resetGCRDecoder();

                    // Report SYNC to the controller.
                    via.diskByteFromMedia(0xFF, true);

                    continue;
                }

                mech.setSyncDetected(false);
                mech.setByteReady(false);

                // -------------------------------------
                // Decode incoming GCR byte
                // -------------------------------------
                uint8_t decodedByte = 0;

                if (mech.decodeGCRByte(byte, gcrCodec, decodedByte))
                {
                    mech.setReadLatch(decodedByte);
                    mech.setByteReady(true);

                    // Present decoded data and generate
                    // the controller's byte-ready signal.
                    via.diskByteFromMedia(decodedByte, false);
                }
                else
                    via.setSyncDetected(false);
            }
            else
            {
                // Controller is not actively reading.
                mech.setSyncDetected(false);
                mech.setByteReady(false);

                // Discard any partially decoded GCR group.
                mech.resetGCRDecoder();

                via.clearMechBytePending();
                via.setSyncDetected(false);
            }
        }
    }
}

bool D4040::decodeGCRTrack(const GCRTrackStream& stream, uint8_t track, std::vector<DecodedGCRSector>& sectors)
{
    sectors.clear();

    if (track < 1 || track > 35)
        return false;

    const auto& data = stream.getTrackData();
    const auto& sync = stream.getSyncMap();

    const size_t size = data.size();

    if (size < 325 || sync.size() != size)
        return false;

    const int sectorCount = gcrCodec.sectorsPerTrack1541(track);

    std::vector<bool> seen(static_cast<size_t>(sectorCount), false);

    // Read a section of the circular GCR track.
    auto readBytes = [&](size_t position, uint8_t* output, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
            output[i] = data[(position + i) % size];
    };

    // Circular distance between two positions.
    auto distance = [&](size_t from, size_t to)
    {
        return (to + size - from) % size;
    };

    // Find the starts of all sync regions.
    std::vector<size_t> syncStarts;

    for (size_t i = 0; i < size; ++i)
    {
        const size_t previous = (i + size - 1) % size;

        if (sync[i] && !sync[previous])
            syncStarts.push_back(i);
    }

    if (syncStarts.size() < 2)
        return false;

    // Attempt to decode a sector header immediately
    // following each sync region.
    for (size_t index = 0;
         index < syncStarts.size(); ++index)
    {
        const size_t start = syncStarts[index];

        size_t headerPos = start;

        // Skip the sync region.
        while (sync[headerPos])
        {
            headerPos = (headerPos + 1) % size;

            if (headerPos == start)
                return false;
        }

        // The next sync region must not overlap
        // the encoded header.
        const size_t nextSync = syncStarts[(index + 1) % syncStarts.size()];

        if (distance(headerPos, nextSync) < 10)
            continue;

        uint8_t encodedHeader[10] = {};
        uint8_t header[8] = {};

        readBytes(headerPos, encodedHeader, 10);

        if (!gcrCodec.decode5Bytes(encodedHeader, header) || !gcrCodec.decode5Bytes(encodedHeader + 5, header + 4))
            continue;

        // Commodore sector header marker.
        if (header[0] != 0x08)
            continue;

        const uint8_t sectorNumber = header[2];
        const uint8_t trackNumber = header[3];

        const uint8_t headerChecksum = header[2] ^ header[3] ^ header[4] ^ header[5];

        if (header[1] != headerChecksum || trackNumber != track || sectorNumber >= sectorCount)
            continue;

        // The next sync region should precede
        // the associated sector data block.
        size_t dataPos = nextSync;

        while (sync[dataPos])
        {
            dataPos = (dataPos + 1) % size;

            if (dataPos == nextSync)
                return false;
        }

        const size_t followingSync = syncStarts[(index + 2) % syncStarts.size()];

        // Encoded data block must fit before
        // the following sync field.
        if (distance(dataPos, followingSync) < 325)
            continue;

        std::array<uint8_t, 260> block{};

        bool valid = true;

        for (size_t i = 0; i < 65; ++i)
        {
            uint8_t encoded[5] = {};

            readBytes((dataPos + i * 5) % size, encoded, 5);

            if (!gcrCodec.decode5Bytes(encoded, block.data() + i * 4))
            {
                valid = false;
                break;
            }
        }

        if (!valid || block[0] != 0x07)
            continue;

        // Validate the 256-byte sector checksum.
        uint8_t checksum = 0;

        for (size_t i = 0; i < 256; ++i)
            checksum ^= block[i + 1];

        if (checksum != block[257])
            continue;

        // Reject duplicate sector IDs.
        if (seen[sectorNumber])
        {
            sectors.clear();
            return false;
        }

        DecodedGCRSector decoded;
        decoded.track = track;
        decoded.sector = sectorNumber;

        std::copy_n(block.begin() + 1, 256, decoded.data.begin());

        sectors.push_back(decoded);
        seen[sectorNumber] = true;
    }

    // Require a complete track before declaring
    // it suitable for further processing.
    if (sectors.size() != static_cast<size_t>(sectorCount))
    {
        sectors.clear();
        return false;
    }

    return true;
}

bool D4040::getModifiedGCRSectors(uint8_t unit, uint8_t track, std::vector<DecodedGCRSector>& modifiedSectors)
{
    modifiedSectors.clear();

    if (unit >= mechanisms.size())
        return false;

    D4040Mechanism& mech = mechanisms[unit];

    if (!mech.hasDisk())
        return false;

    // This implementation supports complete D64 tracks.
    if (track < 1 || track > 35)
        return false;

    CBMImage* image = dynamic_cast<CBMImage*>(mech.getDiskImage());

    if (!image)
        return false;

    // Determine which GCR stream represents
    // the requested track.
    const GCRTrackStream* stream = nullptr;

    // Prefer the active track, since it can contain
    // newer modifications than the cached copy.
    if (mech.isGCRValid() && mech.getLoadedGCRTrack() == track)
        stream = &mech.getGCRTrack();
    else
    {
        if (const GCRTrackStream* cached = mech.findDirtyTrack(track))
            stream = cached;
    }

    if (!stream)
        return false;

    // Decode the selected GCR stream.
    std::vector<DecodedGCRSector> decoded;

    if (!decodeGCRTrack(*stream, track, decoded))
        return false;

    // Stage differences locally. Nothing is returned
    // until every decoded sector has been checked.
    std::vector<DecodedGCRSector> changes;

    for (const auto& sector : decoded)
    {
        // Defensive validation.
        if (sector.track != track)
            return false;

        const std::vector<uint8_t> original = image->readSector(sector.track, sector.sector);

        // A valid sector must contain exactly 256 bytes.
        if (original.size() != 256)
            return false;

        const bool different = !std::equal(sector.data.begin(), sector.data.end(), original.begin());

        if (different)
            changes.push_back(sector);
    }

    // Only publish changes after successful validation
    // of the entire track.
    modifiedSectors = std::move(changes);

    return true;
}

bool D4040::collectModifiedGCRSectors(uint8_t unit, std::vector<DecodedGCRSector>& modifiedSectors)
{
    modifiedSectors.clear();

    if (unit >= mechanisms.size())
        return false;

    D4040Mechanism& mech = mechanisms[unit];

    if (!mech.hasDisk())
        return false;

    std::vector<DecodedGCRSector> changes;

    // First process all cached dirty tracks.
    for (const auto& entry : mech.getDirtyTracks())
    {
        const uint8_t track = entry.first;

        std::vector<DecodedGCRSector> trackChanges;

        if (!getModifiedGCRSectors(unit, track, trackChanges))
            return false;

        changes.insert(changes.end(), trackChanges.begin(), trackChanges.end());
    }

    // The active track may also contain writes
    // that have not been copied into the cache.
    if (mech.isGCRDirty())
    {
        const uint8_t track = mech.getLoadedGCRTrack();

        // Avoid processing the active track twice
        // if a cached entry exists for it.
        if (!mech.findDirtyTrack(track))
        {
            std::vector<DecodedGCRSector> trackChanges;

            if (!getModifiedGCRSectors(unit, track, trackChanges))
                return false;

            changes.insert(changes.end(), trackChanges.begin(), trackChanges.end());
        }
    }

    modifiedSectors = std::move(changes);

    return true;
}

std::vector<DriveHardwareInfo> D4040::enumerateDriveHardware() const
{
    return
    {
        {
            0,
            DriveHardwareType::CPU,
            "DOS-6502",
            -1,
            false,
            0,
            0
        },
        {
            1,
            DriveHardwareType::CPU,
            "CONTROLLER-6504",
            -1,
            false,
            0,
            0
        },
        {
            2,
            DriveHardwareType::Bus,
            "DOS-BUS",
            0,
            false,
            0,
            0
        },
        {
            3,
            DriveHardwareType::Bus,
            "CONTROLLER-BUS",
            1,
            false,
            0,
            0
        },
        {
            4,
            DriveHardwareType::Memory,
            "DOS-MEMORY",
            0,
            false,
            0,
            0
        },
        {
            5,
            DriveHardwareType::Memory,
            "CONTROLLER-MEMORY",
            1,
            false,
            0,
            0
        },
        {
            6,
            DriveHardwareType::RIOT6532,
            "RIOT1-IEEE488",
            0,
            true,
            0x0200,
            0x20
        },
        {
            7,
            DriveHardwareType::RIOT6532,
            "RIOT2-IEEE488",
            0,
            true,
            0x0280,
            0x20
        },
        {
            8,
            DriveHardwareType::VIA6522,
            "CONTROLLER-VIA",
            1,
            true,
            0x0040,
            0x10
        },
        {
            9,
            DriveHardwareType::RRIOT6530,
            "CONTROLLER-RRIOT",
            1,
            true,
            0x0080,
            0x10
        },
        {
            10,
            DriveHardwareType::Mechanism,
            "FLOPPY-0",
            -1,
            false,
            0,
            0
        },
        {
            11,
            DriveHardwareType::Mechanism,
            "FLOPPY-1",
            -1,
            false,
            0,
            0
        }
    };
}

CPU* D4040::getHardwareCPU(uint32_t id)
{
    switch (id)
    {
        case 0:
            return &driveCPU;

        case 1:
            return &controllerCPU;

        default:
            return nullptr;
    }
}

CPUBus* D4040::getHardwareBus(uint32_t id)
{
    switch (id)
    {
        case 2:
            return &d4040Bus6502;

        case 3:
            return &d4040Bus6504;

        default:
            return nullptr;
    }
}

DriveVIABase* D4040::getHardwareVIA(uint32_t id)
{
    if (id == 8)
        return &d4040mem.getVIA();

    return nullptr;
}

MOS6532* D4040::getHardwareRIOT(uint32_t id)
{
    switch (id)
    {
        case 6:
            return &d4040mem.getRIOT1();

        case 7:
            return &d4040mem.getRIOT2();

        default:
            return nullptr;
    }
}

MOS6530* D4040::getHardwareRRIOT(uint32_t id)
{
    if (id == 9)
        return &d4040mem.getRRIOT();

    return nullptr;
}

bool D4040::getDriveCPUContext(uint32_t cpuId, DriveCPUContext& context)
{
    context = {};

    switch (cpuId)
    {
        case 0:
        {
            context.cpuId = 0;
            context.busId = 2;

            context.cpu = getHardwareCPU(0);
            context.bus = getHardwareBus(2);

            break;
        }

        case 1:
        {
            context.cpuId = 1;
            context.busId = 3;

            context.cpu = getHardwareCPU(1);
            context.bus = getHardwareBus(3);

            break;
        }

        default:
            return false;
    }

    return context.cpu != nullptr && context.bus != nullptr;
}

bool D4040::getHardwareMechanismDebug(uint32_t hardwareId, DriveMechanismDebugView& view) const
{
    uint32_t mechanismIndex = 0;

    switch (hardwareId)
    {
        case 10:
            mechanismIndex = 0;
            break;

        case 11:
            mechanismIndex = 1;
            break;

        default:
            return false;
    }

    const D4040Mechanism& mechanism = mechanisms[mechanismIndex];

    // Reset the output so no stale values remain.
    view = DriveMechanismDebugView{};

    // Identification
    view.mechanismId = mechanismIndex;
    view.name = mechanismIndex == 0 ? "FLOPPY-0" : "FLOPPY-1";

    // Media
    view.diskInserted = mechanism.hasDisk();
    view.diskPath = mechanism.getDiskPath();

    // Motor and head
    view.motorOn = mechanism.isMotorRunning();
    view.track = mechanism.getTrack();
    view.sector = mechanism.getSector();

    // Physical head position
    view.hasHalfTrack = true;
    view.halfTrack = mechanism.getHalfTrack();

    // Stepper
    view.hasStepperPhase = true;
    view.stepperPhase = mechanism.getStepperPhase();

    // Write protection
    view.hasWriteProtect = true;
    view.writeProtected = mechanism.isWriteProtected();

    // GCR state
    view.hasGCR = true;
    view.gcrValid = mechanism.isGCRValid();
    view.gcrPosition = mechanism.getGCRPosition();
    view.loadedGCRTrack = mechanism.getLoadedGCRTrack();

    // Disk signals
    view.hasDiskSignals = true;
    view.syncDetected = mechanism.isSyncDetected();
    view.byteReady = mechanism.isByteReady();

    // Data latches
    view.hasDataLatches = true;
    view.readLatch = mechanism.getReadLatch();
    view.writeLatch = mechanism.getWriteLatch();

    // Modified-track information
    view.hasDirtyState = true;
    view.gcrDirty = mechanism.hasUncommittedGCR();
    view.gcrBytesWritten = mechanism.getGCRBytesWritten();

    return true;
}

bool D4040::debugStepCPU(uint32_t cpuId)
{
    if (cpuId > 1)
        return false;

    // Advance one drive scheduler clock.
    // The selected CPU may not execute during this clock
    // because its previous instruction can still have
    // outstanding cycles in the scheduler.
    tick(1);

    return true;
}

DriveStepResult D4040::debugStepInstruction(uint32_t cpuId)
{
    if (cpuId > 1)
        return DriveStepResult::InvalidCPU;

    const uint64_t startCount = (cpuId == 0) ? dosInstructionsExecuted : controllerInstructionsExecuted;

    // Temporarily allow the selected CPU to step past
    // a breakpoint at its current instruction.
    debugSkipBreakpointCPU = static_cast<int32_t>(cpuId);

    constexpr uint32_t MAX_STEP_CYCLES = 100000;

    for (uint32_t i = 0; i < MAX_STEP_CYCLES; ++i)
    {
        const uint64_t currentCount = (cpuId == 0) ? dosInstructionsExecuted : controllerInstructionsExecuted;

        if (currentCount != startCount)
        {
            debugSkipBreakpointCPU = -1;
            return DriveStepResult::Completed;
        }

        // A breakpoint on the other CPU must still stop
        // the shared drive scheduler.
        if (isBreakpointHit())
        {
            debugSkipBreakpointCPU = -1;
            return DriveStepResult::Stalled;
        }

        tick(1);

        if (isBreakpointHit())
        {
            debugSkipBreakpointCPU = -1;
            return DriveStepResult::Stalled;
        }
    }

    debugSkipBreakpointCPU = -1;
    return DriveStepResult::Stalled;
}
