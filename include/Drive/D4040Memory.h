// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D4040MEMORY_H
#define D4040MEMORY_H

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include "Chip/MOS6530.h"
#include "Chip/MOS6532.h"
#include "Drive/D4040VIA.h"
#include "Drive/DriveChips.h"
#include "Peripheral.h"

class D4040Memory : public DriveMemoryBase
{
    public:
        D4040Memory();
        ~D4040Memory();

        void attachPeripheralInstance(Peripheral* parentPeripheral);

        void reset();

        void tick(uint32_t cycles) override;

        // Main 6502 DOS processor
        uint8_t read(uint16_t address);
        void write(uint16_t address, uint8_t value);
        uint8_t peek(uint16_t address) const;

        // Secondary 6504 controller processor
        uint8_t read6504(uint16_t address);
        void write6504(uint16_t address, uint8_t value);
        uint8_t peek6504(uint16_t address) const;

        // Initialize function to load all ROMS passed in via config file, initialize RAM
        bool initialize(const std::string& fdcROM, const std::string& D4040LoROM, const std::string& D4040HiROM1,
                         const std::string& D4040HiROM2);

        inline MOS6530& getRRIOT() { return rriot; }
        inline const MOS6530& getRRIOT() const { return rriot; }

        inline D4040VIA& getVIA() { return via; }
        inline const D4040VIA& getVIA() const { return via; }

        inline MOS6532& getRIOT1() { return riot1; }
        inline const MOS6532& getRIOT1() const { return riot1; }

        inline MOS6532& getRIOT2() { return riot2; }
        inline const MOS6532& getRIOT2() const { return riot2; }

    private:
        // Non-owning pointers
        Peripheral* parentPeripheral;

        MOS6532 riot1;
        MOS6532 riot2;

        MOS6530 rriot;
        D4040VIA via;

        // 6532 RIOT RAM
        static constexpr uint16_t RIOT1_START             = 0x0000;
        static constexpr uint16_t RIOT1_END               = 0x007F;
        static constexpr uint16_t RIOT2_START             = 0x0080;
        static constexpr uint16_t RIOT2_END               = 0x00FF;

        // RIOT RAM mirrors (6502 stack page)
        static constexpr uint16_t RIOT_RAM_MIRROR_START   = 0x0100;
        static constexpr uint16_t RIOT_RAM_MIRROR_END     = 0x01FF;

        // RIOT I/O registers
        static constexpr uint16_t RIOT1_IO_START          = 0x0200;
        static constexpr uint16_t RIOT1_IO_END            = 0x027F;
        static constexpr uint16_t RIOT2_IO_START          = 0x0280;
        static constexpr uint16_t RIOT2_IO_END            = 0x02FF;

        // RIOT I/O mirrors
        static constexpr uint16_t RIOT_IO_MIRROR_START    = 0x0300;
        static constexpr uint16_t RIOT_IO_MIRROR_END      = 0x03FF;

        // Additional RIOT RAM and I/O mirrors
        static constexpr uint16_t RIOT_MIRROR2_START      = 0x0400;
        static constexpr uint16_t RIOT_MIRROR2_END        = 0x0FFF;

        static constexpr uint16_t RIOT_MIRROR3_START      = 0x8000;
        static constexpr uint16_t RIOT_MIRROR3_END        = 0x8FFF;

        // Shared RAM: four physical 1 KB banks
        static constexpr uint16_t RAM_SIZE                = 0x1000;

        static constexpr uint16_t SHARED_RAM1_START       = 0x1000;
        static constexpr uint16_t SHARED_RAM1_END         = 0x13FF;

        static constexpr uint16_t SHARED_RAM2_START       = 0x2000;
        static constexpr uint16_t SHARED_RAM2_END         = 0x23FF;

        static constexpr uint16_t SHARED_RAM3_START       = 0x3000;
        static constexpr uint16_t SHARED_RAM3_END         = 0x33FF;

        static constexpr uint16_t SHARED_RAM4_START       = 0x4000;
        static constexpr uint16_t SHARED_RAM4_END         = 0x43FF;

        // Shared RAM mirrors within each 4 KB window
        static constexpr uint16_t SHARED_RAM1_MIRROR_END  = 0x1FFF;
        static constexpr uint16_t SHARED_RAM2_MIRROR_END  = 0x2FFF;
        static constexpr uint16_t SHARED_RAM3_MIRROR_END  = 0x3FFF;
        static constexpr uint16_t SHARED_RAM4_MIRROR_END  = 0x4FFF;

        // Additional shared RAM mirrors
        static constexpr uint16_t RAM_MIRROR_START        = 0x9000;
        static constexpr uint16_t RAM_MIRROR_END          = 0xCFFF;

        // =====================================================
        // 6504 Disk Controller Memory Map
        // =====================================================

        static constexpr uint16_t CPU6504_ADDRESS_MASK = 0x1FFF;

        // MOS 6530 internal RAM (64 bytes)
        static constexpr uint16_t FDC_RAM_START        = 0x0000;
        static constexpr uint16_t FDC_RAM_END          = 0x003F;
        static constexpr uint16_t FDC_RAM_SIZE         = 0x0040;

        // MOS 6522 VIA
        static constexpr uint16_t FDC_VIA_START        = 0x0040;
        static constexpr uint16_t FDC_VIA_END          = 0x004F;

        // MOS 6530 I/O
        static constexpr uint16_t FDC_RRIOT_IO_START   = 0x0080;
        static constexpr uint16_t FDC_RRIOT_IO_END     = 0x008F;

        // Shared RAM as seen by 6504
        static constexpr uint16_t FDC_SHARED_RAM_START = 0x0400;
        static constexpr uint16_t FDC_SHARED_RAM_END   = 0x13FF;

        // MOS 6530 controller firmware
        static constexpr uint16_t FDC_ROM_START        = 0x1C00;
        static constexpr uint16_t FDC_ROM_END          = 0x1FFF;
        static constexpr uint16_t FDC_ROM_SIZE         = 0x0400;

        // DOS ROMs (4 KB each)
        static constexpr uint16_t ROM_SIZE                = 0x1000;

        static constexpr uint16_t ROM1_START              = 0xD000;
        static constexpr uint16_t ROM1_END                = 0xDFFF;

        static constexpr uint16_t ROM2_START              = 0xE000;
        static constexpr uint16_t ROM2_END                = 0xEFFF;

        static constexpr uint16_t ROM3_START              = 0xF000;
        static constexpr uint16_t ROM3_END                = 0xFFFF;

        // DOS ROM mirrors
        static constexpr uint16_t ROM_MIRROR_START        = 0x5000;
        static constexpr uint16_t ROM_MIRROR_END          = 0x7FFF;

        std::vector<uint8_t> D4040RAM;
        std::vector<uint8_t> D4040FDCROM;
        std::vector<uint8_t> D4040ROM1;
        std::vector<uint8_t> D4040ROM2;
        std::vector<uint8_t> D4040ROM3;

         // ROM Loader
        bool loadROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName);

        static uint16_t normalizeAddress(uint16_t address);
};

#endif // D4040MEMORY_H
