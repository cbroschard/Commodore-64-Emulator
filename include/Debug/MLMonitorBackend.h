// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MLMONITORBACKEND_H
#define MLMONITORBACKEND_H

#include "Computer.h"
#include "Drive.h"

class MLMonitorBackend
{
    public:
        MLMonitorBackend();
        virtual ~MLMonitorBackend();

        // Pointer functions
        inline void attachCartridgeInstance(Cartridge* cart) { this->cart = cart; }
        inline void attachCassetteInstance(Cassette* cass) { this->cass = cass; }
        inline void attachCIA1Instance(CIA1* cia1object) { this->cia1object = cia1object; }
        inline void attachCIA2Instance(CIA2* cia2object) { this->cia2object = cia2object; }
        inline void attachComputerInstance(Computer* comp) { this->comp = comp; }
        inline void attachProcessorInstance(CPU* processor) { this->processor = processor; }
        inline void attachIECBusInstance(IECBUS* bus) { this->bus = bus; }
        inline void attachIOInstance(IO* IO_adapter) { this->IO_adapter = IO_adapter; }
        inline void attachKeyboardInstance(Keyboard* keyb) { this->keyb = keyb; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachMemoryInstance(Memory* mem) { this->mem = mem; }
        inline void attachPLAInstance(PLA* pla) { this->pla = pla; }
        inline void attachSIDInstance(SID* sidchip) { this->sidchip = sidchip; }
        inline void attachVICInstance(Vic* vicII) { this->vicII = vicII; }

        struct CPUState
        {
            uint16_t PC;
            uint8_t  A, X, Y, SP, SR;
        };

        // ML Monitor Cartridge methods
        inline Cartridge* getCart() { return cart; }
        void detachCartridge();
        bool getCartridgeAttached();

        // ML Monitor Cassette methods
        inline std::string dumpTapeDebug(size_t count) const { return cass ? cass->dumpPulses(count) : "CASSETTE not attached\n"; }

        // ML Monitor CIA1 Register Dumps
        inline std::string dumpCIA1Regs() const { return cia1object ? cia1object->dumpRegisters("all") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1Ports() const { return cia1object ? cia1object->dumpRegisters("port") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1Timers() const { return cia1object ? cia1object->dumpRegisters("timer") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1TOD() const { return cia1object ? cia1object->dumpRegisters("tod") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1ICR() const { return cia1object ? cia1object->dumpRegisters("icr") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1Serial() const { return cia1object ? cia1object->dumpRegisters("serial") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1Mode() const { return cia1object ? cia1object->dumpRegisters("mode") : "CIA1 not attached\n"; }

        // ML Monitor CIA2 Register Dumps
        inline std::string dumpCIA2Regs() const { return cia2object ? cia2object->dumpRegisters("all") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2Ports() const { return cia2object ? cia2object->dumpRegisters("port") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2Timers() const { return cia2object ? cia2object->dumpRegisters("timer") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2TOD() const { return cia2object ? cia2object->dumpRegisters("tod") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2ICR() const { return cia2object ? cia2object->dumpRegisters("icr") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2Serial() const { return cia2object ? cia2object->dumpRegisters("serial") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2VICBanks() const { return cia2object ? cia2object->dumpRegisters("vic") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2IEC() const { return cia2object ? cia2object->dumpRegisters("iec") : "CIA2 not attached\n"; }

        // ML Monitor Computer Methods
        void coldReset();
        void warmReset();

        // ML Monitor CPU Methods
        inline CPUState getCPUState() const { return CPUState{ processor->getPC(), processor->getA(), processor->getX(),
                 processor->getY(), processor->getSP(), processor->getSR()}; }
        inline uint8_t cpuGetSR() { return processor->getSR(); }
        inline void cpuStep() { return processor->tick(); }
        std::string getJamMode() const { return processor ? jamModeToString() : "Processor not attached\n"; }
        inline uint8_t getOpCode(uint16_t PC) { return mem->read(PC); }
        inline uint16_t getPC() { return processor->getPC(); }
        void setJamMode(const std::string& mode);
        inline void setPC(uint16_t address) { processor->setPC(address); }

        // ML Monitor IEC Bus
        IECBUS* getIECBus() const { return bus; }
        void dumpDriveList();
        void dumpDriveSummary(int id);
        void dumpDriveCPU(int id);
        void dumpDriveMemory(int id, uint16_t startAddress, uint16_t endAddress);

        // ML Monitor IRQ
        struct IRQSnapshot
        {
            bool has = false;
            Vic::VICIRQSnapshot  vic;
            CIA1::CIA1IRQSnapshot cia1;
            CIA2::CIA2IRQSnapshot cia2;
        };

        void irqDisableAll();
        void irqClearAll();
        void irqRestore();

        // ML Monitor per chip IRQ helpers
        inline uint8_t vicIER()  const { return vicII ? vicII->getIER() : 0; }
        inline uint8_t vicIFR()  const { return vicII ? vicII->getIFR() : 0; }
        inline bool    vicIRQ()  const { return vicII && vicII->irqLineActive(); }
        inline uint8_t cia1IER() const { return cia1object ? cia1object->getIER() : 0; }
        inline uint8_t cia1IFR() const { return cia1object ? cia1object->getIFR() : 0; }
        inline bool    cia1IRQ() const { return cia1object && cia1object->irqLineActive(); }
        inline uint8_t cia2IER() const { return cia2object ? cia2object->getIER() : 0; }
        inline uint8_t cia2IFR() const { return cia2object ? cia2object->getIFR() : 0; }
        inline bool    cia2NMI() const { return cia2object && cia2object->irqLineActive(); }
        inline void cpuCLI(){ return processor->setCLI(); }
        inline void cpuSEI(){ return processor->setSEI(); }
        inline void setVicIER(uint8_t m)  { if (vicII)      vicII->setIERExact(m & 0x0F); }
        inline void setCIA1IER(uint8_t m) { if (cia1object) cia1object->setIERExact(m & 0x1F); }
        inline void setCIA2IER(uint8_t m) { if (cia2object) cia2object->setIERExact(m & 0x1F); }

        // ML Monitor Logging enable/disable
        void setLogging(LogSet log, bool enabled);

        // ML Monitor Memory methods
        inline Memory* getMem() { return mem; }
        inline uint8_t readRAM(uint16_t address) { return mem->read(address); }
        inline void writeRAM(uint16_t address, uint8_t value) { mem->write(address, value); }
        inline void writeRAMDirect(uint16_t address, uint8_t value) { mem->writeDirect(address, value); }

        // ML Monitor PLA methods
        inline std::string plaGetState() { return pla ? pla->describeMode() : "PLA not attached\n"; }
        inline std::string plaGetAddressInfo(uint16_t address) { return pla ? pla->describeAddress(address) : "PLA not attached\n"; }

        // ML Monitor SID Register Dumps
        inline std::string dumpSIDRegs() const { return sidchip ? sidchip->dumpRegisters("all") : "SID not attached\n"; }
        inline std::string dumpSIDVoice1() const { return sidchip ? sidchip->dumpRegisters("voice1") : "SID not attached\n"; }
        inline std::string dumpSIDVoice2() const { return sidchip ? sidchip->dumpRegisters("voice2") : "SID not attached\n"; }
        inline std::string dumpSIDVoice3() const { return sidchip ? sidchip->dumpRegisters("voice3") : "SID not attached\n"; }
        inline std::string dumpSIDVoices() const { return sidchip ? sidchip->dumpRegisters("voices") : "SID not attached\n"; }
        inline std::string dumpSIDFilter() const { return sidchip ? sidchip->dumpRegisters("filter") : "SID not attached\n"; }

        // ML Monitor VIC-II methods
        inline std::string vicGetModeName() { return vicII ? vicII->decodeModeName() : "VIC not attached\n"; }
        inline std::string getCurrentVICBanks() { return vicII ? vicII->getVICBanks() : "VIC not attached\n"; }
        inline std::string vicDumpRegs(const std::string& group) { return vicII ? vicII->dumpRegisters(group) : " VIC not attached\n"; }
        inline uint8_t getCurrentRaster() { return vicII->getCurrentRaster(); }
        void vicFFRaster(uint8_t targetRaster);


    protected:

    private:

        // Non-owning pointers
        Cartridge* cart;
        Cassette* cass;
        CIA1* cia1object;
        CIA2* cia2object;
        Computer* comp;
        CPU* processor;
        IECBUS* bus;
        IO* IO_adapter;
        Keyboard* keyb;
        Logging* logger;
        Memory* mem;
        PLA* pla;
        SID* sidchip;
        Vic* vicII;

        // ML Monitor IRQ snapshot
        IRQSnapshot snapshot;

        // Helpers
        std::string jamModeToString() const;
        std::string decodeDriveStatus(Drive::DriveStatus status);
};

#endif // MLMONITORBACKEND_H
