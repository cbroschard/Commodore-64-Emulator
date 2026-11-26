// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <iostream>
#include "Drive/Drive.h"

Drive::Drive() :
    currentDriveError(DriveError::NONE),
    currentDriveStatus(DriveStatus::IDLE),
    diskImage(),
    breakpointHit(false),
    breakpointHitAddress(0),
    breakpointHitCPU(0),
    watchpointHit(false),
    watchpointHitAddress(0),
    watchpointHitType(DriveWatchpoint::Type::Read),
    watchpointHitValue(0x00)
{

}

Drive::~Drive() = default;

bool Drive::insert(const std::string &path)
{
    // Detect disk format
    auto fmt = DiskFactory::detectFormat(path);

    // Ask the concrete drive (1571, etc.) if it supports this format
    if (!canMount(fmt))
    {
        currentDriveError  = DriveError::NO_DISK;
        currentDriveStatus = DriveStatus::IDLE;
        std::cerr << "Drive: format not supported for '" << path << "'\n";
        return false;
    }

    // Create disk implementation
    std::unique_ptr<Disk> d = DiskFactory::create(path);
    if (!d)
    {
        currentDriveError  = DriveError::NO_DISK;
        currentDriveStatus = DriveStatus::IDLE;
        std::cerr << "Drive: no disk implementation for '" << path << "'\n";
        return false;
    }

    // Load backing file
    if (!d->loadDisk(path))
    {
        currentDriveError  = DriveError::READ_ERROR;
        currentDriveStatus = DriveStatus::IDLE;
        std::cerr << "Drive: failed to load disk image '" << path << "'\n";
        return false;
    }

    // Take ownership
    diskImage = std::move(d);

    // Let concrete drive sync its internal state (track, side, etc.)
    try
    {
        loadDisk(path);
    }
    catch (...)
    {
        currentDriveError  = DriveError::READ_ERROR;
        currentDriveStatus = DriveStatus::IDLE;
        std::cerr << "Drive: exception while mounting disk '" << path << "'\n";
        return false;
    }

    currentDriveError  = DriveError::NONE;
    currentDriveStatus = DriveStatus::READY;
    return true;
}

void Drive::addBreakpoint(uint16_t address)
{
    addBreakpoint(0, address);
}

void Drive::addBreakpoint(uint16_t address, DriveBreakpoint::Condition condition, uint8_t value)
{
    addBreakpoint(0, address, condition, value);
}

void Drive::addBreakpoint(uint32_t cpuId, uint16_t address)
{
    addBreakpoint(cpuId, address, DriveBreakpoint::Condition::None, 0);
}

void Drive::addBreakpoint(uint32_t cpuId, uint16_t address, DriveBreakpoint::Condition condition, uint8_t value)
{
    // Prevent duplicate breakpoints with the same CPU,
    // address, condition, and value.
    for (const auto& bp : breakpoints)
    {
        if (bp.cpuId == cpuId && bp.address == address && bp.condition == condition && bp.value == value)
            return;
    }

    breakpoints.push_back({address, condition, value, cpuId});
}

void Drive::removeBreakpoint(uint16_t address)
{
    removeBreakpoint(0, address);
}

void Drive::removeBreakpoint(uint32_t cpuId, uint16_t address)
{
    breakpoints.erase(std::remove_if(breakpoints.begin(), breakpoints.end(), [cpuId, address](const DriveBreakpoint& bp)
    {
        return bp.cpuId == cpuId && bp.address == address;
    }),
    breakpoints.end());
}

void Drive::clearBreakpoints()
{
    breakpoints.clear();

    breakpointHit = false;
    breakpointHitAddress = 0;
    breakpointHitCPU = 0;
}

void Drive::clearBreakpointHit()
{
    breakpointHit = false;
    breakpointHitAddress = 0;
    breakpointHitCPU = 0;
}

bool Drive::hasBreakpoint(uint16_t address) const
{
    return hasBreakpoint(0, address);
}

bool Drive::hasBreakpoint(uint32_t cpuId, uint16_t address) const
{
    return std::any_of(breakpoints.begin(), breakpoints.end(), [cpuId, address](const DriveBreakpoint& bp)
    {
        return bp.cpuId == cpuId && bp.address == address;
    });
}

void Drive::addWatchpoint(DriveWatchpoint::Type type, uint16_t address)
{
    if (hasWatchpoint(type, address))
        return;

    watchpoints.push_back({type, address});
}

void Drive::removeWatchpoint(DriveWatchpoint::Type type, uint16_t address)
{
    watchpoints.erase(std::remove_if(watchpoints.begin(), watchpoints.end(), [type, address](const DriveWatchpoint& wp)
            { return wp.type == type && wp.address == address; }), watchpoints.end());
}

void Drive::clearWatchpointHit()
{
    watchpointHit = false;
    watchpointHitAddress = 0;
    watchpointHitType = DriveWatchpoint::Type::Read;
    watchpointHitValue = 0x00;
}

void Drive::clearWatchpoints()
{
    watchpoints.clear();

    // Also clear any stale hit state.
    watchpointHit = false;
    watchpointHitAddress = 0;
    watchpointHitType = DriveWatchpoint::Type::Read;
    watchpointHitValue = 0x00;
}

bool Drive::hasWatchpoint(DriveWatchpoint::Type type, uint16_t address) const
{
    return std::any_of(watchpoints.begin(), watchpoints.end(), [type, address](const DriveWatchpoint& wp)
        { return wp.type == type && wp.address == address; });
}

void Drive::checkWatchRead(uint16_t address, uint8_t value)
{
    if (!hasWatchpoint(DriveWatchpoint::Type::Read, address))
        return;

    watchpointHit = true;
    watchpointHitAddress = address;
    watchpointHitType = DriveWatchpoint::Type::Read;
    watchpointHitValue = value;
}

void Drive::checkWatchWrite(uint16_t address, uint8_t value)
{
    if (!hasWatchpoint(DriveWatchpoint::Type::Write, address))
        return;

    watchpointHit = true;
    watchpointHitAddress = address;
    watchpointHitType = DriveWatchpoint::Type::Write;
    watchpointHitValue = value;
}

bool Drive::checkBreakpoint()
{
    return checkBreakpoint(0);
}

bool Drive::checkBreakpoint(uint32_t cpuId)
{
    CPU* cpu = getHardwareCPU(cpuId);

    // Compatibility for older drives that do not yet
    // expose their CPU through DriveHardware.
    if (!cpu && cpuId == 0 && enumerateDriveHardware().empty())
        cpu = getDriveCPU();

    if (!cpu)
        return false;

    // A breakpoint should only stop execution when
    // the CPU is ready to execute a new instruction.
    if (!cpu->isAtInstructionBoundary())
        return false;

    const auto state = cpu->getState();

    for (const DriveBreakpoint& bp : breakpoints)
    {
        if (bp.cpuId != cpuId)
            continue;

        if (bp.address != state.PC)
            continue;

        bool matched = false;

        switch (bp.condition)
        {
            case DriveBreakpoint::Condition::None:
                matched = true;
                break;

            case DriveBreakpoint::Condition::AEquals:
                matched = state.A == bp.value;
                break;

            case DriveBreakpoint::Condition::XEquals:
                matched = state.X == bp.value;
                break;

            case DriveBreakpoint::Condition::YEquals:
                matched = state.Y == bp.value;
                break;
        }

        if (!matched)
            continue;

        breakpointHit = true;
        breakpointHitAddress = state.PC;
        breakpointHitCPU = cpuId;

        return true;
    }

    return false;
}
