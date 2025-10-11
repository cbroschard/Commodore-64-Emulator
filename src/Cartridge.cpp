// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/C64GameSystemMapper.h"
#include "Cartridge/DinamicMapper.h"
#include "Cartridge/EasyFlashMapper.h"
#include "Cartridge/FunPlayMapper.h"
#include "Cartridge/GenericMapper.h"
#include "Cartridge/MagicDeskMapper.h"
#include "Cartridge/OceanMapper.h"
#include "Cartridge/RossMapper.h"
#include "Cartridge/SimonsBasicMapper.h"
#include "Cartridge/StructuredBasicMapper.h"
#include "Cartridge/SuperGamesMapper.h"
#include "Cartridge/SuperZaxxonMapper.h"

Cartridge::Cartridge() :
    hasRAM(false),
    currentBank(0),
    wiringMode(WiringMode::NONE),
    cartSize(0),
    mapperType(CartridgeType::GENERIC),
    setLogging(false)
{
    // defaults
    header.exROMLine = true;
    header.gameLine = true;
}

Cartridge::~Cartridge() = default;

bool Cartridge::loadROM(const std::string& path)
{
    if (!(loadFile(path, romData)))
    {
        throw std::runtime_error("Error: Unable to load the ROM file!");
        return false;
    }

    if (romData.size() < sizeof(header))
    {
        if (logger && setLogging)
        {
            std::stringstream out;
            out << "Error: Cartridge:: file is not correct size!" << std::endl;
            logger->WriteLog(out.str());
        }
        return false;
    }

    // Parse the header info
    std::memcpy(&header, romData.data(), sizeof(header));

    if (std::strncmp(header.magic, "C64 CARTRIDGE   ", sizeof(header.magic)) != 0)
    {
        if (logger && setLogging)
        {
            std::stringstream out;
            out << "Error: Cartridge:: file is not a C64 cartridge!" << std::endl;
            logger->WriteLog(out.str());
        }
        return false;
    }

    if (!processChipSections())
    {
        if (logger && setLogging)
        {
            std::stringstream out;
            out << "Error: Unable to read any chip sections in the ROM!" << std::endl;
            logger->WriteLog(out.str());
        }
        return false;
    }

    // Make sure we found at least one chip section
    if (chipSections.empty())
    {
        if (logger && setLogging)
        {
            std::stringstream out;
            out << "Error: Unable to find a CHIP section in the ROM!" << std::endl;
            logger->WriteLog(out.str());
        }
        return false;
    }

    // Detect type of cartridge
    mapperType = detectType(swap16(header.CartridgeHardwareType));

    switch(mapperType)
    {
        case CartridgeType::C64_GAME_SYSTEM:
        {
            mapper = std::make_unique<C64GameSystemMapper>();
            mapper->attachCartridgeInstance(this);
            mapper->attachMemoryInstance(mem);
            mapper->loadIntoMemory(0);
            break;
        }
        case CartridgeType::DINAMIC:
        {
            mapper = std::make_unique<DinamicMapper>();
            mapper->attachCartridgeInstance(this);
            mapper->attachMemoryInstance(mem);
            mapper->loadIntoMemory(0);
            break;
        }
        case CartridgeType::EASYFLASH:
        {
            mapper = std::make_unique<EasyFlashMapper>();
            mapper->attachCartridgeInstance(this);
            mapper->attachMemoryInstance(mem);
            mapper->loadIntoMemory(0);
            break;
        }
        case CartridgeType::KCS_POWER:
        case CartridgeType::WESTERMANN:
        case CartridgeType::GENERIC:
        {
            mapper = std::make_unique<GenericMapper>();
            mapper->attachCartridgeInstance(this);
            mapper->attachMemoryInstance(mem);
            mapper->loadIntoMemory(0);
            break;
        }
        case CartridgeType::OCEAN:
        {
            mapper = std::make_unique<OceanMapper>();
            mapper->attachCartridgeInstance(this);
            mapper->attachMemoryInstance(mem);
            mapper->loadIntoMemory(0);
            break;
        }
        case CartridgeType::ROSS:
        {
            mapper = std::make_unique<RossMapper>();
            mapper->attachCartridgeInstance(this);
            mapper->attachMemoryInstance(mem);
            mapper->loadIntoMemory(0);
            break;
        }
        case CartridgeType::STRUCTURED_BASIC:
        {
            mapper = std::make_unique<StructuredBasicMapper>();
            mapper->attachCartridgeInstance(this);
            mapper->attachMemoryInstance(mem);
            mapper->loadIntoMemory(0);
            break;
        }
        case CartridgeType::SUPER_GAMES:
        {
            mapper = std::make_unique<SuperGamesMapper>();
            mapper->attachCartridgeInstance(this);
            mapper->attachMemoryInstance(mem);
            mapper->loadIntoMemory(0);
            break;
        }
        case CartridgeType::SUPER_ZAXXON:
        {
            mapper = std::make_unique<SuperZaxxonMapper>();
            mapper->attachCartridgeInstance(this);
            mapper->attachMemoryInstance(mem);
            mapper->loadIntoMemory(0);
            break;
        }
        case CartridgeType::SIMONS_BASIC:
        {
            mapper = std::make_unique<SimonsBasicMapper>();
            mapper->attachCartridgeInstance(this);
            mapper->attachMemoryInstance(mem);
            mapper->loadIntoMemory(0);
            break;
        }
        case CartridgeType::MAGICDESK:
        {
            mapper = std::make_unique<MagicDeskMapper>();
            mapper->attachCartridgeInstance(this);
            mapper->attachMemoryInstance(mem);
            mapper->loadIntoMemory(0);
            break;
        }
        case CartridgeType::FUN_PLAY:
        {
            mapper = std::make_unique<FunPlayMapper>();
            mapper->attachCartridgeInstance(this);
            mapper->attachMemoryInstance(mem);
            mapper->loadIntoMemory(0);
            break;
        }
        default:
        {
            setCurrentBank(chipSections[0].bankNumber);
            break;
        }
    }
    return true;
}

Cartridge::CartridgeType Cartridge::detectType(uint16_t type)
{
    switch (type)
    {
        case 0x00: return CartridgeType::GENERIC;
        case 0x01: return CartridgeType::ACTION_REPLAY;
        case 0x02: return CartridgeType::KCS_POWER;
        case 0x03: return CartridgeType::FINAL_CARTRIDGE_III;
        case 0x04: return CartridgeType::SIMONS_BASIC;
        case 0x05: return CartridgeType::OCEAN;
        case 0x07: return CartridgeType::FUN_PLAY;
        case 0x08: return CartridgeType::SUPER_GAMES;
        case 0x0B: return CartridgeType::WESTERMANN;
        case 0x0E: return CartridgeType::C64_GAME_SYSTEM;
        case 0x11: return CartridgeType::DINAMIC;
        case 0x12: return CartridgeType::SUPER_ZAXXON;
        case 0x13: return CartridgeType::MAGICDESK;
        case 0x16: return CartridgeType::STRUCTURED_BASIC;
        case 0x17: return CartridgeType::ROSS;
        case 0x20: return CartridgeType::EASYFLASH;
        default:   return CartridgeType::UNKNOWN;
    }
}

std::string Cartridge::getMapperName() const
{
    switch (mapperType)
    {
        case CartridgeType::GENERIC:              return "Generic 8K/16K";
        case CartridgeType::ACTION_REPLAY:        return "Action Replay";
        case CartridgeType::KCS_POWER:            return "KCS Power Cartridge";
        case CartridgeType::FINAL_CARTRIDGE_III:  return "Final Cartridge III";
        case CartridgeType::SIMONS_BASIC:         return "Simon's BASIC";
        case CartridgeType::OCEAN:                return "Ocean";
        case CartridgeType::FUN_PLAY:             return "Fun Play";
        case CartridgeType::SUPER_GAMES:          return "Super Games";
        case CartridgeType::WESTERMANN:           return "Westermann Learning";
        case CartridgeType::C64_GAME_SYSTEM:      return "C64 Game System, System 3";
        case CartridgeType::DINAMIC:              return "DINAMIC";
        case CartridgeType::SUPER_ZAXXON:         return "Zaxxon, Super Zaxxon";
        case CartridgeType::MAGICDESK:            return "Magic Desk";
        case CartridgeType::STRUCTURED_BASIC:     return "Structured BASIC";
        case CartridgeType::ROSS:                 return "ROSS";
        case CartridgeType::EASYFLASH:            return "EasyFlash";
        case CartridgeType::UNKNOWN:              return "Unknown cartridge format";
    }
    // Default
    return "Unknown";
}

uint8_t Cartridge::read(uint16_t address)
{
    if (mapper)
    {
        return mapper->read(address);
    }

    switch(mapperType)
    {
        default:
        {
            break;
        }
    }

    // Cartridge has RAM so return it
    if (hasRAM && address < ramData.size())
    {
        return ramData[address];
    }
    return 0xFF; // Open bus
}

void Cartridge::write(uint16_t address, uint8_t value)
{
    if (mapper)
    {
        mapper->write(address, value);
        return;
    }

    switch (mapperType)
    {
        case CartridgeType::ACTION_REPLAY:
        {
            // TODO: add logic
            break;
        }
        default:
            break;
    }
    if (hasRAM && address < ramData.size())
    {
        ramData[address] = value;
        return;
    }
}

bool Cartridge::setCurrentBank(uint8_t bank)
{
    bool bankFound = false;
    for (const auto &section : chipSections)
    {
        if (section.bankNumber == bank)
        {
            bankFound = true;
            break;
        }
    }
    if (!bankFound)
    {
        if (logger && setLogging)
        {
            std::stringstream out;
            out << "Attempted to set invalid bank: " << bank << std::endl;
            logger->WriteLog(out.str());
        }
        return false;
    }
    currentBank = bank;

    // Refresh memory mapping after switching banks
    if (!loadIntoMemory())
    {
        if (logger && setLogging)
        {
            std::stringstream out;
            out << "Error: Unable to reload cartridge data for bank " << bank << std::endl;
            logger->WriteLog(out.str());
        }
        return false;
    }
    return true;
}

uint16_t Cartridge::getNumberOfBanks() const
{
    std::set<uint16_t> uniqueBanks;
    for (const auto &section : chipSections)
    {
        uniqueBanks.insert(section.bankNumber);
    }
    return static_cast<uint16_t>(uniqueBanks.size());
}

bool Cartridge::hasSectionAt(uint16_t address) const
{
    for (const auto& section : chipSections)
    {
        if (section.loadAddress == address)
        return true;
    }
    return false;
}

bool Cartridge::loadFile(const std::string& path, std::vector<uint8_t>& buffer)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        if (logger && setLogging)
        {
            std::stringstream out;
            out << "Failed to open file: " << path << std::endl;
            logger->WriteLog(out.str());
        }
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    buffer.resize(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        if (logger && setLogging)
        {
            std::stringstream out;
            out << "Failed to read file: " << path << std::endl;
            logger->WriteLog(out.str());
        }
        return false;
    }

    std::cout << "Loaded ROM file: " << path << " (" << size << " bytes)" << std::endl;
    return true;
}

bool Cartridge::loadIntoMemory()
{
    if (!mem)
    {
        throw std::runtime_error("Unable to load cartridge as there is no pointer to memory!");
    }

    if (mapper)
    {
        return mapper->loadIntoMemory(currentBank);
    }

    // Iterate through only the sections for the current bank.
    for (const auto &section : chipSections)
    {
        if (section.bankNumber != currentBank)
            continue;

        // Check for a 16K chunk and split it.
        if (section.data.size() == 16384)
        {
            // Load First 8K into LO
            {
                cartLocation location = cartLocation::LO;
                uint16_t loBase = CART_LO_START;
                // Calculate an optional offset if loadAddress isn't exactly CART_LO_START.
                uint16_t loOffset = (section.loadAddress >= loBase) ? section.loadAddress - loBase : 0;
                std::cout << "Loading 16K section: LO bank at address 0x" << std::hex << (loBase + loOffset) << std::endl;
                for (size_t i = 0; i < 8192; ++i)
                {
                    mem->writeCartridge(loOffset + i, section.data[i], location);
                }
            }
            // Load Second 8K into HI
            {
                cartLocation location = cartLocation::HI;
                // Determine HI base based on header flags.
                uint16_t hiBase = (header.exROMLine && !header.gameLine) ? CART_HI_START1 : CART_HI_START;
                std::cout << "Loading 16K section: HI bank at base address 0x" << std::hex << hiBase << std::endl;
                // For HI, we write starting at offset 0.
                for (size_t i = 8192; i < section.data.size(); ++i)
                {
                    mem->writeCartridge(i - 8192, section.data[i], location);
                }
            }
        }
        // If the section is exactly 8K, load it into the proper bank as indicated by its load address.
        else if (section.data.size() == 8192)
        {
            cartLocation location;
            uint16_t baseAddress;
            if (section.loadAddress == CART_LO_START)
            {
                location = cartLocation::LO;
                baseAddress = CART_LO_START;
            }
            else if (section.loadAddress == CART_HI_START || section.loadAddress == CART_HI_START1)
            {
                location = cartLocation::HI;
                baseAddress = (header.exROMLine && !header.gameLine) ? CART_HI_START1 : CART_HI_START;
            }
            else
            {
                // Default to LO if load address is unrecognized.
                location = cartLocation::LO;
                baseAddress = CART_LO_START;
            }
            std::cout << "Loading 8K section into " << ((location == cartLocation::LO) ? "LO" : "HI")
                      << " bank, starting at base 0x" << std::hex << baseAddress << std::endl;
            for (size_t i = 0; i < section.data.size(); i++)
            {
                uint16_t offset = (section.loadAddress - baseAddress) + i;
                mem->writeCartridge(offset, section.data[i], location);
            }
        }
        else
        {
            // For any non-standard size, use a safe fallback.
            std::cout << "Loading non-standard section of size " << section.data.size()
                      << " into default LO bank at address 0x" << std::hex << section.loadAddress << std::endl;
            for (size_t i = 0; i < section.data.size(); ++i)
            {
                mem->writeCartridge(section.loadAddress + i, section.data[i], cartLocation::LO);
            }
        }
    }
    return true;
}

void Cartridge::clearCartridge(cartLocation location)
{
    if (!mem)
    {
        throw std::runtime_error("Unable to clear cartridge memory, no memory object");
    }

    const uint16_t area_size = 8192; // 8KB

    // Iterate through all offsets in the 8KB block and write 0xFF
    for (uint16_t offset = 0; offset < area_size; ++offset)
    {
        mem->writeCartridge(offset, 0xFF, location);
    }
}

bool Cartridge::processChipSections()
{
    size_t offset = sizeof(header); // 64 Byte header

    // Loop through until we run out of sections to process
    while (offset < romData.size())
    {
        if (offset + sizeof(crtChipHeader) > romData.size())
        {
            if (logger && setLogging)
            {
                std::stringstream out;
                out << "Error: not enough space in file to process chip header!" << std::endl;
                logger->WriteLog(out.str());
            }
            return false;
        }
        // Read from the current offset
        crtChipHeader chipHdr;
        std::memcpy(&chipHdr, romData.data() + offset, sizeof(chipHdr));

        // Convert multi-byte fields from big-endian to host endianness.
        chipHdr.packetLength = swap32(chipHdr.packetLength);
        chipHdr.chipType     = swap16(chipHdr.chipType);
        chipHdr.bankNumber   = swap16(chipHdr.bankNumber);
        chipHdr.loadAddress  = swap16(chipHdr.loadAddress);
        chipHdr.romSize      = swap16(chipHdr.romSize);

        if (std::strncmp(chipHdr.signature, "CHIP", 4) != 0)
        {
            if (logger && setLogging)
            {
                std::stringstream out;
                out << "Error: Invalid chip section signature!" << std::endl;
                logger->WriteLog(out.str());
            }
            return false;
        }

        offset += sizeof(chipHdr);

        // Validate packet length
        uint32_t expectedPacketLength = sizeof(crtChipHeader) + chipHdr.romSize;
        if (expectedPacketLength != chipHdr.packetLength)
        {
            if (logger && setLogging)
            {
                std::stringstream out;
                out <<  "Error: Mismatch in expected packet length! Expected " << expectedPacketLength << " and got "
                        << chipHdr.packetLength << std::endl;
                logger->WriteLog(out.str());
            }
            return false;
        }

        // Validate size is not past romData size
        if (sizeof(crtChipHeader) + chipHdr.romSize > romData.size())
        {
            if (logger && setLogging)
            {
                std::stringstream out;
                out << "Error: Unable to load ROM CHIP as it's larger than ROM data size!" << std::endl;
                logger->WriteLog(out.str());
            }
            return false;
        }

        // Store the current bank of chip data
        chipSection section;
        section.chipType = chipHdr.chipType;
        section.bankNumber = chipHdr.bankNumber;
        section.loadAddress = chipHdr.loadAddress;
        section.data.resize(chipHdr.romSize);

        std::memcpy(section.data.data(), romData.data() + offset, chipHdr.romSize);

        switch (chipHdr.chipType)
        {
            case 0: // ROM CHIP
                {
                    chipSections.push_back(section);
                    cartSize += chipHdr.romSize;   // accumulate total ROM size
                    break;
                }
            case 1: // RAM CHIP
                {
                    // Update RamData to size of CHIP and copy data in
                    hasRAM = true;
                    ramData = section.data;
                    break;
                }
            default:
            {
                if (logger && setLogging)
                {
                    std::stringstream out;
                    out << "Unsupported chip type: " << static_cast<int>(chipHdr.chipType) << std::endl;
                    logger->WriteLog(out.str());
                }
                return false;
            }
        }

        // Update the offset to the next CHIP section
        offset += chipHdr.romSize;
    }

    // Set lines based on cart type
    determineWiringMode();

    return true;
}

void Cartridge::determineWiringMode()
{
    bool has8000 = false;
    bool hasA000 = false;
    bool hasE000 = false;
    bool has16K  = false;

    for (auto& s : chipSections)
    {
        if (s.loadAddress == 0x8000 && s.data.size() == 16384)
            has16K = true;  // single 16K block at $8000
        if (s.loadAddress == 0x8000 && s.data.size() == 8192)
            has8000 = true;
        if (s.loadAddress == 0xA000 && s.data.size() == 8192)
            hasA000 = true;
        if (s.loadAddress == 0xE000 && s.data.size() == 8192)
            hasE000 = true;
    }

    // Determine wiring based on load addresses & sizes
    if (hasE000)
    {
        wiringMode = WiringMode::CART_ULTIMAX;
        setExROMLine(true);
        setGameLine(false);
    }
    else if (has16K || (has8000 && hasA000))
    {
        wiringMode = WiringMode::CART_16K;
        setExROMLine(false);
        setGameLine(false);
    }
    else if (has8000 && !hasA000)
    {
        wiringMode = WiringMode::CART_8K;
        setExROMLine(false);
        setGameLine(true);
    }
    else
    {
        wiringMode = WiringMode::NONE;
        setExROMLine(true);
        setGameLine(true);
    }
}
