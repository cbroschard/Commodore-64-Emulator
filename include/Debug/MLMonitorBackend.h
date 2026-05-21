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
#include "Debug/CommandUtils.h"
#include "Drive/Drive.h"

class MLMonitorBackend
{
    public:
        MLMonitorBackend();
        virtual ~MLMonitorBackend();

        // Pointer functions
        inline void attachCartridgeInstance(Cartridge* cart) { this->cart = cart; }
        inline void attachCassetteInstance(Cassette* cass) { this->cass = cass; }
        inline void attachCIA1Instance(CIA1* cia1) { this->cia1 = cia1; }
        inline void attachCIA2Instance(CIA2* cia2) { this->cia2 = cia2; }
        inline void attachComputerInstance(Computer* comp) { this->comp = comp; }
        inline void attachIRQLineInstance(IRQLine* irq) { this->irq = irq; }
        inline void attachProcessorInstance(CPU* cpu) { this->cpu = cpu; }
        inline void attachIECBusInstance(IECBUS* bus) { this->bus = bus; }
        inline void attachIOInstance(IO* io) { this->io = io; }
        inline void attachKeyboardInstance(Keyboard* keyb) { this->keyb = keyb; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }
        inline void attachMemoryInstance(Memory* mem) { this->mem = mem; }
        inline void attachPLAInstance(PLA* pla) { this->pla = pla; }
        inline void attachREUInstance(REU* reu) { this->reu = reu; }
        inline void attachSIDInstance(SID* sid) { this->sid = sid; }
        inline void attachVICInstance(Vic* vic) { this->vic = vic; }

         using CPUState = CPU::CPUState;

        // ML Monitor Cartridge methods
        inline Cartridge* getCart() { return cart; }
        void detachCartridge();
        bool getCartridgeAttached();

        // ML Monitor Cassette methods
        inline std::string dumpTapeDebug(size_t count) const { return cass ? cass->dumpPulses(count) : "CASSETTE not attached\n"; }

        // ML Monitor CIA1 Register Dumps
        inline std::string dumpCIA1Regs() const { return cia1 ? cia1->dumpRegisters("all") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1Ports() const { return cia1 ? cia1->dumpRegisters("port") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1Timers() const { return cia1 ? cia1->dumpRegisters("timer") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1TOD() const { return cia1 ? cia1->dumpRegisters("tod") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1ICR() const { return cia1 ? cia1->dumpRegisters("icr") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1Serial() const { return cia1 ? cia1->dumpRegisters("serial") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1Mode() const { return cia1 ? cia1->dumpRegisters("mode") : "CIA1 not attached\n"; }

        // ML Monitor CIA2 Register Dumps
        inline std::string dumpCIA2Regs() const { return cia2 ? cia2->dumpRegisters("all") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2Ports() const { return cia2 ? cia2->dumpRegisters("port") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2Timers() const { return cia2 ? cia2->dumpRegisters("timer") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2TOD() const { return cia2 ? cia2->dumpRegisters("tod") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2ICR() const { return cia2 ? cia2->dumpRegisters("icr") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2Serial() const { return cia2 ? cia2->dumpRegisters("serial") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2VICBanks() const { return cia2 ? cia2->dumpRegisters("vic") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2IEC() const { return cia2 ? cia2->dumpRegisters("iec") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2IECSnapshot() const { return cia2 ? cia2->debugIECSnapshotString() : "CIA2 not attached\n"; }

        // ML Monitor Computer Methods
        void enterMonitor();
        void coldReset();
        void warmReset();

        // ML Monitor CPU Methods
        inline CPUState getCPUState() const { return cpu ? cpu->getState() : CPUState{}; }
        inline uint8_t cpuGetSR() { return cpu->getSR(); }
        inline void cpuStep() { return cpu->tick(); }
        inline std::string getJamMode() const { return cpu ? jamModeToString() : "CPU not attached\n"; }
        inline uint8_t getOpCode(uint16_t PC) { return mem->read(PC); }
        inline uint16_t getPC() { return cpu->getPC(); }
        inline bool cpuIsBusArbEnabled() const { return cpu->isVICBusArbitrationEnabled(); }
        inline void cpuSetBusArbEnabled(bool enabled) { cpu->setVICBusArbitrationEnabled(enabled); }
        inline std::string cpuMicroOpStatus() const { return cpu ? cpu->dumpMicroOpStatus() : "CPU not attached\n"; }
        inline void cpuSetMicroOp(bool enable) { return cpu->setUseMicroOpsForTest(enable) ; }

        void setPC(uint16_t address);
        void cpuStepInstruction();
        std::string cpuAddressStatus() const;
        std::string cpuBranchStatus() const;
        std::string cpuInterruptStatus() const;
        std::string cpuIrqStatus() const;
        std::string cpuCycleStatus() const;
        std::string cpuJMPStatus() const;
        std::string cpuJSRStatus() const;
        std::string cpuPHAStatus() const;
        std::string cpuPHPStatus() const;
        std::string cpuPLAStatus() const;
        std::string cpuPLPStatus() const;
        std::string cpuRTIStatus() const;
        std::string cpuRTSStatus() const;
        std::string cpuStackStatus(int count) const;
        std::string cpuLastStatus() const;
        void setJamMode(const std::string& mode);

        // ML Monitor Drives
        void dumpDriveList();
        void dumpDriveSummary(int id);
        void dumpDriveCIA(int id);
        void dumpDriveCPU(int id);
        void driveCPUStep(int id);
        void dumpDriveFDC(int id);
        void dumpDriveIECState(int id);
        void dumpDriveMemory(int id, uint16_t startAddress, uint16_t count);
        void dumpDriveVIA1(int id);
        void dumpDriveVIA2(int id);

        // ML Monitor IEC Bus
        IECBUS* getIECBus() const { return bus; }

        // Helper to check if irq is left on which is disruptive to normal operation
        inline bool irqForceActive() const { return irq && ( irq->getActiveSources() & IRQLine::MONITOR) != 0; }

        // ML Monitor IRQ
        struct IRQSnapshot
        {
            bool has = false;
            Vic::VICIRQSnapshot  vic;
            CIA1::CIA1IRQSnapshot cia1;
            CIA2::CIA2IRQSnapshot cia2;
        };

        inline bool irqLineActive() const { return irq && irq->isIRQActive(); }
        void irqForceOn();
        void irqForceOff();
        void irqDisableAll();
        void irqClearAll();
        void irqRestore();

        // ML Monitor per chip IRQ helpers
        inline uint8_t vicIER()  const { return vic ? vic->getIER() : 0; }
        inline uint8_t vicIFR()  const { return vic ? vic->getIFR() : 0; }
        inline bool    vicIRQ()  const { return vic && vic->irqLineActive(); }
        inline uint8_t cia1IER() const { return cia1 ? cia1->getIER() : 0; }
        inline uint8_t cia1IFR() const { return cia1 ? cia1->getIFR() : 0; }
        inline bool    cia1IRQ() const { return cia1 && cia1->irqLineActive(); }
        inline uint8_t cia2IER() const { return cia2 ? cia2->getIER() : 0; }
        inline uint8_t cia2IFR() const { return cia2 ? cia2->getIFR() : 0; }
        inline bool    cia2NMI() const { return cia2 && cia2->irqLineActive(); }
        inline void cpuCLI(){ return cpu->setCLI(); }
        inline void cpuSEI(){ return cpu->setSEI(); }
        inline void setVicIER(uint8_t m)  { if (vic)      vic->setIERExact(m & 0x0F); }
        inline void setCIA1IER(uint8_t m) { if (cia1) cia1->setIERExact(m & 0x1F); }
        inline void setCIA2IER(uint8_t m) { if (cia2) cia2->setIERExact(m & 0x1F); }

        // ML Monitor Logging enable/disable
        void setLogging(LogSet log, bool enabled);

        // ML Monitor Memory methods
        inline Memory* getMem() { return mem; }
        inline uint8_t readRAM(uint16_t address) { return mem->read(address); }
        inline void writeRAM(uint16_t address, uint8_t value) { mem->write(address, value); }
        inline void writeRAMDirect(uint16_t address, uint8_t value) { mem->writeDirect(address, value); }

        // ML Monitor PLA
        inline std::string plaGetState() { return pla ? pla->describeMode() : "PLA not attached\n"; }
        inline std::string plaGetAddressInfo(uint16_t address) { return pla ? pla->describeAddress(address) : "PLA not attached\n"; }

        // ML Monitor REU
        inline std::string reuClearRAM() { return reu ? reu->clearRAM() : "REU not attached\n"; }
        inline std::string reuDumpRAM(uint32_t address, uint32_t count) const { return reu ? reu->dumpRAM(address, count) : "REU not attached\n"; }
        inline std::string reuDumpRegs() const { return reu ? reu->dumpRegs() : "REU not attached\n"; }
        inline std::string reuDumpStatus() const { return reu ? reu->dumpStatus() : "REU not attached\n"; }
        inline std::string reuFillRAM(uint32_t address, uint32_t count, uint8_t value) { return reu ? reu->fillRAM(address, count, value) : "REU not attached\n"; }
        inline std::string reuIRQ() const { return reu ? reu->dumpIRQStatus() : "REU not attached\n"; }
        inline std::string reuPeekRAM(uint32_t address) const { return reu ? reu->peekRAM(address) : "REU not attached\n"; }
        inline std::string reuPokeRAM(uint32_t address, uint8_t value) const { return reu ? reu->pokeRAM(address, value) : "REU not attached\n"; }
        inline std::string reuSelfTest() { return reu ? reu->selfTest() : "REU not attached\n"; }

        // ML Monitor SID Register Dumps
        inline std::string dumpSIDRegs() const { return sid ? sid->dumpRegisters("all") : "SID not attached\n"; }
        inline std::string dumpSIDVoice1() const { return sid ? sid->dumpRegisters("voice1") : "SID not attached\n"; }
        inline std::string dumpSIDVoice2() const { return sid ? sid->dumpRegisters("voice2") : "SID not attached\n"; }
        inline std::string dumpSIDVoice3() const { return sid ? sid->dumpRegisters("voice3") : "SID not attached\n"; }
        inline std::string dumpSIDVoices() const { return sid ? sid->dumpRegisters("voices") : "SID not attached\n"; }
        inline std::string dumpSIDFilter() const { return sid ? sid->dumpRegisters("filter") : "SID not attached\n"; }
        inline std::string dumpSIDAudio() const  { return sid ? sid->dumpAudioStats() : "SID not attached\n"; }
        inline std::string dumpSIDCutoffTable() const { return sid ? sid->dumpCutoffTable() : "SID not attached\n"; }
        inline void resetSIDAudioStats() { if (sid) sid->resetAudioStats(); }

        // ML Monitor VIC-II methods
        inline std::string vicGetModeName() { return vic ? vic->decodeModeName() : "VIC not attached\n"; }
        inline std::string getCurrentVICBanks() { return vic ? vic->getVICBanks() : "VIC not attached\n"; }
        inline uint8_t getCurrentRaster() { return vic->getCurrentRaster(); }
        inline std::string vicDumpRasterPixelCompositionDebug(int raster, int x0, int x1) const { return vic ?
                                                                    vic->dumpRasterPixelCompositionDebug(raster, x0, x1) : "VIC not available"; }

        std::string vicDumpBackgroundRowDebug(int raster) const;
        std::string vicDumpBackgroundCellDebug(int raster, int col) const;
        std::string vicDumpAllRasterEvents() const;
        std::string vicDumpRasterEventsSummary() const;
        std::string vicDumpRasterEvents(int raster) const;
        std::string vicDumpRasterRowState(int raster) const;
        std::string VicDumpBadlineTimelineAroundRaster(int centerRaster) const;
        std::string vicDumpBorderWindowAroundCurrentRaster() const;
        std::string vicDumpBorderWindowAroundRaster(int centerRaster) const;
        std::string vicDumpRasterFetchMap(int raster) const;
        std::string vicDumpSpriteDmaState() const;
        std::string vicDumpCurrentCycleDebug() const;
        std::string vicDumpCycleDebugFor(int raster, int cycle) const;
        std::string vicDumpRegs(const std::string& group) const;
        std::string vicDumpBadlineState() const;
        std::string vicDumpBorderState() const;
        void vicFFRaster(uint8_t targetRaster);

    protected:

    private:

        // Non-owning pointers
        Cartridge* cart;
        Cassette* cass;
        CIA1* cia1;
        CIA2* cia2;
        Computer* comp;
        CPU* cpu;
        IECBUS* bus;
        IO* io;
        IRQLine* irq;
        Keyboard* keyb;
        Logging* logger;
        Memory* mem;
        PLA* pla;
        REU* reu;
        SID* sid;
        Vic* vic;

        // ML Monitor IRQ snapshot
        IRQSnapshot snapshot;

        // Helpers
        std::string jamModeToString() const;
        std::string decodeDriveStatus(Drive::DriveStatus status);
};

#endif // MLMONITORBACKEND_H
