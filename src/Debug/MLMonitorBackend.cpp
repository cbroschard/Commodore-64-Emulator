// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "MLMonitorBackend.h"

MLMonitorBackend::MLMonitorBackend() :
    cart(nullptr),
    cass(nullptr),
    cia1object(nullptr),
    cia2object(nullptr),
    processor(nullptr),
    bus(nullptr),
    logger(nullptr),
    pla(nullptr),
    sidchip(nullptr),
    vicII(nullptr)
{

}

MLMonitorBackend::~MLMonitorBackend() = default;

void MLMonitorBackend::detachCartridge()
{
    if (comp) comp->setCartridgeAttached(false);
}

bool MLMonitorBackend::getCartridgeAttached()
{
    if (comp) return comp->getCartridgeAttached();
    else return false;
}

void MLMonitorBackend::vicFFRaster(uint8_t targetRaster)
{
    while(vicII->getCurrentRaster() != targetRaster)
    {
        vicII->tick(1);
        processor->tick();
        cia1object->updateTimers(1);
        cia2object->updateTimers(1);
        sidchip->tick(1);
    }
}

void MLMonitorBackend::coldReset()
{
    if (comp) comp->coldReset();
    else std::cerr << "Error: No Computer attached, cannot perform reset!\n";
}

void MLMonitorBackend::warmReset()
{
    if (comp) comp->warmReset();
    else std::cerr << "Error: No Computer attached, cannot perform reset!\n";
}

void MLMonitorBackend::irqDisableAll()
{
    if (!vicII && !cia1object && !cia2object) return;

    snapshot.has = true;
    snapshot.vic  = vicII->snapshotIRQs();
    snapshot.cia1 = cia1object->snapshotIRQs();
    snapshot.cia2 = cia2object->snapshotIRQs();

    vicII->disableAllIRQs();
    cia1object->disableAllIRQs();
    cia2object->disableAllIRQs();

    irqClearAll();  // acknowledge anything pending after the mask change
}

void MLMonitorBackend::irqClearAll()
{
    if (!vicII && !cia1object && !cia2object) return;

    vicII->clearPendingIRQs();
    cia1object->clearPendingIRQs();
    cia2object->clearPendingIRQs();
}

void MLMonitorBackend::irqRestore()
{
    if (!vicII && !cia1object && !cia2object) return;
    if (!snapshot.has) return;

    vicII->restoreIRQs(snapshot.vic);
    cia1object->restoreIRQs(snapshot.cia1);
    cia2object->restoreIRQs(snapshot.cia2);
}

void MLMonitorBackend::setLogging(LogSet log, bool enabled)
{
    switch (log)
    {
        case LogSet::Cartridge: if (cart) cart->setLog(enabled); break;
        case LogSet::Cassette: if (cass) cass->setLog(enabled); break;
        case LogSet::CIA1: if (cia1object) cia1object->setLog(enabled); break;
        case LogSet::CIA2: if (cia2object) cia2object->setLog(enabled); break;
        case LogSet::CPU: if (processor) processor->setLog(enabled); break;
        case LogSet::IO: if (IO_adapter) IO_adapter->setLog(enabled); break;
        case LogSet::Joystick:
        {
            Joystick* joy1 = comp->getJoy1();
            if (joy1)
            {
                joy1->attachLogInstance(logger);
                joy1->setLog(enabled);
            }

            Joystick* joy2 = comp->getJoy2();
            if (joy2)
            {
                joy2->attachLogInstance(logger);
                joy2->setLog(enabled);
            }
        }
        case LogSet::Keyboard: if (keyb) keyb->setLog(enabled); break;
        case LogSet::Memory: if (mem) mem->setLog(enabled); break;
        case LogSet::PLA: if (pla) pla->setLog(enabled); break;
        case LogSet::VIC: if (vicII) vicII->setLog(enabled); break;
    }
}

std::string MLMonitorBackend::jamModeToString() const
{
    if (processor)
    {
        CPU::JamMode mode = processor->getJamMode();
        switch(mode)
        {
            case CPU::JamMode::FreezePC: return "FreezePC";
            case CPU::JamMode::Halt: return "Halt";
            case CPU::JamMode::NopCompat: return "NopCompat";
        }
    }

    // Default
        return "Unknown";
}

void MLMonitorBackend::setJamMode(const std::string& mode)
{
    if (processor)
    {
        if (mode == "freeze")
        {
            processor->setJamMode(CPU::JamMode::FreezePC);
        }
        else if (mode == "halt")
        {
            processor->setJamMode(CPU::JamMode::Halt);
        }
        else if (mode == "nop")
        {
            processor->setJamMode(CPU::JamMode::NopCompat);
        }
    }
}
