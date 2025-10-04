// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Memory.h"

Memory::Memory() :
    cartridgeAttached(false),
    cassetteSenseLow(false),
    dataDirectionRegister(0x2F),
    port1OutputLatch(0x37),
    setLogging(false)
{
    mem.resize(MAX_MEMORY,0);
    basicROM.resize(BASIC_ROM_SIZE,0);
    kernalROM.resize(KERNAL_ROM_SIZE,0);
    charROM.resize(CHAR_ROM_SIZE,0);
    colorRAM.resize(COLOR_RAM_SIZE,0);
    cart_lo.resize(CART_LO_SIZE,0);
    cart_hi.resize(CART_HI_SIZE,0);
}

Memory::~Memory() = default;

uint8_t Memory::read(uint16_t address)
{
    // Wrap every return so read-watches can trigger exactly once per CPU read.
    auto RET = [&](uint8_t v)->uint8_t
    {
        if (monitor && monitor->checkWatchRead(address, v)) {
            monitor->enter();
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
    else if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
    {
        return RET(colorRAM[address - COLOR_MEMORY_START] & 0x0F);
    }
    else if ((address >= 0x8000 && address <= 0x9FFF) && cartridgeAttached && cart->getMapperName() == "Zaxxon, Super Zaxxon")
    {
        return RET(cart->read(address));
    }

    if (!pla) throw std::runtime_error("Error: Missing PLA object!");

    PLA::memoryAccessInfo accessInfo = pla->getMemoryAccess(address);
    switch(accessInfo.bank)
    {
        case PLA::RAM:
        {
            // Special handling for CPU vectors if KERNAL is hidden
            if (address == 0xFFFE || address == 0xFFFF)
            {
            uint8_t lo = mem[0xFFFE], hi = mem[0xFFFF];
            if (lo == 0x00 && hi == 0x00)
            {
                return RET((address == 0xFFFE) ? mem[0x0314] : mem[0x0315]); // fallback
            }
            return RET((address == 0xFFFE) ? lo : hi); // real RAM vector
            }

            if (address == 0xFFFA || address == 0xFFFB)
            {
                uint8_t lo = mem[0xFFFA], hi = mem[0xFFFB];
                if (lo == 0x00 && hi == 0x00)
                {
                    return RET((address == 0xFFFA) ? mem[0x0318] : mem[0x0319]); // fallback
                }
                return RET((address == 0xFFFA) ? lo : hi); // real RAM vector
            }
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
            return RET(readIO(accessInfo.offset));
        }
        case PLA::UNMAPPED:
        {
            if (logger && setLogging)
            {
                logger->WriteLog("Attempt to read from unmapped address: " + std::to_string(address));
            }
            return RET(0xFF);
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

    // Check if bank 0 or 2 and return CHAR ROM if so
    uint16_t bankBase = cia2object ? cia2object->getCurrentVICBank() : 0;

    // Ask VIC which char base is latched for this raster as well as graphics mode
    uint16_t charBase = vicII ? vicII->getCHARBase(raster) : 0;

    // Check the char base for special cases
    if (!pla->isUltimax() && (bankBase == 0x0000 || bankBase == 0x8000) && vicAddress >= 0x1000 && vicAddress < 0x2000)
    {
        return charROM[vicAddress & 0x0FFF];
    }

    // Special case handling for programs that only load 1k of Char ROM
    if (charBase == 0x0800 && (vicAddress >= 0x0C00 && vicAddress < 0x1000) && (bankBase == 0x0000 || bankBase == 0x8000))
    {
        return charROM[(vicAddress - 0x0C00) + 0x0800];
    }

    uint16_t cpuAddress = vicAddress | bankBase;
    return mem[cpuAddress];
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
    return 0xFF;
}

void Memory::write(uint16_t address, uint8_t value)
{
    if (!pla) throw std::runtime_error("Error: Missing PLA object!");

    if (address == 0x0000)
    {
        dataDirectionRegister = value;
        uint8_t effective = computeEffectivePort1(port1OutputLatch, dataDirectionRegister);
        applyPort1SideEffects(effective);
    }
    else if (address == 0x0001)
    {
        port1OutputLatch = value;
        uint8_t effective = computeEffectivePort1(port1OutputLatch, dataDirectionRegister);
        applyPort1SideEffects(effective);
    }
    else if (address >= COLOR_MEMORY_START && address <= COLOR_MEMORY_END)
    {
        colorRAM[address - COLOR_MEMORY_START] = value & 0x0F;
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
            writeIO(accessInfo.offset, value);
            mem[address] = value; // Update underlying RAM
            break;
        }
        case PLA::KERNAL_ROM:
        case PLA::BASIC_ROM:
        case PLA::CHARACTER_ROM:
        {
            if (accessInfo.offset < mem.size())
            {
                mem[address] = value;
            }
            break;
        }
        case PLA::CARTRIDGE_LO:
        case PLA::CARTRIDGE_HI:
        {
            // Write the value to the requested RAM address
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
    if (monitor && monitor->checkWatchWrite(address, value))
    {
        // Enter the monitor as we hit a watch point
        monitor->enter();
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
        std::cout << "Error: Write direct attempted to write past end of memory!" << std::endl;
    }
    if (monitor && monitor->checkWatchWrite(address, value))
    {
        monitor->enter();
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

    // Update PLA MCR with the effective bits (0..2 matter)
    pla->updateMemoryControlRegister(effective & 0x07);
}
