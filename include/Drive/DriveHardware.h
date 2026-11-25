// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DRIVEHARDWARE_H
#define DRIVEHARDWARE_H

#include <cstdint>
#include <string>
#include <vector>

class CPU;
class CPUBus;

enum class DriveHardwareType
{
    CPU,
    Bus,
    Memory,
    VIA6522,
    CIA6526,
    CIA8520,
    RIOT6532,
    RRIOT6530,
    FDC177X,
    DiskController,
    Mechanism,
    Other
};

struct DriveHardwareInfo
{
    // Stable unique ID within this drive.
    uint32_t id = 0;

    DriveHardwareType type =
        DriveHardwareType::Other;

    // Human-readable component name.
    std::string name;

    // CPU associated with this component.
    // -1 means no specific CPU association.
    int32_t processorId = -1;

    // Optional memory-mapped register information.
    bool memoryMapped = false;
    uint16_t baseAddress = 0;
    uint32_t addressSize = 0;
};

inline const char* driveHardwareTypeName(
    DriveHardwareType type)
{
    switch (type)
    {
        case DriveHardwareType::CPU:
            return "CPU";

        case DriveHardwareType::Bus:
            return "BUS";

        case DriveHardwareType::Memory:
            return "MEMORY";

        case DriveHardwareType::VIA6522:
            return "VIA6522";

        case DriveHardwareType::CIA6526:
            return "CIA6526";

        case DriveHardwareType::CIA8520:
            return "CIA8520";

        case DriveHardwareType::RIOT6532:
            return "RIOT6532";

        case DriveHardwareType::RRIOT6530:
            return "RRIOT6530";

        case DriveHardwareType::FDC177X:
            return "FDC177X";

        case DriveHardwareType::DiskController:
            return "CONTROLLER";

        case DriveHardwareType::Mechanism:
            return "MECHANISM";

        default:
            return "OTHER";
    }
}

struct DriveCPUContext
{
    uint32_t cpuId = 0;
    uint32_t busId = 0;

    CPU* cpu = nullptr;
    CPUBus* bus = nullptr;
};

#endif // DRIVEHARDWARE_H
