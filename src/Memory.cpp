// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cassette.h"
#include "CIA1.h"
#include "CIA2.h"
#include "CPU.h"
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

Memory::Memory() :
    cart(nullptr),
    cia1(nullptr),
    cia2(nullptr),
    cass(nullptr),
    cpu(nullptr),
    dataBus(nullptr),
    debugManager(nullptr),
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
    romHOverLayIsRAM(false),
    cassetteSenseLow(false),
    dataDirectionRegister(0x2F),
    port1OutputLatch(0x37)
{
    mem.resize(MAX_MEMORY,0);
    basicROM.resize(BASIC_ROM_SIZE,0);
    kernalROM.resize(KERNAL_ROM_SIZE,0);
    charROM.resize(CHAR_ROM_SIZE,0);
    colorRAM.resize(COLOR_RAM_SIZE,0);
    cart_lo.resize(CART_LO_SIZE,0);
    cart_hi.resize(CART_HI_SIZE,0);
    cart_hi_e000.resize(CART_HI_E000_SIZE,0);

    applyPort1SideEffects(computeEffectivePort1(port1OutputLatch, dataDirectionRegister));
}

Memory::~Memory() = default;

void Memory::saveState(StateWriter& wrtr) const
{
    // MEM0 = "Core"
    wrtr.beginChunk("MEM0");
    wrtr.writeU32(1); //version

    // Dump main memory
    wrtr.writeVectorU8(mem);

    // Dump Color RAM
    wrtr.writeVectorU8(colorRAM);

    // Dump CPU port $00/$01 mapping controls
    wrtr.writeU8(dataDirectionRegister);
    wrtr.writeU8(port1OutputLatch);

    // Dump Misc
    wrtr.writeBool(cartridgeAttached);

    // Dump Cartridge Lo/Hi
    wrtr.writeVectorU8(cart_lo);
    wrtr.writeVectorU8(cart_hi);
    wrtr.writeVectorU8(cart_hi_e000);

    // End the chunk for CIA1
    wrtr.endChunk();
}

bool Memory::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "MEM0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                              { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                                       { rdr.exitChunkPayload(chunk); return false; }

        // Load Main memory
        if (!rdr.readVectorU8(mem))                                         { rdr.exitChunkPayload(chunk); return false; }

        // Load Color RAM
        if (!rdr.readVectorU8(colorRAM))                                    { rdr.exitChunkPayload(chunk); return false; }

        // Load CPU port $00/$01 mapping controls
        if (!rdr.readU8(dataDirectionRegister))                             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(port1OutputLatch))                                  { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(cartridgeAttached))                               { rdr.exitChunkPayload(chunk); return false; }

        // Load cart vectors
        if (!rdr.readVectorU8(cart_lo))                                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU8(cart_hi))                                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readVectorU8(cart_hi_e000))                                { rdr.exitChunkPayload(chunk); return false; }

        // Re-apply port $01 side effects (PLA mapping + cassette motor)
        applyPort1SideEffects(computeEffectivePort1(port1OutputLatch, dataDirectionRegister));

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Not our chunk
    return false;
}

uint8_t Memory::read(uint16_t address)
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
    {
        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_PORT))
        {
            std::ostringstream out;

            out << "[MEM:PORT] read $0000 DDR=$"
                << std::hex
                << std::uppercase
                << std::setw(2)
                << std::setfill('0')
                << int(dataDirectionRegister);

            traceMgr->recordCustomEvent(out.str(), traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, vic ? vic->getCurrentRaster() : 0,
                    vic ? vic->getRasterDot() : 0));
        }

        return memoryRead(dataDirectionRegister);
    }

    if (address == 0x0001)
    {
        const uint8_t outputs = static_cast<uint8_t>(port1OutputLatch & dataDirectionRegister);
        uint8_t inputs = static_cast<uint8_t>(~dataDirectionRegister);

        if (cassetteSenseLow)
            inputs = static_cast<uint8_t>(inputs & ~0x10);
        else
            inputs = static_cast<uint8_t>(inputs | 0x10);

        // Bits 6 and 7 read high.
        inputs = static_cast<uint8_t>(inputs | 0xC0);

        const uint8_t value = static_cast<uint8_t>(outputs | inputs);

        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_PORT))
        {
            std::ostringstream out;

            out << "[MEM:PORT] read $0001 value=$"
                << std::hex
                << std::uppercase
                << std::setw(2)
                << std::setfill('0')
                << int(value)
                << " DDR=$"
                << std::setw(2)
                << int(dataDirectionRegister)
                << " latch=$"
                << std::setw(2)
                << int(port1OutputLatch)
                << " sense="
                << (cassetteSenseLow ? "L" : "H");

            traceMgr->recordCustomEvent(out.str(), traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, vic ? vic->getCurrentRaster() : 0,
                    vic ? vic->getRasterDot() : 0));
        }

        return memoryRead(value);
    }

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
            if (accessInfo.offset >= mem.size())
                throw std::runtime_error("Error: Attempt to read past end of RAM");

            return memoryRead(mem[accessInfo.offset]);
        }

        case PLA::KERNAL_ROM:
        {
            if (accessInfo.offset >= kernalROM.size())
                throw std::runtime_error("Error: Attempt to read past end of KERNAL ROM");

            return memoryRead(kernalROM[accessInfo.offset]);
        }

        case PLA::BASIC_ROM:
        {
            if (accessInfo.offset >= basicROM.size())
                throw std::runtime_error("Error: Attempt to read past end of BASIC ROM");

            return memoryRead(basicROM[accessInfo.offset]);
        }

        case PLA::CHARACTER_ROM:
        {
            if (accessInfo.offset >= charROM.size())
                throw std::runtime_error("Error: Attempt to read past end of CHARACTER ROM");

            return memoryRead(charROM[accessInfo.offset]);
        }

        case PLA::CARTRIDGE_LO:
        {
            if (romLOverlayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return deviceRead(cart->readRAM(accessInfo.offset));

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return deviceRead(cart->read(address));

            if (accessInfo.offset >= cart_lo.size())
                throw std::runtime_error("Error: Attempt to read past end of cartridge LO");

            return cartridgeRead(cart_lo[accessInfo.offset]);
        }

        case PLA::CARTRIDGE_HI:
        {
            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return deviceRead(cart->readRAM(accessInfo.offset));

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return deviceRead(cart->read(address));

            if (accessInfo.offset >= cart_hi.size())
                throw std::runtime_error("Error: Attempt to read past end of cartridge HI");

            return cartridgeRead(cart_hi[accessInfo.offset]);
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

            if (accessInfo.offset >= cart_hi_e000.size())
                throw std::runtime_error("Error: Attempt to read past end of cartridge HI_E000");

            return cartridgeRead(cart_hi_e000[accessInfo.offset]);
        }

        case PLA::IO:
        {
            if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
            {
                const uint8_t lowNibble = static_cast<uint8_t>(colorRAM[address - COLOR_MEMORY_START] & 0x0F);
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

uint8_t Memory::readRAM(uint16_t address) const
{
    return mem[address];
}

uint8_t Memory::readKernalROM(uint16_t address) const
{
    return kernalROM[address];
}

uint8_t Memory::readBASICROM(uint16_t address) const
{
    return basicROM[address];
}

uint8_t Memory::readCharROM(uint16_t address) const
{
    return charROM[address];
}

uint8_t Memory::readColorRAM(uint16_t address) const
{
    return colorRAM[address] & 0x0F;
}

uint8_t Memory::peek(uint16_t address) const
{
    if (address == 0x0000)
        return dataDirectionRegister;

    if (address == 0x0001)
    {
        const uint8_t outputs =  static_cast<uint8_t>(port1OutputLatch & dataDirectionRegister);

        uint8_t inputs = static_cast<uint8_t>(~dataDirectionRegister);

        if (cassetteSenseLow)
            inputs = static_cast<uint8_t>(inputs & ~0x10);
        else
            inputs = static_cast<uint8_t>(inputs | 0x10);

        inputs = static_cast<uint8_t>(inputs | 0xC0);

        return static_cast<uint8_t>(outputs | inputs);
    }

    if (cart && cartridgeAttached && cart->cpuMemoryHandledByMapper(address))
        return cart->read(address);

    if (!pla)
        return dataBus ? dataBus->sample() : 0xFF;

    const PLA::memoryAccessInfo accessInfo = pla->getMemoryAccess(address);

    switch (accessInfo.bank)
    {
        case PLA::RAM:
        {
            if (accessInfo.offset >= mem.size())
                return dataBus ? dataBus->sample() : 0xFF;

            return mem[accessInfo.offset];
        }

        case PLA::KERNAL_ROM:
        {
            if (accessInfo.offset >= kernalROM.size())
                return dataBus ? dataBus->sample() : 0xFF;

            return kernalROM[accessInfo.offset];
        }

        case PLA::BASIC_ROM:
        {
            if (accessInfo.offset >= basicROM.size())
                return dataBus ? dataBus->sample() : 0xFF;

            return basicROM[accessInfo.offset];
        }

        case PLA::CHARACTER_ROM:
        {
            if (accessInfo.offset >= charROM.size())
                return dataBus ? dataBus->sample() : 0xFF;

            return charROM[accessInfo.offset];
        }

        case PLA::CARTRIDGE_LO:
        {
            if (romLOverlayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return cart->readRAM(accessInfo.offset);

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return cart->read(address);

            if (accessInfo.offset >= cart_lo.size())
                return dataBus ? dataBus->sample() : 0xFF;

            return cart_lo[accessInfo.offset];
        }

        case PLA::CARTRIDGE_HI:
        {
            if (romHOverLayIsRAM && cart && cartridgeAttached &&  cart->hasCartridgeRAM())
                return cart->readRAM(accessInfo.offset);

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return cart->read(address);

            if (accessInfo.offset >= cart_hi.size())
                return dataBus ? dataBus->sample() : 0xFF;

            return cart_hi[accessInfo.offset];
        }

        case PLA::CARTRIDGE_HI_E000:
        {
            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return cart->readRAM(accessInfo.offset);

            // Added mapper hook for Ultimax ROMH.
            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return cart->read(address);

            if (accessInfo.offset >= cart_hi_e000.size())
                return dataBus ? dataBus->sample() : 0xFF;

            return cart_hi_e000[accessInfo.offset];
        }

        case PLA::IO:
        {
            if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
            {
                const size_t index = static_cast<size_t>(address - COLOR_MEMORY_START);

                if (index >= colorRAM.size())
                    return dataBus ? dataBus->sample() : 0xFF;

                const uint8_t upperNibble = dataBus ? static_cast<uint8_t>(dataBus->sample() & 0xF0) : 0xF0;
                return static_cast<uint8_t>(upperNibble | (colorRAM[index] & 0x0F));
            }

            return peekIO(accessInfo.offset);
        }

        case PLA::UNMAPPED:
        default:
        {
            return dataBus ? dataBus->sample() : 0xFF;
        }
    }
}

uint8_t Memory::peekIO(uint16_t address) const
{
    (void)address;
    return dataBus ? dataBus->sample() : 0xFF;
}

uint8_t Memory::vicRead(uint16_t vicAddress, uint16_t raster)
{
    // Enforce 14-bit address
    vicAddress &= 0x3FFF;

    // Grab the VIC bank for this raster
    uint16_t bankBase = vic ? vic->getBankBaseFromVIC(raster) : 0;

    // Check the char base for special cases
    if ((bankBase == 0x0000 || bankBase == 0x8000) && vicAddress >= 0x1000 && vicAddress < 0x2000)
        return charROM[vicAddress & 0x0FFF];

    uint16_t cpuAddress = (vicAddress & 0x3FFF) | bankBase;
    return mem[cpuAddress];
}

uint8_t Memory::vicReadColor(uint16_t address) const
{
    if (address >= 0xD800 && address <= 0xDBFF)
        return colorRAM[address - 0xD800] & 0x0F;

    return 0x0F; // out of bounds
}

uint16_t Memory::read16(uint16_t addr)
{
    uint8_t lo = read(addr);
    uint8_t hi = read(addr + 1);
    return static_cast<uint16_t>(lo | (hi << 8));
}

uint8_t Memory::readForDMA(uint16_t address)
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

    if (address == 0x0000)
        return driveMemory(dataDirectionRegister);

    if (address == 0x0001)
    {
        const uint8_t outputs = static_cast<uint8_t>(port1OutputLatch & dataDirectionRegister);

        uint8_t inputs = static_cast<uint8_t>(~dataDirectionRegister);

        if (cassetteSenseLow)
            inputs = static_cast<uint8_t>(inputs & ~0x10);
        else
            inputs = static_cast<uint8_t>(inputs | 0x10);

        inputs = static_cast<uint8_t>(inputs | 0xC0);

        const uint8_t value = static_cast<uint8_t>(outputs | inputs);

        return driveMemory(value);
    }

    if (cart && cartridgeAttached && cart->cpuMemoryHandledByMapper(address))
        // Cartridge::read() already drives Driver::Cartridge.
        return cart->read(address);

    if (!pla)
        return sampleOpenBus();

    const PLA::memoryAccessInfo accessInfo =
        pla->getMemoryAccess(address);

    switch (accessInfo.bank)
    {
        case PLA::RAM:
        {
            if (accessInfo.offset >= mem.size())
                return sampleOpenBus();

            return driveMemory(mem[accessInfo.offset]);
        }

        case PLA::KERNAL_ROM:
        {
            if (accessInfo.offset >= kernalROM.size())
                return sampleOpenBus();

            return driveMemory(kernalROM[accessInfo.offset]);
        }

        case PLA::BASIC_ROM:
        {
            if (accessInfo.offset >= basicROM.size())
                return sampleOpenBus();

            return driveMemory(basicROM[accessInfo.offset]);
        }

        case PLA::CHARACTER_ROM:
        {
            if (accessInfo.offset >= charROM.size())
                return sampleOpenBus();

            return driveMemory(charROM[accessInfo.offset]);
        }

        case PLA::IO:
        {
            if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
            {
                const uint8_t lowNibble = static_cast<uint8_t>(colorRAM[address - COLOR_MEMORY_START] & 0x0F);
                const uint8_t upperNibble = dataBus ? static_cast<uint8_t>(dataBus->sample() & 0xF0) : 0xF0;
                const uint8_t value = static_cast<uint8_t>(upperNibble | lowNibble);
                return driveMemory(value);
            }

            // VIC, SID, CIA, REU, or Cartridge should drive
            // the latch inside their own read functions.
            return readIO(accessInfo.offset);
        }

        case PLA::CARTRIDGE_LO:
        {
            if (romLOverlayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                // Cartridge::readRAM() drives the latch.
                return cart->readRAM(accessInfo.offset);

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                // Cartridge::read() drives the latch.
                return cart->read(address);

            if (accessInfo.offset >= cart_lo.size())
                return sampleOpenBus();

            // Stored in Memory, but physically supplied by cartridge.
            return driveCartridge(cart_lo[accessInfo.offset]);
        }

        case PLA::CARTRIDGE_HI:
        {
            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return cart->readRAM(accessInfo.offset);

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return cart->read(address);

            if (accessInfo.offset >= cart_hi.size())
                return sampleOpenBus();

            return driveCartridge(cart_hi[accessInfo.offset]
            );
        }

        case PLA::CARTRIDGE_HI_E000:
        {
            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                return cart->readRAM(accessInfo.offset);

            if (cart && cartridgeAttached && cart->romReadHandledByMapper(address))
                return cart->read(address);

            if (accessInfo.offset >= cart_hi_e000.size())
                return sampleOpenBus();

            return driveCartridge(cart_hi_e000[accessInfo.offset]);
        }

        case PLA::UNMAPPED:
        default:
            return sampleOpenBus();
    }
}

uint8_t Memory::readIO(uint16_t address)
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

void Memory::write(uint16_t address, uint8_t value)
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
        dataDirectionRegister = value;
        uint8_t effective = computeEffectivePort1(port1OutputLatch, dataDirectionRegister);

        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_PORT))
        {
            std::ostringstream out;
            out << "[MEM:PORT] write $0000 DDR=$"
                << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                << " effective=$" << std::setw(2) << int(effective);
            traceMgr->recordCustomEvent(out.str(), traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, vic ? vic->getCurrentRaster() : 0,
                                    vic ? vic->getRasterDot() : 0));
        }

        applyPort1SideEffects(effective);

        return;
    }
    else if (address == 0x0001)
    {
        port1OutputLatch = value;
        uint8_t effective = computeEffectivePort1(port1OutputLatch, dataDirectionRegister);

        if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_PORT))
        {
            std::ostringstream out;
            out << "[MEM:PORT] write $0001 latch=$"
                << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(port1OutputLatch)
                << " effective=$" << std::setw(2) << int(effective);
            traceMgr->recordCustomEvent(out.str(), traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0, vic ? vic->getCurrentRaster() : 0,
                                    vic ? vic->getRasterDot() : 0));
        }

        applyPort1SideEffects(effective);

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
            if (accessInfo.offset >= mem.size())
                throw std::runtime_error("Error: Attempt to write past end of memory!");

            mem[accessInfo.offset] = value;
            break;
        }
        case PLA::IO:
        {
            if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
            {
                colorRAM[address - COLOR_MEMORY_START] = value & 0x0F;
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
            mem[address] = value;
            break;
        }
        case PLA::CARTRIDGE_LO:
        {
            mem[address] = value;

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
            mem[address] = value;

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
            mem[address] = value;

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

void Memory::write16(uint16_t address, uint16_t value)
{
    write(address, value & 0xFF); // low byte
    write(address + 1, value >> 8); // high byte
}

void Memory::writeRAM(uint16_t address, uint8_t value)
{
    mem[address] = value;
}

void Memory::writeColorRAM(uint16_t address, uint8_t value)
{
    colorRAM[address] = value & 0x0F;
}

void Memory::writeDirect(uint16_t address, uint8_t value)
{
    if (address >= 0xD000 && address <= 0xDFFF )
    {
        writeIO(address, value);
    }
    else if (address < MAX_MEMORY)
    {
        mem[address] = value;
    }
    else
    {
        #ifdef Debug
        std::cout << "Error: Write direct attempted to write past end of memory!" << std::endl;
        #endif
    }

    if (monitor && monitor->checkWatchWrite(address, value))
    {
        if (debugManager)
            debugManager->onWatchpoint();
        else
            monitor->enterMonitor();
    }
}

void Memory::writeForDMA(uint16_t address, uint8_t value)
{
    if (address == 0x0000)
    {
        dataDirectionRegister = value;
        applyPort1SideEffects(computeEffectivePort1(port1OutputLatch, dataDirectionRegister));
        return;
    }

    if (address == 0x0001)
    {
        port1OutputLatch = value;
        applyPort1SideEffects(computeEffectivePort1(port1OutputLatch, dataDirectionRegister));
        return;
    }

    if (cart && cartridgeAttached && cart->cpuMemoryHandledByMapper(address))
    {
        cart->write(address, value);
        return;
    }

    if (!pla)
        return;

    PLA::memoryAccessInfo accessInfo = pla->getMemoryAccess(address);

    switch (accessInfo.bank)
    {
        case PLA::RAM:
            mem[accessInfo.offset] = value;
            break;

        case PLA::IO:
            if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
            {
                colorRAM[address - COLOR_MEMORY_START] = value & 0x0F;
                return;
            }

            writeIO(accessInfo.offset, value);
            break;

        case PLA::KERNAL_ROM:
        case PLA::BASIC_ROM:
        case PLA::CHARACTER_ROM:
            // Writes under ROM go to underlying RAM.
            mem[address] = value;
            break;

        case PLA::CARTRIDGE_LO:
            mem[address] = value;

            if (romLOverlayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                cart->writeRAM(accessInfo.offset, value);

            if (cart && cartridgeAttached && cart->romWriteEnabled(address))
                cart->write(address, value);

            break;

        case PLA::CARTRIDGE_HI:
            mem[address] = value;

            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                cart->writeRAM(accessInfo.offset, value);

            if (cart && cartridgeAttached && cart->romWriteEnabled(address))
                cart->write(address, value);

            break;

        case PLA::CARTRIDGE_HI_E000:
            mem[address] = value;

            if (romHOverLayIsRAM && cart && cartridgeAttached && cart->hasCartridgeRAM())
                cart->writeRAM(accessInfo.offset, value);

            break;

        case PLA::UNMAPPED:
        default:
            break;
    }
}

uint8_t Memory::readCartridge(uint16_t offset, cartLocation location) const
{
    switch (location)
    {
        case cartLocation::LO:
            if (offset >= cart_lo.size())
                throw std::runtime_error("Error: Attempt to read past end of cartridge lo");
            return cart_lo[offset];

        case cartLocation::HI:
            if (offset >= cart_hi.size())
                throw std::runtime_error("Error: Attempt to read past end of cartridge hi");
            return cart_hi[offset];

        case cartLocation::HI_E000:
            if (offset >= cart_hi_e000.size())
                throw std::runtime_error("Error: Attempt to read past end of cartridge hi e000");
            return cart_hi_e000[offset];

        default:
            return 0xFF;
    }
}

void Memory::writeCartridge(uint16_t address, uint8_t value, cartLocation location)
{
    switch(location)
    {
        case cartLocation::LO:
        {
            if (address < cart_lo.size())
                cart_lo[address] = value;
            else
                throw std::runtime_error("Error: Attempt to write past end of cartridge lo size");
            break;
        }
        case cartLocation::HI:
        {
            if (address < cart_hi.size())
                cart_hi[address] = value;
            else
                throw std::runtime_error("Error: Attempt to write past end of cartridge hi size");
            break;
        }
        case cartLocation::HI_E000:
        {
            if (address < cart_hi_e000.size())
                cart_hi_e000[address] = value;
            else
                throw std::runtime_error("Error: Attempt to write past end of cartridge hi e000 size");
            break;
        }
        default:
            break;
    }
}

void Memory::writeIO(uint16_t address, uint8_t value)
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

bool Memory::load_ROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    std::streamsize fileSize = file.tellg();
    if (static_cast<size_t>(fileSize) != expectedSize)
        return false;

    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(targetBuffer.data()), expectedSize))
        return false;

    file.close();
    return true;
}

bool Memory::Initialize(const std::string& basic, const std::string& kernal, const std::string& character)
{
    // Initialize RAM to 0
    for (size_t i = 0; i < mem.size(); ++i)
    {
        mem[i] = (i & 0x40) ? 0xFF : 0x00;
    }

    // Load each ROM and check for successful load
    if (!load_ROM(basic, basicROM, 0x2000, "BASIC") || !load_ROM(kernal, kernalROM, 0x2000, "Kernal") || !load_ROM(character, charROM, 0x1000, "CHAR"))
    {
        return false;
    }
    else
    {
        return true;
    }
}

uint8_t Memory::computeEffectivePort1(uint8_t latch, uint8_t ddr)
{
    // Inputs read back as 1 (pull-ups)
    uint8_t invDDR = static_cast<uint8_t>(~ddr);
    return static_cast<uint8_t>((latch & ddr) | invDDR);
}

void Memory::applyPort1SideEffects(uint8_t effective)
{
    // Bit 5 low => motor ON (active low)
    bool motorOn = (effective & 0x20) == 0;

    if (traceMgr && traceMgr->memDetailOn(TraceManager::TraceDetail::MEM_PORT))
    {
        std::ostringstream out;
        out << "[MEM:PORT] sidefx effective=$"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(effective)
            << " motor=" << (motorOn ? "ON" : "OFF")
            << " pla=$"  << std::setw(2) << int(effective & 0x07);
        traceMgr->recordCustomEvent(out.str(),
            traceMgr->makeStamp(cpu ? cpu->getTotalCycles() : 0,
                                vic ? vic->getCurrentRaster() : 0,
                                vic ? vic->getRasterDot() : 0));
    }

    if (cass) motorOn ? cass->startMotor() : cass->stopMotor();

    // Update PLA MCR with the latch bits (0..2 matter)
    if (pla) pla->updateMemoryControlRegister(effective & 0x07);
}
