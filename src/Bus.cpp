// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Bus.h"
#include "Cartridge.h"
#include "Cassette.h"
#include "CIA1.h"
#include "CIA2.h"
#include "CPU.h"
#include "CPU6510Port.h"
#include "DataBusLatch.h"
#include "DebugManager.h"
#include "Memory.h"
#include "MLMonitor.h"
#include "PLA.h"
#include "REU.h"
#include "SID/SID.h"
#include "Expansion/SwiftLink.h"
#include "Debug/TraceManager.h"
#include "Expansion/Turbo232.h"
#include "Vic.h"

Bus::Bus() :
    cart(nullptr),
    cia1(nullptr),
    cia2(nullptr),
    cass(nullptr),
    cpu(nullptr),
    cpu6510Port(nullptr),
    dataBus(nullptr),
    debugManager(nullptr),
    mem(nullptr),
    monitor(nullptr),
    pla(nullptr),
    reu(nullptr),
    sid(nullptr),
    swiftLink(nullptr),
    traceMgr(nullptr),
    turbo232(nullptr),
    vic(nullptr),
    cartridgeAttached(false),
    romLOverlayIsRAM(false),
    romHOverLayIsRAM(false)
{

}

Bus::~Bus() = default;

uint8_t Bus::read(uint16_t address)
{
    // Complete tracing and watchpoint processing without changing
    // which component drove the shared data bus.
    auto finishRead = [&](uint8_t value) -> uint8_t
    {
        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_CPU) && traceMgr->memRangeContains(address))
        {
            const uint16_t pc = cpu ? cpu->getPC() : 0;

            const TraceManager::Stamp stamp = traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, vic ? vic->getCurrentRaster() : 0,
                    vic ? vic->getRasterDot() : 0);

            traceMgr->recordMemRead(address, value, pc, stamp);
        }

        if (monitor && monitor->checkWatchRead(address, value))
        {
            if (debugManager)
                debugManager->onWatchpoint();
            else
                monitor->enterMonitor();
        }

        return value;
    };

    // RAM, system ROM, CPU port and color RAM are supplied
    // directly by the Memory subsystem.
    auto memoryRead = [&](uint8_t value) -> uint8_t
    {
        if (dataBus)
            dataBus->drive(value, DataBusLatch::Driver::Memory);

        return finishRead(value);
    };

    // Cartridge images stored in Memory's cart_lo/cart_hi arrays
    // still represent the cartridge driving the physical bus.
    auto cartridgeRead = [&](uint8_t value) -> uint8_t
    {
        if (dataBus)
            dataBus->drive(value, DataBusLatch::Driver::Cartridge);

        return finishRead(value);
    };

    // The selected device already updated DataBusLatch.
    auto deviceRead = [&](uint8_t value) -> uint8_t
    {
        return finishRead(value);
    };

    // No component drives the bus. Return the existing latch.
    auto openBusRead = [&]() -> uint8_t
    {
        const uint8_t value = dataBus ? dataBus->sample() : 0xFF;
        return finishRead(value);
    };

    if (address == 0x0000)
        return memoryRead(cpu6510Port->readDDR());

    if (address == 0x0001)
        return memoryRead(cpu6510Port->readPort());

    // Mapper-controlled CPU accesses are checked before PLA decoding.
    // Capture uses this for cartridge RAM at $6000-$7FFF and its
    // control locations at $FFF7-$FFF8.
    if (cart && cartridgeAttached && cart->cpuMemoryHandledByMapper(address))
        return deviceRead(cart->read(address));

    if (!pla)
        throw std::runtime_error("Error: Missing PLA object!");

    const PLA::memoryAccessInfo accessInfo = pla->getMemoryAccess(address);

    switch (accessInfo.bank)
    {
        case PLA::RAM:
        {
            return memoryRead(mem->readRAM(accessInfo.offset));
        }

        case PLA::KERNAL_ROM:
        {
            return memoryRead(mem->readKernalROM(accessInfo.offset));
        }

        case PLA::BASIC_ROM:
        {
            return memoryRead(mem->readBASICROM(accessInfo.offset));
        }

        case PLA::CHARACTER_ROM:
        {
            return memoryRead(mem->readCharROM(accessInfo.offset));
        }

        case PLA::CARTRIDGE_LO:
        {
            if (romLOverlayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return deviceRead(cart->readRAM(accessInfo.offset));

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return deviceRead(cart->read(address));

            return cartridgeRead(mem->readCartridge(accessInfo.offset, cartLocation::LO));
        }

        case PLA::CARTRIDGE_HI:
        {
            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return deviceRead(cart->readRAM(accessInfo.offset));

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return deviceRead(cart->read(address));

            return cartridgeRead(mem->readCartridge(accessInfo.offset, cartLocation::HI));
        }

        case PLA::CARTRIDGE_HI_E000:
        {
            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return deviceRead(cart->readRAM(accessInfo.offset));

            // Added for mapper-controlled Ultimax high ROM.
            // Capture uses this so $FFF7 can hide ROMH and $FFF8
            // can make ROMH visible again.
            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return deviceRead(cart->read(address));

            return cartridgeRead(mem->readCartridge(accessInfo.offset, cartLocation::HI_E000));
        }

        case PLA::IO:
        {
            if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
            {
                const uint8_t lowNibble = mem->readColorRAM(address - COLOR_MEMORY_START);
                const uint8_t upperNibble = dataBus ? static_cast<uint8_t>(dataBus->sample() & 0xF0) : 0xF0;

                const uint8_t value = static_cast<uint8_t>(upperNibble | lowNibble);

                return memoryRead(value);
            }
            return deviceRead(readIO(accessInfo.offset));
        }

        case PLA::UNMAPPED:
        {
            return openBusRead();
        }
    }

    return openBusRead();
}

void Bus::write(uint16_t address, uint8_t value)
{
       if (!pla) throw std::runtime_error("Error: Missing PLA object!");

    // Check for trace enabled and write if so
    if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_CPU) && traceMgr->memRangeContains(address))
    {
        uint16_t PC = cpu ? cpu->getPC() : 0;
        TraceManager::Stamp stamp = traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, vic ? vic->getCurrentRaster() : 0,
            vic ? vic->getRasterDot() : 0);

        traceMgr->recordMemWrite(address, value, PC, stamp);
    }

    if (address == 0x0000)
    {
        cpu6510Port->writeDDR(value);
        return;
    }

    if (address == 0x0001)
    {
        cpu6510Port->writePort(value);
        return;
    }

    if (cart && cartridgeAttached && cart->cpuMemoryHandledByMapper(address))
    {
        cart->write(address, value);

        if (monitor && monitor->checkWatchWrite(address, value))
        {
            if (debugManager)
                debugManager->onWatchpoint();
            else
                monitor->enterMonitor();
        }

        return;
    }

    PLA::memoryAccessInfo accessInfo = pla->getMemoryAccess(address);

    switch(accessInfo.bank)
    {
        case PLA::RAM:
        {
            mem->writeRAM(accessInfo.offset, value);
            break;
        }
        case PLA::IO:
        {
            if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
            {
                mem->writeColorRAM(address - COLOR_MEMORY_START, value);
                return;
            }
            writeIO(accessInfo.offset, value);
            break;
        }
        case PLA::KERNAL_ROM:
        case PLA::BASIC_ROM:
        case PLA::CHARACTER_ROM:
        {
            // Write the value to the requested RAM address
            mem->writeRAM(address, value);
            break;
        }
        case PLA::CARTRIDGE_LO:
        {
            mem->writeRAM(address, value);

            if (romLOverlayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
            {
                cart->writeRAM(accessInfo.offset, value);
            }

            if (cart && cartridgeAttached && cart->romWriteEnabled(address))
            {
                cart->write(address, value);
            }

            break;
        }
        case PLA::CARTRIDGE_HI:
        {
            mem->writeRAM(address, value);

            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
            {
                cart->writeRAM(accessInfo.offset, value);
            }

            if (cart && cartridgeAttached && cart->romWriteEnabled(address))
            {
                cart->write(address, value);
            }

            break;
        }
        case PLA::CARTRIDGE_HI_E000:
        {
            mem->writeRAM(address, value);

            // If you ever support RAM overlay in this region:
            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
            {
                cart->writeRAM(accessInfo.offset, value);
            }

            break;
        }
        case PLA::UNMAPPED:
        {
            break;
        }
    }
    if (monitor && monitor->checkWatchWrite(address, value))
    {
        if (debugManager)
            debugManager->onWatchpoint();
        else
            monitor->enterMonitor();
    }
}

uint8_t Bus::readIO(uint16_t address)
{

    if (address >= IO_VIC_START && address <= IO_VIC_END)
    {
        // Handle VIC address mirroring
        uint16_t mirroredAddress = (address & 0x003F) + 0xD000; // Mask out everything except the lower 6 bits
        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_IO))
        {
            std::ostringstream out;
            out << "[MEM:IO] read VIC $"
                << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << mirroredAddress
                << " via $" << std::setw(4) << address;
            traceMgr->recordCustomEvent(out.str(), traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, vic ? vic->getCurrentRaster() : 0,
                                    vic ? vic->getRasterDot() : 0));
        }

        if (vic)
            return vic->readRegister(mirroredAddress);
    }
    else if (address >= IO_SID_START && address <= IO_SID_END)
    {
        // Handle SID address mirroring
        uint16_t mirroredAddress = (address & 0x001F) + 0xD400;
        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_IO))
        {
            std::ostringstream out;
            out << "[MEM:IO] read SID $"
                << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << mirroredAddress
                << " via $" << std::setw(4) << address;
            traceMgr->recordCustomEvent(out.str(), traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, vic ? vic->getCurrentRaster() : 0,
                                    vic ? vic->getRasterDot() : 0));
        }

        if (sid)
            return sid->readRegister(mirroredAddress);
    }
    else if (address >= IO_CIA1_START && address <= IO_CIA1_END)
    {
        // Handle CIA1 address mirroring
        uint16_t mirroredAddress = (address & 0x000F) + 0xDC00;
        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_IO))
        {
            std::ostringstream out;
            out << "[MEM:IO] read CIA1 $"
                << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << mirroredAddress
                << " via $" << std::setw(4) << address;
            traceMgr->recordCustomEvent(out.str(), traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, vic ? vic->getCurrentRaster() : 0,
                                    vic ? vic->getRasterDot() : 0));
        }

        if (cia1)
            return cia1->readRegister(mirroredAddress);
    }
    else if (address >= IO_CIA2_START && address <= IO_CIA2_END)
    {
        // Handle CIA2 address mirroring
        uint16_t mirroredAddress = (address & 0x000F) + 0xDD00;
        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_IO))
        {
            std::ostringstream out;
            out << "[MEM:IO] read CIA2 $"
                << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << mirroredAddress
                << " via $" << std::setw(4) << address;

            traceMgr->recordCustomEvent(out.str(), traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, vic ? vic->getCurrentRaster() : 0,
                                    vic ? vic->getRasterDot() : 0));
        }

        if (cia2)
            return cia2->readRegister(mirroredAddress);
    }
    else if (address >= 0xDE00 && address <= 0xDFFF)
    {
        if (swiftLink && swiftLink->handlesAddress(address))
            return swiftLink->read(address);

        if (turbo232 && turbo232->handlesAddress(address))
            return turbo232->read(address);

        if (reu && reu->isEnabled() && address >= 0xDF00 && address <= 0xDF0A)
            return reu->readIO(address);

        if (cart && cartridgeAttached)
            return cart->read(address);

        return dataBus ? dataBus->sample() : 0xFF;
    }

    return dataBus ? dataBus->sample() : 0xFF;
}

void Bus::writeIO(uint16_t address, uint8_t value)
{
    if (address >= 0xD000 && address <= 0xD3FF)
    {
        uint16_t mirroredAddress = (address & 0x003F) + 0xD000;
        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_IO))
        {
            std::ostringstream out;
            out << "[MEM:IO] write VIC $"
                << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << mirroredAddress
                << " via $" << std::setw(4) << address
                << " value=$" << std::setw(2) << int(value);
            traceMgr->recordCustomEvent(out.str(),
                traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0,
                                    vic ? vic->getCurrentRaster() : 0,
                                    vic ? vic->getRasterDot() : 0));
        }

        if (vic)
        {
            vic->writeRegister(mirroredAddress,value);
            return;
        }
    }
    else if (address >= 0xD400 && address <= 0xD7FF)
    {
        uint16_t mirroredAddress = (address & 0x001F) + 0xD400;
        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_IO))
        {
            std::ostringstream out;
            out << "[MEM:IO] write SID $"
                << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << mirroredAddress
                << " via $" << std::setw(4) << address
                << " value=$" << std::setw(2) << int(value);
            traceMgr->recordCustomEvent(out.str(),
                traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0,
                                    vic ? vic->getCurrentRaster() : 0,
                                    vic ? vic->getRasterDot() : 0));
        }

        if (sid)
        {
            sid->writeRegister(mirroredAddress,value);
            return;
        }
    }
    else if (address >= 0xDC00 && address <= 0xDCFF)
    {
        uint16_t mirroredAddress = (address & 0x000F) + 0xDC00;
        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_IO))
        {
            std::ostringstream out;
            out << "[MEM:IO] write CIA1 $"
                << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << mirroredAddress
                << " via $" << std::setw(4) << address
                << " value=$" << std::setw(2) << int(value);
            traceMgr->recordCustomEvent(out.str(),
                traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0,
                                    vic ? vic->getCurrentRaster() : 0,
                                    vic ? vic->getRasterDot() : 0));
        }

        if (cia1)
        {
            cia1->writeRegister(mirroredAddress,value);
            return;
        }
    }
    else if (address >= 0xDD00 && address <= 0xDDFF)
    {
        uint16_t mirroredAddress = (address & 0x000F) + 0xDD00;
        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_IO))
        {
            std::ostringstream out;
            out << "[MEM:IO] write CIA2 $"
                << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << mirroredAddress
                << " via $" << std::setw(4) << address
                << " value=$" << std::setw(2) << int(value);
            traceMgr->recordCustomEvent(out.str(),
                traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0,
                                    vic ? vic->getCurrentRaster() : 0,
                                    vic ? vic->getRasterDot() : 0));
        }

        if (cia2)
        {
            cia2->writeRegister(mirroredAddress,value);
            return;
        }
    }
    else if (address >= 0xDE00 && address <= 0xDFFF)
    {
        if (swiftLink && swiftLink->handlesAddress(address))
        {
            swiftLink->write(address, value);
            return;
        }

        if (turbo232 && turbo232->handlesAddress(address))
        {
            turbo232->write(address, value);
            return;
        }

        if (reu && reu->isEnabled() &&
            address >= 0xDF00 && address <= 0xDF0A)
        {
            reu->writeIO(address, value);
            return;
        }

        if (cart && cartridgeAttached)
        {
            cart->write(address, value);
            return;
        }
    }
}

uint8_t Bus::vicRead(uint16_t vicAddress, uint16_t raster)
{
    // VIC has a 14-bit address bus.
    vicAddress &= 0x3FFF;

    const uint16_t bankBase = vic ? vic->getBankBaseFromVIC(raster) : 0;

    // Character ROM is visible to the VIC in banks 0 and 2
    // at VIC-local $1000-$1FFF.
    if ((bankBase == 0x0000 || bankBase == 0x8000) && vicAddress >= 0x1000 && vicAddress < 0x2000)
        return mem->readCharROM(vicAddress & 0x0FFF);

    const uint16_t cpuAddress = static_cast<uint16_t>(bankBase | vicAddress);

    return mem->readRAM(cpuAddress);
}

uint8_t Bus::vicReadColor(uint16_t address) const
{
    if (!mem)
        return 0x0F;

    if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
        return mem->readColorRAM(address - COLOR_MEMORY_START);

    return 0x0F;
}

uint8_t Bus::readForDMA(uint16_t address)
{
    auto sampleOpenBus = [&]() -> uint8_t
    {
        return dataBus ? dataBus->sample() : 0xFF;
    };

    auto driveMemory = [&](uint8_t value) -> uint8_t
    {
        if (dataBus)
            dataBus->drive(value, DataBusLatch::Driver::Memory);

        return value;
    };

    auto driveCartridge = [&](uint8_t value) -> uint8_t
    {
        if (dataBus)
            dataBus->drive(value, DataBusLatch::Driver::Cartridge);

        return value;
    };

    // 6510 internal port.
    if (address == 0x0000)
        return driveMemory(cpu6510Port->readDDR());

    if (address == 0x0001)
        return driveMemory(cpu6510Port->readPort());

    // Mapper gets first opportunity just as with normal CPU reads.
    if (cart && cartridgeAttached && cart->cpuMemoryHandledByMapper(address))
        return cart->read(address);

    if (!pla)
        return sampleOpenBus();

    const PLA::memoryAccessInfo accessInfo = pla->getMemoryAccess(address);

    switch (accessInfo.bank)
    {
        case PLA::RAM:
            return driveMemory(mem->readRAM(accessInfo.offset));

        case PLA::KERNAL_ROM:
            return driveMemory(mem->readKernalROM(accessInfo.offset));

        case PLA::BASIC_ROM:
            return driveMemory(mem->readBASICROM(accessInfo.offset));

        case PLA::CHARACTER_ROM:
            return driveMemory(mem->readCharROM(accessInfo.offset));

        case PLA::IO:
        {
            if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
            {
                const uint8_t lowNibble = mem->readColorRAM(address - COLOR_MEMORY_START);

                const uint8_t upperNibble = dataBus ? static_cast<uint8_t>(dataBus->sample() & 0xF0) : 0xF0;

                return driveMemory(static_cast<uint8_t>(upperNibble | lowNibble));
            }

            return readIO(accessInfo.offset);
        }

        case PLA::CARTRIDGE_LO:
        {
            if (romLOverlayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return cart->readRAM(accessInfo.offset);

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return cart->read(address);

            return driveCartridge(mem->readCartridge(accessInfo.offset, cartLocation::LO));
        }

        case PLA::CARTRIDGE_HI:
        {
            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return cart->readRAM(accessInfo.offset);

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return cart->read(address);

            return driveCartridge(mem->readCartridge(accessInfo.offset, cartLocation::HI));
        }

        case PLA::CARTRIDGE_HI_E000:
        {
            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return cart->readRAM(accessInfo.offset);

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return cart->read(address);

            return driveCartridge(mem->readCartridge(accessInfo.offset, cartLocation::HI_E000));
        }

        case PLA::UNMAPPED:
        default:
            return sampleOpenBus();
    }
}

void Bus::writeForDMA(uint16_t address, uint8_t value)
{
    if (address == 0x0000)
    {
        if (cpu6510Port)
            cpu6510Port->writeDDR(value);
        return;
    }

    if (address == 0x0001)
    {
        if (cpu6510Port)
            cpu6510Port->writePort(value);
        return;
    }

    if (cart && cart->cpuMemoryHandledByMapper(address))
    {
        cart->write(address, value);
        return;
    }

    if (!pla || !mem)
        return;

    const PLA::memoryAccessInfo accessInfo =
        pla->getMemoryAccess(address);

    switch (accessInfo.bank)
    {
        case PLA::RAM:
            mem->writeRAM(accessInfo.offset, value);
            break;

        case PLA::IO:
            if (address >= COLOR_MEMORY_START &&
                address <= COLOR_MEMORY_END)
            {
                mem->writeColorRAM(
                    static_cast<uint16_t>(address - COLOR_MEMORY_START),
                    value);
                return;
            }

            writeIO(accessInfo.offset, value);
            break;

        case PLA::KERNAL_ROM:
        case PLA::BASIC_ROM:
        case PLA::CHARACTER_ROM:
            // DMA writes under ROM go to underlying RAM.
            mem->writeRAM(address, value);
            break;

        case PLA::CARTRIDGE_LO:
            mem->writeRAM(address, value);

            if (romLOverlayIsRAM &&
                cart &&
                cart->hasCartridgeRAM())
            {
                cart->writeRAM(accessInfo.offset, value);
            }

            if (cart && cart->romWriteEnabled(address))
                cart->write(address, value);

            break;

        case PLA::CARTRIDGE_HI:
            mem->writeRAM(address, value);

            if (romHOverLayIsRAM &&
                cart &&
                cart->hasCartridgeRAM())
            {
                cart->writeRAM(accessInfo.offset, value);
            }

            if (cart && cart->romWriteEnabled(address))
                cart->write(address, value);

            break;

        case PLA::CARTRIDGE_HI_E000:
            mem->writeRAM(address, value);

            if (romHOverLayIsRAM &&
                cart &&
                cart->hasCartridgeRAM())
            {
                cart->writeRAM(accessInfo.offset, value);
            }

            break;

        case PLA::UNMAPPED:
        default:
            break;
    }
}

uint8_t Bus::peek(uint16_t address) const
{
    if (!mem)
        return 0xFF;

    if (address == 0x0000)
        return cpu6510Port ? cpu6510Port->readDDR() : 0xFF;

    if (address == 0x0001)
        return cpu6510Port ? cpu6510Port->readPort() : 0xFF;

    if (cart && cartridgeAttached && cart->cpuMemoryHandledByMapper(address))
        return cart->peek(address);

    if (!pla)
        return dataBus ? dataBus->sample() : 0xFF;

    const PLA::memoryAccessInfo accessInfo = pla->getMemoryAccess(address);

    switch (accessInfo.bank)
    {
        case PLA::RAM:
            return mem->readRAM(accessInfo.offset);

        case PLA::KERNAL_ROM:
            return mem->readKernalROM(accessInfo.offset);

        case PLA::BASIC_ROM:
            return mem->readBASICROM(accessInfo.offset);

        case PLA::CHARACTER_ROM:
            return mem->readCharROM(accessInfo.offset);

        case PLA::CARTRIDGE_LO:
        {
            if (romLOverlayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return cart->peekRAM(accessInfo.offset);

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return cart->peek(address);

            return mem->readCartridge(accessInfo.offset, cartLocation::LO);
        }

        case PLA::CARTRIDGE_HI:
        {
            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return cart->peekRAM(accessInfo.offset);

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return cart->peek(address);

            return mem->readCartridge(accessInfo.offset, cartLocation::HI);
        }

        case PLA::CARTRIDGE_HI_E000:
        {
            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return cart->peekRAM(accessInfo.offset);

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return cart->peek(address);

            return mem->readCartridge(accessInfo.offset, cartLocation::HI_E000);
        }

        case PLA::IO:
        {
            if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
            {
                const uint8_t lowNibble = mem->readColorRAM(address - COLOR_MEMORY_START);
                const uint8_t upperNibble = dataBus ? static_cast<uint8_t>(dataBus->sample() & 0xF0) : 0xF0;

                return static_cast<uint8_t>(upperNibble | lowNibble);
            }

            return peekIO(accessInfo.offset);
        }

        case PLA::UNMAPPED:
        default:
            return dataBus ? dataBus->sample() : 0xFF;
    }
}

uint8_t Bus::peekIO(uint16_t address) const
{
    if (address >= IO_VIC_START && address <= IO_VIC_END)
    {
        const uint16_t mirroredAddress = static_cast<uint16_t>((address & 0x003F) + 0xD000);

        if (vic)
            return vic->peekRegister(mirroredAddress);
    }
    else if (address >= IO_SID_START && address <= IO_SID_END)
    {
        const uint16_t mirroredAddress = static_cast<uint16_t>((address & 0x001F) + 0xD400);

        if (sid)
            return sid->peekRegister(mirroredAddress);
    }
    else if (address >= IO_CIA1_START && address <= IO_CIA1_END)
    {
        const uint16_t mirroredAddress = static_cast<uint16_t>((address & 0x000F) + 0xDC00);

        if (cia1)
            return cia1->peekRegister(mirroredAddress);
    }
    else if (address >= IO_CIA2_START && address <= IO_CIA2_END)
    {
        const uint16_t mirroredAddress = static_cast<uint16_t>((address & 0x000F) + 0xDD00);

        if (cia2)
            return cia2->peekRegister(mirroredAddress);
    }
    else if (address >= IO1_START && address <= IO2_END)
    {
        if (swiftLink && swiftLink->handlesAddress(address))
            return swiftLink->peek(address);

        if (turbo232 && turbo232->handlesAddress(address))
            return turbo232->peek(address);

        if (reu && reu->isEnabled() && address >= 0xDF00 && address <= 0xDF0A)
            return reu->peekIO(address);

        if (cart && cartridgeAttached)
            return cart->peek(address);
    }

    return dataBus ? dataBus->sample() : 0xFF;
}

void Bus::setCassetteSenseLow(bool low)
{
    if (cpu6510Port)
        cpu6510Port->setCassetteSenseLow(low);
}

bool Bus::getCassetteSenseLow() const
{
    return cpu6510Port ? cpu6510Port->getCassetteSenseLow() : false;
}

bool Bus::isCassetteMotorOn() const
{
    return cpu6510Port ? cpu6510Port->isCassetteMotorOn() : false;
}

void Bus::writeRAM(uint16_t address, uint8_t value)
{
    if (mem)
        mem->writeRAM(address, value);
}

void Bus::write16(uint16_t address, uint16_t value)
{
    if (!mem)
        return;

    mem->write16(address, value);
}

void Bus::writeDirect(uint16_t address, uint8_t value)
{
    if (!mem)
        return;

    mem->writeDirect(address, value);
}

uint8_t Bus::readRAM(uint16_t address) const
{
    return mem ? mem->readRAM(address) : 0xFF;
}
