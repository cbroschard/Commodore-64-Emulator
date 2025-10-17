// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MEMORY_H
#define MEMORY_H

// Forward declarations
class Cartridge;
class CIA1;

#include <bitset>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include "Cartridge.h"
#include "Cassette.h"
#include "CIA1.h"
#include "CIA2.h"
#include "common.h"
#include "CPU.h"
#include "Logging.h"
#include "Mlmonitor.h"
#include "PLA.h"
#include "SID/SID.h"
#include "Vic.h"

class Memory
{
    public:

        Memory();
        virtual ~Memory();

        // Pointers
        inline void attachProcessorInstance(CPU* processor) { this->processor = processor; }
        inline void attachVICInstance(Vic* vicII) { this->vicII = vicII; }
        inline void attachCassetteInstance(Cassette* cass) { this->cass = cass; }
        inline void attachCIA1Instance(CIA1* cia1object) { this->cia1object = cia1object; }
        inline void attachCIA2Instance(CIA2* cia2object) { this->cia2object = cia2object; }
        inline void attachSIDInstance(SID* sidchip) { this->sidchip = sidchip; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachCartridgeInstance(Cartridge* cart) { this->cart = cart; }
        inline void attachPLAInstance(PLA* pla) { this->pla = pla; }
        inline void attachMonitorInstance(MLMonitor* monitor) { this->monitor = monitor; }

        // Public access to memory
        uint8_t read(uint16_t address); // CPU access
        uint16_t read16(uint16_t addr);
        uint8_t vicRead(uint16_t vicAddress, uint16_t raster); // VIC access range
        uint8_t vicReadColor(uint16_t address) const;
        void write(uint16_t address, uint8_t value); // CPU access
        void write16(uint16_t address, uint16_t value);
        void writeDirect(uint16_t address, uint8_t value);

        // Allow the cartridge to be loaded into memory
        void writeCartridge(uint16_t address, uint8_t value, cartLocation location);

        // Setter for cartridge
        inline void setCartridgeAttached(bool flag) { cartridgeAttached = flag; }

        // Getter/Setter for cassette
        inline bool getCassetteSenseLow() { return cassetteSenseLow; }
        inline void setCassetteSenseLow(bool pressed) { cassetteSenseLow = pressed; }
        inline bool isCassetteMotorOn() const  { return (port1OutputLatch & 0x20) == 0; }

        // Load all ROMS
        bool Initialize(const std::string& basic, const std::string& kernal, const std::string& character);

        // Helpers for certain cartridge types
        inline uint8_t getCartLOByte(uint16_t offset) const { return (offset < cart_lo.size()) ? cart_lo[offset] : 0xFF; }
        inline uint8_t getCartHIByte(uint16_t offset) const { return (offset < cart_hi.size()) ? cart_hi[offset] : 0xFF; }

        // ML Monitor logging
        inline void setLog(bool enable) { setLogging = enable; }

    protected:

    private:

        // Non-owning pointers
        Cartridge* cart;
        CIA1* cia1object;
        CIA2* cia2object;
        Cassette* cass;
        CPU* processor;
        Logging* logger;
        MLMonitor* monitor;
        PLA* pla;
        SID* sidchip;
        Vic* vicII;

        // RAM/ROM
        std::vector<uint8_t> mem;
        std::vector<uint8_t> basicROM;
        std::vector<uint8_t> charROM;
        std::vector<uint8_t> kernalROM;
        std::vector<uint8_t> colorRAM;
        std::vector<uint8_t> cart_lo;
        std::vector<uint8_t> cart_hi;

        // Rom constants
        static constexpr size_t BASIC_ROM_SIZE = 0x2000;
        static constexpr size_t KERNAL_ROM_SIZE = 0x2000;
        static constexpr size_t CHAR_ROM_SIZE = 0x1000;
        static constexpr size_t CART_LO_SIZE = 0x2000;
        static constexpr size_t CART_HI_SIZE = 0x2000;
        static constexpr size_t MAX_MEMORY = 0x10000;
        static constexpr size_t COLOR_RAM_SIZE = 0x400;
        static const uint16_t COLOR_MEMORY_START = 0xD800;
        static const uint16_t COLOR_MEMORY_END =  0xDBFF;

        // I/O Constants
        static const uint16_t IO_VIC_START = 0xD000;
        static const uint16_t IO_VIC_END = 0xD3FF;
        static const uint16_t IO_SID_START = 0xD400;
        static const uint16_t IO_SID_END = 0xD7FF;
        static const uint16_t IO_CIA1_START = 0xDC00;
        static const uint16_t IO_CIA1_END = 0xDCFF;
        static const uint16_t IO_CIA2_START = 0xDD00;
        static const uint16_t IO_CIA2_END = 0xDDFF;

        // Cartridge
        bool cartridgeAttached;
        bool cassetteSenseLow;

        // MCR
        uint8_t dataDirectionRegister;
        uint8_t port1OutputLatch;

        // Open bus
        uint8_t lastBus;

        // ML Monitor logging
        bool setLogging;

        uint8_t readIO(uint16_t address);
        void writeIO(uint16_t address, uint8_t value);

        bool load_ROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName);

        uint8_t computeEffectivePort1(uint8_t latch, uint8_t ddr);
        void applyPort1SideEffects(uint8_t effective);
};

#endif // MEMORY_H
