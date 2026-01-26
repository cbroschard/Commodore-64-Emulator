// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef RESETCONTROLLER_H
#define RESETCONTROLLER_H

#include <string>
#include "CPUTiming.h"

// forward declares
class Cartridge;
class CIA1;
class CIA2;
class CPU;
class IECBUS;
class MediaManager;
class Memory;
class PLA;
class SID;
class Vic;

enum class VideoMode;

class ResetController
{
public:
    ResetController(
        CPU& cpu,
        Memory& mem,
        PLA& pla,
        CIA1& cia1,
        CIA2& cia2,
        Vic& vic,
        SID& sid,
        IECBUS& bus,
        Cartridge& cart,
        MediaManager* media,
        const std::string& basicRom,
        const std::string& kernalRom,
        const std::string& charRom,
        VideoMode& videoMode,
        const CPUConfig*& cpuCfg);

    ~ResetController() = default;

    void warmReset();
    void coldReset();
    void setVideoMode(const std::string& mode);

private:
    CPU& cpu_;
    Memory& mem_;
    PLA& pla_;
    CIA1& cia1_;
    CIA2& cia2_;
    Vic& vic_;
    SID& sid_;
    IECBUS& bus_;
    Cartridge& cart_;
    MediaManager* media_;

    const std::string& basicRom_;
    const std::string& kernalRom_;
    const std::string& charRom_;

    VideoMode& videoMode_;
    const CPUConfig*& cpuCfg_;
};

#endif // RESETCONTROLLER_H
