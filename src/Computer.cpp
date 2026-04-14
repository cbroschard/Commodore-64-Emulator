// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include "Computer.h"
#include "DebugManager.h"
#include "Drive/D1541.h"
#include "Drive/D1571.h"
#include "Drive/D1581.h"
#include "Drive/Drive.h"
#include "EmulationSession.h"
#include "MachineBuilder.h"
#include "ResetController.h"
#include "StateManager.h"
#include "Tape/TapeImageFactory.h"
#include "UIBridge.h"

Computer::Computer() :
    videoMode_(VideoMode::NTSC),
    cpuCfg_(&NTSC_CPU),
    running(true),
    uiQuit(false),
    uiPaused(false),
    pendingBusPrime(false),
    busPrimedAfterBoot(false),
    runtime_
    {
        running,
        uiPaused,
        videoMode_,
        cpuCfg_,
        pendingBusPrime,
        busPrimedAfterBoot
    }
{
    components_.cart = std::make_unique<Cartridge>();
    components_.cass = std::make_unique<Cassette>();
    components_.cia1 = std::make_unique<CIA1>();
    components_.cia2 = std::make_unique<CIA2>();
    components_.cpu = std::make_unique<CPU>();
    components_.ui = std::make_unique<EmulatorUI>();
    components_.bus = std::make_unique<IECBUS>();
    components_.inputMgr = std::make_unique<InputManager>();
    components_.irq = std::make_unique<IRQLine>();
    components_.keyb = std::make_unique<Keyboard>();
    components_.logger = std::make_unique<Logging>("debug.txt");
    components_.mem = std::make_unique<Memory>();
    components_.pla = std::make_unique<PLA>();
    components_.sid = std::make_unique<SID>(44100);
    components_.io = std::make_unique<IO>();
    components_.vic = std::make_unique<Vic>();

    // Wire components
    wireUp();
}

Computer::~Computer() noexcept
{
    try
    {
        if (components_.io)
        {
            // Ensure the render thread is down
            if (components_.debug) components_.debug->closeMonitor();
            running = false;
            components_.io->stopRenderThread(running);
            components_.io->setGuiCallback({});
            components_.io->setInputCallback({});

            // Audio shutdown
            components_.io->stopAudio();
        }

        if (components_.logger) components_.logger->flush();
    }
    catch(...)
    {

    }
}

bool Computer::saveStateToFile(const std::string& path)
{
    return components_.stateMgr ? components_.stateMgr->save(path) : false;
}

bool Computer::loadStateFromFile(const std::string& path)
{
    return components_.stateMgr ? components_.stateMgr->load(path) : false;
}

void Computer::requestColdReset()
{
    if (components_.resetCtl)
        components_.resetCtl->coldReset();
}

void Computer::requestWarmReset()
{
    if (components_.resetCtl)
        components_.resetCtl->warmReset();
}

void Computer::requestCartridgeNMI()
{
    if (components_.cpu)
        components_.cpu->pulseNMI();
}

void Computer::setJoystickAttached(int port, bool flag)
{
    if (components_.inputMgr) components_.inputMgr->setJoystickAttached(port, flag);
}

void Computer::set1541LoROM(const std::string& loROM)
{
    roms_.d1541LoRom = loROM;
    if (components_.media) components_.media->setD1541LoROM(loROM);
}

void Computer::set1541HiROM(const std::string& hiROM)
{
    roms_.d1541HiRom = hiROM;
    if (components_.media) components_.media->setD1541HiROM(hiROM);
}

void Computer::set1571ROM(const std::string& rom)
{
    roms_.d1571Rom = rom;
    if (components_.media) components_.media->setD1571ROM(rom);
}

void Computer::set1581ROM(const std::string& rom)
{
    roms_.d1581Rom = rom;
    if (components_.media) components_.media->setD1581ROM(rom);
}

void Computer::enterMonitor()
{
    if (components_.debug) components_.debug->openMonitor();
}

void Computer::setJoystickConfig(int port, JoystickMapping& cfg)
{
    if (!components_.inputMgr) return;
    components_.inputMgr->setJoystickConfig(port, cfg);
}

bool Computer::boot()
{
    EmulationSession session(components_, runtime_, roms_, uiQuit);

    return session.run();
}

void Computer::warmReset()
{
     if (components_.resetCtl) components_.resetCtl->warmReset();
}

void Computer::coldReset()
{
     if (components_.resetCtl) components_.resetCtl->coldReset();
}

void Computer::setVideoMode(const std::string& mode)
{
    if (components_.resetCtl) components_.resetCtl->setVideoMode(mode);
}

void Computer::wireUp()
{
    MachineBuilder::assemble(this, components_, runtime_, roms_);
}
