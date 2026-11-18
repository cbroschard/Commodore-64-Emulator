// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
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
    currentTrack    = 18;
    currentSector   = 0;

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

    controllerCycleBalance = 0;

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
    wrtr.writeU32(1); // Version
    wrtr.writeU8(static_cast<uint8_t>(deviceNumber));

    wrtr.endChunk();
}

bool D4040::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "4040", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))              { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                       { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
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
    int64_t remaining = static_cast<int64_t>(cycles);

    while (remaining > 0)
    {
        if (checkBreakpoint())
            return;

        // -----------------------------------------
        // Main DOS CPU (6502)
        // -----------------------------------------
        driveCPU.tick();

        uint32_t dc = driveCPU.getElapsedCycles();

        if (dc == 0)
            dc = 1;

        remaining -= static_cast<int64_t>(dc);

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
            // Make external status inputs visible before
            // the next controller instruction.
            updateControllerInputs();

            controllerCPU.tick();

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
    // Drive 6502 Interrupt
    const bool riotIRQ = d4040mem.getRIOT1().isIRQActive() || d4040mem.getRIOT2().isIRQActive();

    if (riotIRQ)
        driveIRQ.raiseIRQ(IRQLine::D4040_IRQ);
    else
        driveIRQ.clearIRQ(IRQLine::D4040_IRQ);

     // Controller 6504: VIA interrupt
    const bool viaIRQ = d4040mem.getVIA().checkIRQActive();

    if (viaIRQ)
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
    // Drive::insert() has already loaded the image.
    if (!diskImage)
        throw std::runtime_error("D4040: disk image was not loaded");

    D4040Mechanism& mech = mechanisms[0];

    if (!flushMechanism(0))
    {
        throw std::runtime_error(
            "D4040: cannot replace unit 0; previous disk save failed");
    }

    mech.diskImage = std::move(diskImage);

    mech.diskInserted = true;
    mech.diskPath = path;
    mech.writeProtected = mech.diskImage->isWriteProtected();

    mech.syncDetected = false;
    mech.byteReady = false;
    mech.readLatch = 0xFF;
    mech.writeLatch = 0xFF;

    mech.gcrTrack.clear();
    mech.gcrPos = 0;
    mech.loadedGCRTrack = 0;
    mech.gcrValid = false;
    mech.gcrCycleAccumulator = 0;

    lastError = DriveError::NONE;
    status = DriveStatus::READY;
}

void D4040::unloadDisk()
{
    if (!tryUnloadAllDisks())
    {
        std::cerr << "[D4040] Failed to eject disks\n";
        return;
    }

    diskImage.reset();
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

    // Save any changes on the old disk first.
    if (!flushMechanism(unit))
    {
        std::cerr << "[D4040] Cannot replace disk in unit "
                  << static_cast<int>(unit)
                  << ": previous disk save failed\n";
        return false;
    }

    mech.diskImage = std::move(img);

    mech.diskInserted = true;
    mech.diskPath = path;
    mech.writeProtected = mech.diskImage->isWriteProtected();

    // Reset media-dependent controller state.
    mech.syncDetected = false;
    mech.byteReady = false;
    mech.readLatch = 0xFF;
    mech.writeLatch = 0xFF;

    mech.gcrTrack.clear();
    mech.gcrPos = 0;
    mech.loadedGCRTrack = 0;
    mech.gcrValid = false;
    mech.gcrCycleAccumulator = 0;

    lastError = DriveError::NONE;
    status = DriveStatus::READY;

    std::cout << "[D4040] Mounted disk in unit "
              << static_cast<int>(unit)
              << ": " << path << '\n';

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

    return mechanisms[unit].diskImage != nullptr;
}

std::string D4040::getDiskPath(uint8_t unit) const
{
    if (unit >= mechanisms.size())
        return {};

    return mechanisms[unit].diskPath;
}

void D4040::startMotor()
{
    mechanisms[0].motorOn = true;
}

void D4040::stopMotor()
{
    mechanisms[0].motorOn = false;
}

bool D4040::tryUnloadDisk(uint8_t unit)
{
    if (unit >= mechanisms.size())
        return false;

    // Refuse ejection when pending changes cannot be saved.
    if (!flushMechanism(unit))
        return false;

    D4040Mechanism& mech = mechanisms[unit];

    mech.diskImage.reset();
    mech.diskInserted = false;
    mech.diskPath.clear();
    mech.writeProtected = false;

    mech.syncDetected = false;
    mech.byteReady = false;
    mech.readLatch = 0xFF;
    mech.writeLatch = 0xFF;

    mech.gcrTrack.clear();
    mech.gcrPos = 0;
    mech.loadedGCRTrack = 0;
    mech.gcrValid = false;
    mech.gcrCycleAccumulator = 0;

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

void D4040::flushAndSaveDisk()
{
    const bool unit0Saved = flushMechanism(0);
    const bool unit1Saved = flushMechanism(1);

    if (!unit0Saved || !unit1Saved)
    {
        std::cerr
            << "[D4040] WARNING: One or more disk "
               "images could not be saved\n";
    }
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

    mechanisms[0].motorOn = signals.motor0On;
    mechanisms[1].motorOn = signals.motor1On;

    for (uint8_t unit = 0; unit < 2; ++unit)
    {
        D4040Mechanism& mech = mechanisms[unit];

        const uint8_t oldHalfTrack = mech.halfTrack;
        const uint8_t newPhase = (unit == 0) ? signals.stepper0 : signals.stepper1;

        mech.updateStepperPhase(newPhase);

        if (mech.halfTrack != oldHalfTrack)
        {
            mech.gcrValid = false;
            mech.syncDetected = false;
            mech.byteReady = false;
        }
    }
}

bool D4040::flushMechanism(uint8_t unit)
{
    if (unit >= mechanisms.size())
        return false;

    D4040Mechanism& mech = mechanisms[unit];

    if (!mech.diskImage)
        return true;

    // No writes to persist.
    if (!mech.diskImage->isDirty())
        return true;

    if (mech.diskPath.empty())
    {
        std::cerr << "[D4040] Cannot save unit "
                  << static_cast<int>(unit)
                  << ": missing disk path\n";
        return false;
    }

    if (mech.writeProtected)
    {
        std::cerr << "[D4040] Cannot save unit "
                  << static_cast<int>(unit)
                  << ": disk is write protected\n";
        return false;
    }

    if (!mech.diskImage->saveDisk(mech.diskPath))
    {
        std::cerr << "[D4040] Failed saving unit "
                  << static_cast<int>(unit)
                  << ": " << mech.diskPath << '\n';
        return false;
    }

    mech.diskImage->clearDirty();

    std::cout << "[D4040] Saved unit "
              << static_cast<int>(unit)
              << ": " << mech.diskPath << '\n';

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
    if (mech.diskImage && !mech.writeProtected)
        pb &= static_cast<uint8_t>(~0x08);

    // PB6: SYNC detection, active low.
    if (mech.diskImage && mech.syncDetected)
        pb &= static_cast<uint8_t>(~0x40);

    rriot.setPortBInput(pb);
}

bool D4040::rebuildGCRTrack(uint8_t unit)
{
    if (unit >= mechanisms.size())
        return false;

    D4040Mechanism& mech = mechanisms[unit];

    // Invalidate the previous stream.
    mech.gcrTrack.clear();
    mech.gcrPos = 0;
    mech.gcrValid = false;
    mech.loadedGCRTrack = 0;
    mech.syncDetected = false;
    mech.byteReady = false;

    if (!mech.diskImage)
        return false;

    CBMImage* image = dynamic_cast<CBMImage*>(mech.diskImage.get());

    if (!image)
        return false;

    // D64 contains complete sectors on whole tracks.
    // Do not pretend an off-track head is aligned.
    if ((mech.halfTrack & 1) != 0)
        return false;

    const uint8_t track = static_cast<uint8_t>(mech.halfTrack / 2);

    if (track < 1 || track > 35)
        return false;

    const int sectors = gcrCodec.sectorsPerTrack1541(track);

    // Read disk ID from BAM on track 18, sector 0.
    const auto bam = image->readSector(18, 0);

    if (bam.size() != 256)
        return false;

    const uint8_t id1 = bam[0xA2];
    const uint8_t id2 = bam[0xA3];

    auto& data = mech.gcrTrack.getTrackData();
    auto& sync = mech.gcrTrack.getSyncMap();

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
            mech.gcrTrack.clear();
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

    mech.loadedGCRTrack = track;
    mech.gcrValid = !mech.gcrTrack.empty() && sync.size() == data.size();

    // A newly generated GCR stream represents
    // the currently mounted D64 sector data.
    mech.gcrDirty = false;
    mech.gcrBytesWritten = 0;

    return mech.gcrValid;
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

        // A stopped motor cannot produce disk bytes.
        if (!mech.motorOn || !mech.diskImage)
        {
            mech.gcrCycleAccumulator = 0;
            mech.syncDetected = false;
            mech.byteReady = false;
            continue;
        }

        const uint8_t physicalTrack = static_cast<uint8_t>(mech.halfTrack / 2);

        if ((mech.halfTrack & 1) != 0)
        {
            // The head is between whole tracks.
            // No valid D64 sector track is available.
            mech.gcrValid = false;
            mech.gcrCycleAccumulator = 0;
            mech.syncDetected = false;
            mech.byteReady = false;

            if (unit == selectedUnit)
                via.clearMechBytePending();

            continue;
        }

        // Rebuild only if the stream is invalid or
        // represents a different track.
        if (!mech.gcrValid || mech.loadedGCRTrack != physicalTrack)
        {
            if (!rebuildGCRTrack(unit))
            {
                mech.gcrCycleAccumulator = 0;
                mech.syncDetected = false;
                mech.byteReady = false;
                continue;
            }
        }

        const size_t trackSize = mech.gcrTrack.size();

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

        mech.gcrCycleAccumulator += cycles;

        const auto& data = mech.gcrTrack.getTrackData();
        const auto& sync = mech.gcrTrack.getSyncMap();

        while (mech.gcrCycleAccumulator >= interval)
        {
            mech.gcrCycleAccumulator -= interval;

            const size_t pos = mech.gcrPos % trackSize;
            const uint8_t byte = data[pos];
            const bool inSync = pos < sync.size() && sync[pos] != 0;

            // Advance the rotational position.
            mech.gcrPos = (pos + 1) % trackSize;

            // Only the selected mechanism can exchange
            // data with the controller.
            if (unit != selectedUnit)
            {
                mech.syncDetected = inSync;
                mech.byteReady = !inSync;
                mech.readLatch = byte;
                continue;
            }

            // -----------------------------------------
            // Disk write mode
            // -----------------------------------------
            if (via.isDiskWriteMode())
            {
                // No incoming read data while writing.
                mech.syncDetected = false;
                mech.byteReady = false;

                via.clearMechBytePending();
                via.setSyncDetected(false);

                // MOS6530 Port A is the GCR output data latch.
                const MOS6530& rriot = d4040mem.getRRIOT();

                const bool writeSync = via.isSyncFillMode();

                const uint8_t writeByte = writeSync ? 0xFF : rriot.getPortAOutput();

                // Record data or sync at the current
                // rotational position.
                if (mech.writeGCRByte(pos, writeByte, writeSync))
                    mech.writeLatch = writeByte;

                continue;
            }

            // -----------------------------------------
            // Disk read mode
            // -----------------------------------------
            if (via.isDiskReadMode())
            {
                mech.syncDetected = inSync;
                mech.byteReady = !inSync;
                mech.readLatch = byte;

                via.diskByteFromMedia(byte, inSync);
            }
            else
            {
                // Controller is not actively reading.
                mech.syncDetected = false;
                mech.byteReady = false;

                via.clearMechBytePending();
                via.setSyncDetected(false);
            }
        }
    }
}
