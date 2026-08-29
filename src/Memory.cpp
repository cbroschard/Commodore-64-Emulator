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

void Memory::write16(uint16_t address, uint16_t value)
{
    writeRAM(address, static_cast<uint8_t>(value & 0xFF));
    writeRAM(static_cast<uint16_t>(address + 1), static_cast<uint8_t>((value >> 8) & 0xFF));
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
    if (address < MAX_MEMORY)
        mem[address] = value;
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
