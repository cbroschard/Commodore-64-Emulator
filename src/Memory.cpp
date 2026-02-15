// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Memory.h"

Memory::Memory() :
    cart(nullptr),
    cia1object(nullptr),
    cia2object(nullptr),
    cass(nullptr),
    processor(nullptr),
    logger(nullptr),
    monitor(nullptr),
    pla(nullptr),
    sidchip(nullptr),
    traceMgr(nullptr),
    vicII(nullptr),
    cartridgeAttached(false),
    cassetteSenseLow(false),
    dataDirectionRegister(0x2F),
    port1OutputLatch(0x37),
    lastBus(0xFF),
    setLogging(false)
{
    mem.resize(MAX_MEMORY,0);
    basicROM.resize(BASIC_ROM_SIZE,0);
    kernalROM.resize(KERNAL_ROM_SIZE,0);
    charROM.resize(CHAR_ROM_SIZE,0);
    colorRAM.resize(COLOR_RAM_SIZE,0);
    cart_lo.resize(CART_LO_SIZE,0);
    cart_hi.resize(CART_HI_SIZE,0);

    applyPort1SideEffects(computeEffectivePort1(port1OutputLatch, dataDirectionRegister));
}

Memory::~Memory() = default;

void Memory::saveState(StateWriter& wrtr) const
{
    // MEM0 = "Core"
    wrtr.beginChunk("MEM0");

    // Dump main memory
    wrtr.writeVectorU8(mem);

    // Dump Color RAM
    wrtr.writeVectorU8(colorRAM);

    // Dump CPU port $00/$01 mapping controls
    wrtr.writeU8(dataDirectionRegister);
    wrtr.writeU8(port1OutputLatch);

    // Dump Misc
    wrtr.writeU8(lastBus);
    wrtr.writeBool(cartridgeAttached);

    // Dump Cartridge Lo/Hi
    wrtr.writeVectorU8(cart_lo);
    wrtr.writeVectorU8(cart_hi);

    // End the chunk for CIA1
    wrtr.endChunk();
}

bool Memory::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "MEM0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        // Load Main memory
        if (!rdr.readVectorU8(mem))      return false;

        // Load Color RAM
        if (!rdr.readVectorU8(colorRAM)) return false;

        rdr.skipChunk(chunk);
        return true;
    }
    return true;
}

uint8_t Memory::read(uint16_t address)
{
    // Wrap every return so read-watches can trigger exactly once per CPU read.
    auto RET = [&](uint8_t v)->uint8_t
    {
        lastBus = v; // Update Open Bus value

        // Check for trace enabled
        if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::MEM) && traceMgr->memRangeContains(address))
        {
            uint16_t PC = processor ? processor->getPC() : 0;
            TraceManager::Stamp stamp = traceMgr->makeStamp(processor ? processor->getTotalCycles() : 0,
                            vicII ? vicII->getCurrentRaster() : 0, vicII ? vicII->getRasterDot() : 0);
            traceMgr->recordMemRead(address, v, PC, stamp);
        }

        // Check for watch hit and enter monitor
        if (monitor && monitor->checkWatchRead(address, v))
        {
            monitor->enterMonitor();
        }
        return v;
    };

    if (address == 0x0000)
    {
        return RET(dataDirectionRegister);
    }
    else if (address == 0x0001)
    {
        uint8_t outputs = (port1OutputLatch & dataDirectionRegister);
        uint8_t inputs  = static_cast<uint8_t>(~dataDirectionRegister);
        inputs = (cassetteSenseLow) ? static_cast<uint8_t>(inputs & ~0x10)
                                    : static_cast<uint8_t>(inputs |  0x10);

        // Bits 6-7 read as 1 (no hardware there)
        inputs |= 0xC0;

        uint8_t valueToReturn = static_cast<uint8_t>(outputs | inputs);
        return RET(valueToReturn);
    }

    if (!pla) throw std::runtime_error("Error: Missing PLA object!");

    PLA::memoryAccessInfo accessInfo = pla->getMemoryAccess(address);
    switch(accessInfo.bank)
    {
        case PLA::RAM:
        {
            if (accessInfo.offset >= mem.size())
            {
                throw std::runtime_error("Error: Attempt to read past end of RAM");
            }
            return RET(mem[accessInfo.offset]);
        }
        case PLA::KERNAL_ROM:
        {
            if (accessInfo.offset >= kernalROM.size())
            {
                throw std::runtime_error("Error: Attempt to read past end of KERNAL ROM");
            }
            return RET(kernalROM[accessInfo.offset]);
        }
        case PLA::BASIC_ROM:
        {
            if (accessInfo.offset >= basicROM.size())
            {
                throw std::runtime_error("Error: Attempt to read past end of BASIC ROM");
            }
            return RET(basicROM[accessInfo.offset]);
        }
        case PLA::CHARACTER_ROM:
        {
            if (accessInfo.offset >= charROM.size())
            {
                throw std::runtime_error("Error: Attempt to read past end of CHARACTER ROM");
            }
            return RET(charROM[accessInfo.offset]);
        }
        case PLA::CARTRIDGE_LO:
        {
            if (accessInfo.offset >= cart_lo.size())
            {
                throw std::runtime_error("Error: Attempt to read past end of cartridge lo RAM");
            }
            return RET(cart_lo[accessInfo.offset]);
        }
        case PLA::CARTRIDGE_HI:
        {
            if (accessInfo.offset >= cart_hi.size())
            {
                throw std::runtime_error("Error: Attempt to read past end of cartridge hi RAM");
            }
            return RET(cart_hi[accessInfo.offset]);
        }
        case PLA::IO:
        {
            // Color RAM is visible to CPU only when IO is mapped (CHAREN=1)
            if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
            {
                const uint8_t nib = colorRAM[address - COLOR_MEMORY_START] & 0x0F;
                return RET(uint8_t(0xF0 | nib));  // hi nibble = 1111 on real C64
            }
            return RET(readIO(accessInfo.offset));
        }
        case PLA::UNMAPPED:
        {
            if (logger && setLogging)
            {
                logger->WriteLog("Attempt to read from unmapped address: " + std::to_string(address));
            }
            return RET(lastBus); // Open Bus
        }
    }
    // Default for no match
    if (logger && setLogging)
    {
        logger->WriteLog("Attempt to read from invalid address: " + std::to_string(address));
    }
    return RET(0xFF); // invalid address
}

uint8_t Memory::vicRead(uint16_t vicAddress, uint16_t raster)
{
    // Enforce 14-bit address
    vicAddress &= 0x3FFF;

    // Grab the VIC bank for this raster
    uint16_t bankBase = vicII ? vicII->getBankBaseFromVIC(raster) : 0;

    // Check the char base for special cases
    if ((bankBase == 0x0000 || bankBase == 0x8000) && vicAddress >= 0x1000 && vicAddress < 0x2000)
    {
        return charROM[vicAddress & 0x0FFF];
    }

    uint16_t cpuAddress = (vicAddress & 0x3FFF) | bankBase;
    return mem[cpuAddress];
}

uint8_t Memory::vicReadColor(uint16_t address) const
{
    if (address >= 0xD800 && address <= 0xDBFF)
    {
        return colorRAM[address - 0xD800] & 0x0F;
    }
    return 0x0F; // out of bounds
}

uint16_t Memory::read16(uint16_t addr)
{
    uint8_t lo = read(addr);
    uint8_t hi = read(addr + 1);
    return static_cast<uint16_t>(lo | (hi << 8));
}

uint8_t Memory::readIO(uint16_t address)
{

    if (address >= IO_VIC_START && address <= IO_VIC_END)
    {
        // Handle VIC address mirroring
        uint16_t mirroredAddress = (address & 0x003F) + 0xD000; // Mask out everything except the lower 6 bits
        if (vicII)
        {
            return vicII->readRegister(mirroredAddress);
        }
    }
    else if (address >= IO_SID_START && address <= IO_SID_END)
    {
        // Handle SID address mirroring
        uint16_t mirroredAddress = (address & 0x001F) + 0xD400;
        if (sidchip)
        {
            return sidchip->readRegister(mirroredAddress);
        }
    }
    else if (address >= IO_CIA1_START && address <= IO_CIA1_END)
    {
        // Handle CIA1 address mirroring
        uint16_t mirroredAddress = (address & 0x000F) + 0xDC00;
        if (cia1object)
        {
            return cia1object->readRegister(mirroredAddress);
        }
    }
    else if (address >= IO_CIA2_START && address <= IO_CIA2_END)
    {
        // Handle CIA2 address mirroring
        uint16_t mirroredAddress = (address & 0x000F) + 0xDD00;
        if (cia2object)
        {
            return cia2object->readRegister(mirroredAddress);
        }
    }
    else if (address >= 0xDE00 && address <= 0xDFFF)
    {
        if (cart && cartridgeAttached)
        {
            return cart->read(address);
        }
        return lastBus;
    }
    else
    {
        if (logger && setLogging)
        {
            std::stringstream message;
            message << "Unknown I/O read at address: " << std::hex << address << std::endl;
            logger->WriteLog(message.str());
        }
    }
    return lastBus;
}

void Memory::write(uint16_t address, uint8_t value)
{
    if (!pla) throw std::runtime_error("Error: Missing PLA object!");

    // Update last bus
    lastBus = value;

    // Check for trace enabled and write if so
    if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::MEM) && traceMgr->memRangeContains(address))
        {
            uint16_t PC = processor ? processor->getPC() : 0;
            TraceManager::Stamp stamp = traceMgr->makeStamp(processor ? processor->getTotalCycles() : 0,
                            vicII ? vicII->getCurrentRaster() : 0, vicII ? vicII->getRasterDot() : 0);
            traceMgr->recordMemWrite(address, value, PC, stamp);
    }

    if (address == 0x0000)
    {
        dataDirectionRegister = value;
        uint8_t effective = computeEffectivePort1(port1OutputLatch, dataDirectionRegister);
        applyPort1SideEffects(effective);
        if (logger && setLogging)
        {
            std::stringstream out;
            out << "Updated DDR to value: " << static_cast<int>(value) << " computed effective: " << static_cast<int>(effective);
            logger->WriteLog(out.str());
        }
        return;
    }
    else if (address == 0x0001)
    {
        port1OutputLatch = value & 0x3F;
        uint8_t effective = computeEffectivePort1(port1OutputLatch, dataDirectionRegister);
        applyPort1SideEffects(effective);
        if (logger && setLogging)
        {
            std::stringstream out;
            out << "Updated MCR to value: " << static_cast<int>(value) << " computed effective: " << static_cast<int>(effective);
            logger->WriteLog(out.str());
        }
        return;
    }

    PLA::memoryAccessInfo accessInfo = pla->getMemoryAccess(address);

    switch(accessInfo.bank)
    {
        case PLA::RAM:
        {
            if (accessInfo.offset >= mem.size())
            {
                throw std::runtime_error("Error: Attempt to write past end of memory!");
            }
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
        case PLA::CARTRIDGE_HI:
        {
            // Write the value to the requested RAM address and also the cartridge
            if (cart) cart->write(address, value);
            mem[address] = value;
            break;
        }
        case PLA::UNMAPPED:
        {
            if (logger && setLogging)
            {
                logger->WriteLog("Attempt to write to unmapped address: " + std::to_string(address));
            }
            break;
        }
    }
    if (monitor && monitor->checkWatchWrite(accessInfo.offset, value))
    {
        // Enter the monitor as we hit a watch point
        monitor->enterMonitor();
    }
}

void Memory::write16(uint16_t address, uint16_t value)
{
    write(address, value & 0xFF); // low byte
    write(address + 1, value >> 8); // high byte
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

    lastBus = value;

    if (monitor && monitor->checkWatchWrite(address, value))
    {
        monitor->enterMonitor();
    }
}

void Memory::writeCartridge(uint16_t address, uint8_t value, cartLocation location)
{
    switch(location)
    {
        case cartLocation::LO:
        {
            if (address < cart_lo.size())
            {
                cart_lo[address] = value;
            }
            else
            {
                throw std::runtime_error("Error: Attempt to write past end of cartridge lo size");
            }
            break;
        }
        case cartLocation::HI:
        {
            if (address < cart_hi.size())
            {
                cart_hi[address] = value;
            }
            else
            {
                throw std::runtime_error("Error: Attempt to write past end of cartridge hi size");
            }
            break;
        }
        default:
        {
            if (logger && setLogging)
            {
                logger->WriteLog("Attempt to write to unknown cartridge vector");
            }
            break;
        }
    }
}

void Memory::writeIO(uint16_t address, uint8_t value)
{
    if (address >= 0xD000 && address <= 0xD3FF)
    {
        uint16_t mirroredAddress = (address & 0x003F) + 0xD000;
        if (vicII)
        {
            vicII->writeRegister(mirroredAddress,value);
            return;
        }
    }
    else if (address >= 0xD400 && address <= 0xD7FF)
    {
        uint16_t mirroredAddress = (address & 0x001F) + 0xD400;
        if (sidchip)
        {
            sidchip->writeRegister(mirroredAddress,value);
            return;
        }
    }
    else if (address >= 0xDC00 && address <= 0xDCFF)
    {
        uint16_t mirroredAddress = (address & 0x000F) + 0xDC00;
        if (cia1object)
        {
            cia1object->writeRegister(mirroredAddress,value);
            return;
        }
    }

    else if (address >= 0xDD00 && address <= 0xDDFF)
    {
        uint16_t mirroredAddress = (address & 0x000F) + 0xDD00;
        if (cia2object)
        {
            cia2object->writeRegister(mirroredAddress,value);
            return;
        }
    }
    else if (address >= 0xDE00 && address <= 0xDFFF)
    {
        if (cart && cartridgeAttached)
        {
            cart->write(address, value);
            return;
        }
    }
    else
    {
        if (logger && setLogging)
        {
            logger->WriteLog("Unknown I/O write at address: " + std::to_string(address));
        }
    }
}

bool Memory::load_ROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        if (logger && setLogging)
        {
            logger->WriteLog("Unable to open " + romName + " ROM file: " + filename);
        }
        return false;
    }

    std::streamsize fileSize = file.tellg();
    if (static_cast<size_t>(fileSize) != expectedSize)
    {
        if (logger && setLogging)
        {
        logger->WriteLog("Error: " + romName + " ROM file is not correct size! Expected " +
                         std::to_string(expectedSize) + " bytes, got " + std::to_string(fileSize) + " bytes.");
        }
        return false;
    }

    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(targetBuffer.data()), expectedSize))
    {
        if (logger && setLogging)
        {
            logger->WriteLog("Error: Failed to read " + romName + " ROM file: " + filename);
        }
        return false;
    }

    file.close();
    return true;
}

bool Memory::Initialize(const std::string& basic, const std::string& kernal, const std::string& character)
{
    // Initialize RAM to 0
    std::fill(mem.begin(), mem.end(), 0x00);

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
