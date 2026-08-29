// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef BUS_H
#define BUS_H

#include <cstdint>
#include "CPUBus.h"

// Forward declarations
class Cartridge;
class Cassette;
class CIA1;
class CIA2;
class CPU;
class CPU6510Port;
class DataBusLatch;
class DebugManager;
class Memory;
class MLMonitor;
class PLA;
class REU;
class SID;
class SwiftLink;
class TraceManager;
class Turbo232;
class Vic;

class Bus : public CPUBus
{
    public:
        Bus();
        virtual ~Bus();

        // Pointers
        inline void attachCassetteInstance(Cassette* cass) { this->cass = cass; }
        inline void attachCartridgeInstance(Cartridge* cart) { this->cart = cart; }
        inline void attachCIA1Instance(CIA1* cia1) { this->cia1 = cia1; }
        inline void attachCIA2Instance(CIA2* cia2) { this->cia2 = cia2; }
        inline void attachCPUInstance(CPU* cpu) { this->cpu = cpu; }
        inline void attachCPU6510PortInstance(CPU6510Port* cpu6510Port) { this->cpu6510Port = cpu6510Port; }
        inline void attachDataBusLatchInstance(DataBusLatch* dataBus) { this->dataBus = dataBus; }
        inline void attachDebugManagerInstance(DebugManager* debugManager) { this->debugManager = debugManager; }
        inline void attachMemoryInstance(Memory* mem) { this->mem = mem; }
        inline void attachMonitorInstance(MLMonitor* monitor) { this->monitor = monitor; }
        inline void attachPLAInstance(PLA* pla) { this->pla = pla; }
        inline void attachREUInstance(REU* reu) { this->reu = reu; }
        inline void attachSIDInstance(SID* sid) { this->sid = sid; }
        inline void attachSwiftLinkInstance(SwiftLink* swiftLink) { this->swiftLink = swiftLink; }
        inline void attachTraceManagerInstance(TraceManager* traceMgr) { this->traceMgr = traceMgr; }
        inline void attachTurbo232Instance(Turbo232* turbo232) { this->turbo232 = turbo232; }
        inline void attachVICInstance(Vic* vic) { this->vic = vic; }

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        uint8_t readIO(uint16_t address);
        void writeIO(uint16_t address, uint8_t value);

        uint8_t readForDMA(uint16_t address);
        void writeForDMA(uint16_t address, uint8_t value);

        uint8_t vicRead(uint16_t address, uint16_t raster);
        uint8_t vicReadColor(uint16_t address) const;

        uint8_t peek(uint16_t address) const override;
        uint8_t peekIO(uint16_t address) const;

        // Getters
        inline bool isCartridgeAttached() const { return cartridgeAttached; }
        inline bool isROMLOverlayRAM() const { return romLOverlayIsRAM; }
        inline bool isROMHOverlayRAM() const { return romHOverLayIsRAM; }

        // Setters
        inline void setCartridgeAttached(bool attached) { cartridgeAttached = attached; }
        inline void setROMLOverlayIsRAM(bool enabled) { romLOverlayIsRAM = enabled; }
        inline void setROMHOverlayIsRAM(bool enabled) { romHOverLayIsRAM = enabled; }

    private:
        // Non-owning pointers
        Cartridge* cart;
        CIA1* cia1;
        CIA2* cia2;
        Cassette* cass;
        CPU* cpu;
        CPU6510Port* cpu6510Port;
        DataBusLatch* dataBus;
        DebugManager* debugManager;
        Memory* mem;
        MLMonitor* monitor;
        PLA* pla;
        REU* reu;
        SID* sid;
        SwiftLink* swiftLink;
        TraceManager* traceMgr;
        Turbo232* turbo232;
        Vic* vic;

        // CPU I/O address map
        static constexpr uint16_t IO_VIC_START       = 0xD000;
        static constexpr uint16_t IO_VIC_END         = 0xD3FF;

        static constexpr uint16_t IO_SID_START       = 0xD400;
        static constexpr uint16_t IO_SID_END         = 0xD7FF;

        static constexpr uint16_t COLOR_MEMORY_START = 0xD800;
        static constexpr uint16_t COLOR_MEMORY_END   = 0xDBFF;

        static constexpr uint16_t IO_CIA1_START      = 0xDC00;
        static constexpr uint16_t IO_CIA1_END        = 0xDCFF;

        static constexpr uint16_t IO_CIA2_START      = 0xDD00;
        static constexpr uint16_t IO_CIA2_END        = 0xDDFF;

        static constexpr uint16_t IO1_START           = 0xDE00;
        static constexpr uint16_t IO1_END             = 0xDEFF;

        static constexpr uint16_t IO2_START           = 0xDF00;
        static constexpr uint16_t IO2_END             = 0xDFFF;

        bool cartridgeAttached;
        bool romLOverlayIsRAM;
        bool romHOverLayIsRAM;
};

#endif // BUS_H
