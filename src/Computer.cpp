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
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"
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
        sidModel_,
        cpuCfg_,
        pendingBusPrime,
        busPrimedAfterBoot
    },
    cartridgeNMIPending(false),
    swiftLinkBaseAddress(0xDE00),
    turbo232BaseAddress(0xDE00),
    resumeAfterVicCycleBreakpoint(false)
{
    components_.sdlContext = std::make_unique<SDLContext>();
    components_.audioOutput = std::make_unique<AudioOutput>();
    components_.videoOutput = std::make_unique<VideoOutput>();
    components_.cart = std::make_unique<Cartridge>();
    components_.cass = std::make_unique<Cassette>();
    components_.cia1 = std::make_unique<CIA1>();
    components_.cia2 = std::make_unique<CIA2>();
    components_.cpu = std::make_unique<CPU>();
    components_.dataBus = std::make_unique<DataBusLatch>();
    components_.ui = std::make_unique<EmulatorUI>();
    components_.executionHistory = std::make_unique<ExecutionHistory>(4096);
    components_.expansionManager = std::make_unique<ExpansionManager>(*this);
    components_.bus = std::make_unique<IECBUS>();
    components_.inputMgr = std::make_unique<InputManager>();
    components_.irq = std::make_unique<IRQLine>();
    components_.keyb = std::make_unique<Keyboard>();
    components_.mem = std::make_unique<Memory>();
    components_.nmiLine = std::make_unique<NMILine>();
    components_.pla = std::make_unique<PLA>();
    components_.reu = std::make_unique<REU>();
    components_.sid = std::make_unique<SID>(44100);
    components_.userPort = std::make_unique<UserPort>();
    components_.rs232Device = std::make_unique<RS232Device>();
    components_.userPortRS232Adapter = std::make_unique<UserPortRS232Adapter>();
    components_.vic = std::make_unique<Vic>();

    // Wire components
    wireUp();
}

Computer::~Computer() noexcept
{
    try
    {
        running = false;

        detachVirtualModem();
        detachSwiftLinkVirtualModem();
        detachTurbo232VirtualModem();

        if (components_.userPort)
            components_.userPort->detachDevice();

        if (components_.debug)
            components_.debug->closeMonitor();

        if (components_.videoOutput)
        {
            components_.videoOutput->setGuiCallback({});
            components_.videoOutput->setInputCallback({});
            components_.videoOutput->setMonitorOpenCallback({});
        }

        if (components_.audioOutput)
            components_.audioOutput->stopAudio();

        // Detach anything that points into DebugManager.
        if (components_.cart)
            components_.cart->attachTraceManagerInstance(nullptr);

        if (components_.cia1)
            components_.cia1->attachTraceManagerInstance(nullptr);

        if (components_.cia2)
            components_.cia2->attachTraceManagerInstance(nullptr);

        if (components_.cpu)
            components_.cpu->attachTraceManagerInstance(nullptr);

        if (components_.pla)
            components_.pla->attachTraceManagerInstance(nullptr);

        if (components_.sid)
            components_.sid->attachTraceManagerInstance(nullptr);

        if (components_.vic)
            components_.vic->attachTraceManagerInstance(nullptr);

        if (components_.mem)
        {
            components_.mem->attachDebugManagerInstance(nullptr);
            components_.mem->attachMonitorInstance(nullptr);
            components_.mem->attachTraceManagerInstance(nullptr);
        }

        // Tear down objects that hold references/callbacks
        // to the rest of the machine.
        components_.stateMgr.reset();
        components_.uiBridge.reset();
        components_.inputRouter.reset();
        components_.resetCtl.reset();
        components_.media.reset();
    }
    catch (...)
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
    if (!components_.nmiLine)
        return;

    components_.nmiLine->raiseNMI(NMILine::CARTRIDGE);
    cartridgeNMIPending = true;
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

void Computer::attachVirtualModem()
{
    if (!components_.rs232Device)
        return;

    if (!components_.virtualModem)
        components_.virtualModem = std::make_unique<VirtualModem>();

    components_.virtualModem->reset();

    components_.rs232Device->attachEndpoint(
        components_.virtualModem.get());
}

void Computer::detachVirtualModem()
{
    if (components_.rs232Device)
        components_.rs232Device->detachEndpoint();

    components_.virtualModem.reset();
}

bool Computer::isVirtualModemAttached() const
{
    return components_.rs232Device && components_.virtualModem && components_.rs232Device->getEndpoint() ==
               components_.virtualModem.get();
}

bool Computer::isVirtualModemOnline() const
{
    return components_.virtualModem &&
           components_.virtualModem->isOnline();
}

void Computer::setRS232Baud(uint32_t baud)
{
    if (components_.rs232Device)
        components_.rs232Device->setBaud(baud);
}

uint32_t Computer::getRS232Baud() const
{
    return components_.rs232Device ? components_.rs232Device->getBaud() : 300;
}

void Computer::enableSwiftLink(uint16_t baseAddress)
{
    if (components_.swiftLink)
        return;

    components_.swiftLink = std::make_unique<SwiftLink>(baseAddress);

    components_.swiftLink->attachNMILineInstance(components_.nmiLine.get());
    components_.mem->attachSwiftLinkInstance(components_.swiftLink.get());

    if (components_.debug)
        components_.debug->backend().attachSwiftLinkInstance(components_.swiftLink.get());
}

void Computer::disableSwiftLink()
{
    if (!components_.swiftLink)
        return;

    detachSwiftLinkVirtualModem();

    components_.mem->attachSwiftLinkInstance(nullptr);

    if (components_.debug)
        components_.debug->backend().attachSwiftLinkInstance(nullptr);

    if (components_.nmiLine)
        components_.nmiLine->clearNMI(NMILine::SWIFTLINK);

    components_.swiftLink.reset();
}

void Computer::enableSwiftLink()
{
    enableSwiftLink(swiftLinkBaseAddress);
}

bool Computer::isSwiftLinkEnabled() const
{
    return components_.swiftLink != nullptr;
}

void Computer::setSwiftLinkBaseAddress(uint16_t address)
{
    if (address != 0xDE00 && address != 0xDF00)
        return;

    if (swiftLinkBaseAddress == address)
        return;

    const bool wasEnabled = components_.swiftLink != nullptr;
    const bool modemWasAttached = isSwiftLinkVirtualModemAttached();

    if (wasEnabled)
        disableSwiftLink();

    swiftLinkBaseAddress = address;

    if (wasEnabled)
    {
        enableSwiftLink();

        if (modemWasAttached)
            attachSwiftLinkVirtualModem();
    }
}

uint16_t Computer::getSwiftLinkBaseAddress() const
{
    return swiftLinkBaseAddress;
}

void Computer::attachSwiftLinkVirtualModem()
{
    if (!components_.swiftLink)
        return;

    if (!components_.swiftLinkVirtualModem)
        components_.swiftLinkVirtualModem = std::make_unique<VirtualModem>();

    components_.swiftLinkVirtualModem->reset();
    components_.swiftLink->attachEndpoint(components_.swiftLinkVirtualModem.get());
}

void Computer::detachSwiftLinkVirtualModem()
{
    if (components_.swiftLink)
        components_.swiftLink->detachEndpoint();

    components_.swiftLinkVirtualModem.reset();
}

bool Computer::isSwiftLinkVirtualModemAttached() const
{
    return components_.swiftLink && components_.swiftLinkVirtualModem && components_.swiftLink->hasEndpoint();
}

bool Computer::isSwiftLinkVirtualModemOnline() const
{
    return components_.swiftLinkVirtualModem && components_.swiftLinkVirtualModem->isOnline();
}

void Computer::enableTurbo232(uint16_t baseAddress)
{
    if (components_.turbo232)
        return;

    components_.turbo232 = std::make_unique<Turbo232>(baseAddress);

    components_.turbo232->attachNMILineInstance(components_.nmiLine.get());
    components_.mem->attachTurbo232Instance(components_.turbo232.get());

    if (components_.debug)
        components_.debug->backend().attachTurbo232Instance(components_.turbo232.get());
}

void Computer::disableTurbo232()
{
    if (!components_.turbo232)
        return;

    detachTurbo232VirtualModem();

    components_.mem->attachTurbo232Instance(nullptr);

    if (components_.debug)
        components_.debug->backend().attachTurbo232Instance(nullptr);

    if (components_.nmiLine)
        components_.nmiLine->clearNMI(NMILine::TURBO232);

    components_.turbo232.reset();
}

void Computer::enableTurbo232()
{
    enableTurbo232(turbo232BaseAddress);
}

bool Computer::isTurbo232Enabled() const
{
    return components_.turbo232 != nullptr;
}

void Computer::setTurbo232BaseAddress(uint16_t address)
{
    if (address != 0xDE00 && address != 0xDF00)
        return;

    if (turbo232BaseAddress == address)
        return;

    const bool wasEnabled = components_.turbo232 != nullptr;
    const bool modemWasAttached = isTurbo232VirtualModemAttached();

    if (wasEnabled)
        disableTurbo232();

    turbo232BaseAddress = address;

    if (wasEnabled)
    {
        enableTurbo232();

        if (modemWasAttached)
            attachTurbo232VirtualModem();
    }
}

uint16_t Computer::getTurbo232BaseAddress() const
{
    return turbo232BaseAddress;
}

void Computer::attachTurbo232VirtualModem()
{
    if (!components_.turbo232)
        return;

    if (!components_.turbo232VirtualModem)
        components_.turbo232VirtualModem = std::make_unique<VirtualModem>();

    components_.turbo232VirtualModem->reset();
    components_.turbo232->attachEndpoint(components_.turbo232VirtualModem.get());
}

void Computer::detachTurbo232VirtualModem()
{
    if (components_.turbo232)
        components_.turbo232->detachEndpoint();

    components_.turbo232VirtualModem.reset();
}

bool Computer::isTurbo232VirtualModemAttached() const
{
    return components_.turbo232 && components_.turbo232VirtualModem && components_.turbo232->hasEndpoint();
}

bool Computer::isTurbo232VirtualModemOnline() const
{
    return components_.turbo232VirtualModem && components_.turbo232VirtualModem->isOnline();
}

void Computer::enterMonitor()
{
    if (components_.debug) components_.debug->openMonitor();
}

void Computer::setJoystickConfig(int port, const JoystickMapping& cfg)
{
    if (!components_.inputMgr) return;
    components_.inputMgr->setJoystickConfig(port, cfg);
}

bool Computer::boot()
{
    EmulationSession session(*this, components_, runtime_, roms_, uiQuit);

    return session.run();
}

void Computer::tickCycle()
{
    if (!resumeAfterVicCycleBreakpoint)
    {
        components_.vic->beginCycle();

        if (components_.debug &&
            components_.debug->monitor().checkVicCycleBreakpoint())
        {
            resumeAfterVicCycleBreakpoint = true;

            runtime_.uiPaused = true;
            components_.debug->openMonitor();
            return;
        }
    }
    else
    {
        resumeAfterVicCycleBreakpoint = false;
    }

    // VIC performs its memory/bus fetch before the CPU's Phi2 bus cycle.
    components_.vic->busPhase();

    components_.cpu->setRDY(components_.vic->getBA());
    components_.cpu->setAEC(components_.vic->getAEC());
    components_.cpu->tick();

    if (cartridgeNMIPending)
    {
        components_.nmiLine->clearNMI(NMILine::CARTRIDGE);
        cartridgeNMIPending = false;
    }

    components_.sid->tick(1);

    components_.cia1->updateTimers(1);
    components_.cia2->updateTimers(1);

    if (components_.swiftLink)
        components_.swiftLink->tick(1);

    if (components_.turbo232)
        components_.turbo232->tick(1);

    components_.bus->tick(1);

    if (auto* mapper = components_.cart->getMapper())
        mapper->tick(1);

    // Pixel composition and raster advancement happen after Phi2.
    components_.vic->endCycle();
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

void Computer::setSIDModel(const std::string& model)
{
    if (components_.resetCtl) components_.resetCtl->setSIDModel(model);
}

void Computer::wireUp()
{
    MachineBuilder::assemble(this, components_, runtime_, roms_);
}
