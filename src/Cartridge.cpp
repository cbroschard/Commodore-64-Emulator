// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/ActionReplayMapper.h"
#include "Cartridge/C64GameSystemMapper.h"
#include "Cartridge/DinamicMapper.h"
#include "Cartridge/EasyFlashMapper.h"
#include "Cartridge/EpyxFastloadMapper.h"
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
    processor(nullptr),
    logger(nullptr),
    mem(nullptr),
    traceMgr(nullptr),
    vicII(nullptr),
    wiringMode(WiringMode::NONE),
    cartSize(0),
    exROMLine(true),
    gameLine(true),
    mapperType(CartridgeType::GENERIC),
    setLogging(false)
{
    // defaults
    header.exROMLine = true;
    header.gameLine = true;
}

Cartridge::~Cartridge() = default;

void Cartridge::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("CART");
    wrtr.writeU32(2); // version

    // Dump Active bank
    wrtr.writeU8(currentBank);

    // Dump Wiring
    wrtr.writeU8(static_cast<uint8_t>(wiringMode));

    // Dump mapper type (so we can recreate it on load)
    wrtr.writeU16(static_cast<uint16_t>(mapperType));

    // Dump full CRT contents so we can rebuild chipSections/mapper on load
    wrtr.writeVectorU8(romData);

    // Dump GAME / EXROM
    wrtr.writeBool(gameLine);
    wrtr.writeBool(exROMLine);

    // Dump If cartridge has RAM
    wrtr.writeBool(hasRAM);
    if (hasRAM)
        wrtr.writeVectorU8(ramData);

    // Dump mapper by calling actual mapper's save state method
    if (mapper)
        mapper->saveState(wrtr);

    wrtr.endChunk();
}

bool Cartridge::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "CART", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver)) { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 2)          { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(currentBank)) { rdr.exitChunkPayload(chunk); return false; }

    uint8_t wiringU8 = 0;
    if (!rdr.readU8(wiringU8)) { rdr.exitChunkPayload(chunk); return false; }
    wiringMode = static_cast<WiringMode>(wiringU8);

    // mapperType + romData
    uint16_t mapperU16 = 0;
    if (!rdr.readU16(mapperU16)) { rdr.exitChunkPayload(chunk); return false; }
    mapperType = static_cast<CartridgeType>(mapperU16);

    if (!rdr.readVectorU8(romData)) { rdr.exitChunkPayload(chunk); return false; }

    // Rebuild header + chipSections from romData
    chipSections.clear();
    hasRAM = false;
    cartSize = 0;

    if (romData.size() >= sizeof(header))
    {
        std::memcpy(&header, romData.data(), sizeof(header));
        if (!processChipSections()) { rdr.exitChunkPayload(chunk); return false; }
    }
    else
    {
        rdr.exitChunkPayload(chunk);
        return false;
    }

    // Read GAME/EXROM saved values (keeps exact wiring you had at save time)
    bool game = false, exrom = false;
    if (!rdr.readBool(game))  { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(exrom)) { rdr.exitChunkPayload(chunk); return false; }

    // Drive the actual PLA lines
    setGameLine(game);
    setExROMLine(exrom);

    // RAM snapshot
    if (!rdr.readBool(hasRAM)) { rdr.exitChunkPayload(chunk); return false; }
    if (hasRAM)
    {
        if (!rdr.readVectorU8(ramData)) { rdr.exitChunkPayload(chunk); return false; }
    }
    else
    {
        ramData.clear();
    }

    // recreate mapper NOW (before consuming subchunks)
    mapper.reset();
    mapper = createMapper(mapperType);
    if (mapper)
    {
        mapper->attachCartridgeInstance(this);
        mapper->attachMemoryInstance(mem);
    }

    // Mapper subchunks (now it can actually load them)
    StateReader::Chunk sub{};
    while (rdr.nextChunk(sub))
    {
        bool handled = false;
        if (mapper) handled = mapper->loadState(sub, rdr);
        if (!handled) rdr.skipChunk(sub);
    }

    rdr.exitChunkPayload(chunk);

    // Apply mapping after load
    if (mapper)
    {
        if (!mapper->applyMappingAfterLoad())
            return false;
    }
    else
    {
        // Generic/no-mapper case
        if (!chipSections.empty())
        {
            if (!setCurrentBank(currentBank))
                return false;
        }
    }

    return true;
}

bool Cartridge::loadROM(const std::string& path)
{
    // Clear everything first
    mapper.reset();
    chipSections.clear();
    romData.clear();
    ramData.clear();
    hasRAM = false;
    cartSize = 0;
    currentBank = 0;
    wiringMode = WiringMode::NONE;
    mapperType = CartridgeType::GENERIC;
    header.exROMLine = true;
    header.gameLine = true;
    exROMLine = true;
    gameLine = true;

    if (!(loadFile(path, romData)))
    {
        throw std::runtime_error("Error: Unable to load the ROM file!");
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

    // Initialize live pin levels from the CRT header (as a starting point)
    exROMLine = header.exROMLine ? true : false;
    gameLine  = header.gameLine  ? true : false;

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

    // Create mapper (or nullptr for UNKNOWN => “no mapper”)
    mapper = createMapper(mapperType);

    // Choose a sane initial bank (bank 0 if present, else lowest bank)
    currentBank = selectInitialBank(chipSections);

    // Decide GAME/EXROM wiring using header + chip layout
    determineWiringMode();

    if (mapper)
    {
        mapper->attachCartridgeInstance(this);
        mapper->attachMemoryInstance(mem);

        // Check if cartridge type requires the CPU
        if (auto* cpuCap = dynamic_cast<ICPUAttachable*>(mapper.get()))
        {
            cpuCap->attachCPUInstance(processor);
        }

        // Load the selected bank through mapper logic
        return mapper->loadIntoMemory(currentBank);
    }

    // No mapper => use chipSections-based mapping
    return loadIntoMemory();
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
        case 0x06: return CartridgeType::EPYX_FASTLOAD;
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
        case CartridgeType::EPYX_FASTLOAD:        return "Epyx FastLoad";
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

    return 0xFF; // Open bus
}

uint8_t Cartridge::readRAM(size_t offset)
{
    if (!hasRAM || offset >= ramData.size()) return 0xFF;
    return ramData[offset];
}

void Cartridge::write(uint16_t address, uint8_t value)
{
    if (mapper)
    {
        mapper->write(address, value);
        if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CART))
        {
            std::ostringstream oss;
            oss << "CPU write to address: $"
                << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << int(address)
                << " with value: $"
                << std::setw(2) << int(value);

            const std::string out = oss.str();
            traceActiveWindows(out.c_str());
        }
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
}

void Cartridge::writeRAM(size_t offset, uint8_t value)
{
    if (!hasRAM || offset >= ramData.size()) return;
    ramData[offset] = value;
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
    if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CART))
    {
        traceActiveWindows("Bank Switch");
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

    #ifdef Debug
    std::cout << "Loaded ROM file: " << path << " (" << size << " bytes)" << std::endl;
    #endif // Debug

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

                #ifdef Debug
                std::cout << "Loading 16K section: LO bank at address 0x" << std::hex << (loBase + loOffset) << std::endl;
                #endif // Debug

                for (size_t i = 0; i < 8192; ++i)
                {
                    mem->writeCartridge(loOffset + i, section.data[i], location);
                }
            }
            // Load Second 8K into HI
            {
                cartLocation location = cartLocation::HI;
                // Determine HI base based on header flags.
                uint16_t hiBase = (exROMLine && !gameLine) ? CART_HI_START1 : CART_HI_START;

                #ifdef Debug
                std::cout << "Loading 16K section: HI bank at base address 0x" << std::hex << hiBase << std::endl;
                #endif // Debug

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
                // Always map ROMH content to the HI buffer, starting at 0.
                location = cartLocation::HI;

                for (size_t i = 0; i < section.data.size(); ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), section.data[i], location);

                continue;
            }
            else
            {
                // Default to LO if load address is unrecognized.
                location = cartLocation::LO;
                baseAddress = CART_LO_START;
            }

            #ifdef Debug
            std::cout << "Loading 8K section into " << ((location == cartLocation::LO) ? "LO" : "HI")
                      << " bank, starting at base 0x" << std::hex << baseAddress << std::endl;
            #endif // Debug

            for (size_t i = 0; i < section.data.size(); i++)
            {
                uint16_t offset = (section.loadAddress - baseAddress) + i;
                mem->writeCartridge(offset, section.data[i], location);
            }
        }
        else
        {
            #ifdef Debug
            std::cout << "Loading non-standard section of size " << section.data.size()
                      << " at CPU address 0x" << std::hex << section.loadAddress << std::endl;
            #endif // Debug

            for (size_t i = 0; i < section.data.size(); ++i)
            {
                const uint16_t cpuAddr = static_cast<uint16_t>(section.loadAddress + i);

                cartLocation loc{};
                uint16_t cartOff = 0;

                if (!mapCpuAddrToCartOffset(cpuAddr, wiringMode, loc, cartOff))
                    continue; // outside cart windows

                if (cartOff >= 8192)
                    continue;

                mem->writeCartridge(cartOff, section.data[i], loc);
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
        if (offset + chipHdr.romSize > romData.size())
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
    return true;
}

void Cartridge::determineWiringMode()
{
    // Default to no cartridge mapped
    wiringMode = WiringMode::NONE;
    setExROMLine(true);
    setGameLine(true);

    bool any8000 = false, anyA000 = false, anyE000 = false, any16K = false;

    for (const auto& s : chipSections)
    {
        const uint32_t start = s.loadAddress;
        const uint32_t end   = s.loadAddress + static_cast<uint32_t>(s.data.size()); // exclusive

        if (s.loadAddress == 0x8000 && s.data.size() >= 16384) any16K = true;
        if (start <= 0x9FFF && end > 0x8000)  any8000 = true;  // overlaps $8000-$9FFF
        if (start <= 0xBFFF && end > 0xA000)  anyA000 = true;  // overlaps $A000-$BFFF
        if (start <= 0xFFFF && end > 0xE000)  anyE000 = true;  // overlaps $E000-$FFFF
    }

    if (anyE000) {
        wiringMode = WiringMode::CART_ULTIMAX;
        setExROMLine(true);
        setGameLine(false);
        return;
    }

    if (mapperType == CartridgeType::OCEAN) {
        if (anyA000 || any16K) {
            wiringMode = WiringMode::CART_16K;
            setExROMLine(false);
            setGameLine(false);
        } else if (any8000) {
            wiringMode = WiringMode::CART_8K;
            setExROMLine(false);
            setGameLine(true);
        }
        return;
    }

    if (!header.exROMLine && !header.gameLine) {
        wiringMode = WiringMode::CART_16K;
        setExROMLine(false); setGameLine(false);
    } else if (!header.exROMLine && header.gameLine) {
        wiringMode = WiringMode::CART_8K;
        setExROMLine(false); setGameLine(true);
    } else if (header.exROMLine && !header.gameLine) {
        wiringMode = WiringMode::CART_ULTIMAX;
        setExROMLine(true); setGameLine(false);
    }

    if (wiringMode == WiringMode::NONE) {
        if (anyA000 || any16K) {
            wiringMode = WiringMode::CART_16K;
            setExROMLine(false); setGameLine(false);
        } else if (any8000) {
            wiringMode = WiringMode::CART_8K;
            setExROMLine(false); setGameLine(true);
        }
    }
}

void Cartridge::traceActiveWindows(const char* why)
{
    if (!traceMgr || !traceMgr->isEnabled() || !traceMgr->catOn(TraceManager::TraceCat::CART)) return;
    auto stamp = traceMgr->makeStamp(processor ? processor->getTotalCycles() : 0, vicII ? vicII->getCurrentRaster() : 0,
                vicII ? vicII->getRasterDot() : 0);

    const std::string mapper = getMapperName();
    switch (wiringMode) {
        case WiringMode::CART_8K:      // $8000-$9FFF
            traceMgr->recordCartBank(mapper.c_str(), currentBank, 0x8000, 0x9FFF, stamp);
            break;
        case WiringMode::CART_16K:     // $8000-$BFFF
            traceMgr->recordCartBank(mapper.c_str(), currentBank, 0x8000, 0xBFFF, stamp);
            break;
        case WiringMode::CART_ULTIMAX: // $8000-$9FFF and $E000-$FFFF
            traceMgr->recordCartBank(mapper.c_str(), currentBank, 0x8000, 0x9FFF, stamp);
            traceMgr->recordCartBank(mapper.c_str(), currentBank, 0xE000, 0xFFFF, stamp);
            break;
        default:
            traceMgr->recordCustomEvent(std::string("CART map change (") + why + "): " + getMapperName());
            break;
    }
}

uint8_t Cartridge::selectInitialBank(const std::vector<Cartridge::chipSection>& sections)
{
    if (sections.empty()) return 0;

    bool has0 = false;
    uint16_t minBank = 0xFFFF;

    for (const auto& s : sections)
    {
        if (s.bankNumber == 0) has0 = true;
        if (s.bankNumber < minBank) minBank = s.bankNumber;
    }

    return has0 ? 0 : static_cast<uint8_t>(minBank);
}

bool Cartridge::mapCpuAddrToCartOffset(uint16_t cpuAddr, Cartridge::WiringMode wiringMode, cartLocation& outLoc, uint16_t& outOffset)
{
    // $8000-$9FFF always maps to cart LO when a cart is active
    if (cpuAddr >= 0x8000 && cpuAddr <= 0x9FFF)
    {
        outLoc = cartLocation::LO;
        outOffset = static_cast<uint16_t>(cpuAddr - 0x8000);
        return true;
    }

    // $A000-$BFFF only maps to cart HI in 16K mode
    if (wiringMode == Cartridge::WiringMode::CART_16K)
    {
        if (cpuAddr >= 0xA000 && cpuAddr <= 0xBFFF)
        {
            outLoc = cartLocation::HI;
            outOffset = static_cast<uint16_t>(cpuAddr - 0xA000);
            return true;
        }
    }

    // Ultimax high ROM at $E000-$FFFF maps into your HI buffer
    if (wiringMode == Cartridge::WiringMode::CART_ULTIMAX && cpuAddr >= 0xE000)
    {
        outLoc = cartLocation::HI;
        outOffset = static_cast<uint16_t>(cpuAddr - 0xE000);
        return true;
    }

    return false;
}

std::unique_ptr<CartridgeMapper> Cartridge::createMapper(CartridgeType t)
{
    switch (t)
    {
        case CartridgeType::ACTION_REPLAY:   return std::make_unique<ActionReplayMapper>();
        case CartridgeType::C64_GAME_SYSTEM: return std::make_unique<C64GameSystemMapper>();
        case CartridgeType::DINAMIC:         return std::make_unique<DinamicMapper>();
        case CartridgeType::EASYFLASH:       return std::make_unique<EasyFlashMapper>();
        case CartridgeType::OCEAN:           return std::make_unique<OceanMapper>();
        case CartridgeType::EPYX_FASTLOAD:   return std::make_unique<EpyxFastloadMapper>();
        case CartridgeType::ROSS:            return std::make_unique<RossMapper>();
        case CartridgeType::STRUCTURED_BASIC:return std::make_unique<StructuredBasicMapper>();
        case CartridgeType::SUPER_GAMES:     return std::make_unique<SuperGamesMapper>();
        case CartridgeType::SUPER_ZAXXON:    return std::make_unique<SuperZaxxonMapper>();
        case CartridgeType::SIMONS_BASIC:    return std::make_unique<SimonsBasicMapper>();
        case CartridgeType::MAGICDESK:       return std::make_unique<MagicDeskMapper>();
        case CartridgeType::FUN_PLAY:        return std::make_unique<FunPlayMapper>();

        case CartridgeType::KCS_POWER:
        case CartridgeType::WESTERMANN:
        case CartridgeType::GENERIC:
            return std::make_unique<GenericMapper>();

        default:
            return nullptr; // UNKNOWN => treat as “no mapper”, use chipSections mapping
    }
}
