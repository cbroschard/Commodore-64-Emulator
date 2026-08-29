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
#include "Memory.h"
#include "PLA.h"
#include "REU.h"
#include "SID/SID.h"
#include "Expansion/SwiftLink.h"
#include "Expansion/Turbo232.h"
#include "Vic.h"

Memory::Memory() :
    cart(nullptr),
    cia1(nullptr),
    cia2(nullptr),
    cass(nullptr),
    cpu(nullptr),
    dataBus(nullptr),
    pla(nullptr),
    reu(nullptr),
    sid(nullptr),
    swiftLink(nullptr),
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

    if (cass) motorOn ? cass->startMotor() : cass->stopMotor();

    // Update PLA MCR with the latch bits (0..2 matter)
    if (pla) pla->updateMemoryControlRegister(effective & 0x07);
}
