// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IECBUS_H
#define IECBUS_H

// Forward declarations
class CIA2;

#include <cstdint>
#include <vector>
#include <map>
#include "IECTypes.h"
#include "Logging.h"
#include "Peripheral.h"

class IECBUS
{
    public:
        IECBUS();
        virtual ~IECBUS();

        // State of IEC bus
        enum class State {IDLE, ATTENTION, TALK, LISTEN, UNLISTEN, UNTALK} currentState;

        // Pointers
        inline void attachCIA2Instance(CIA2* cia2object) { this->cia2object = cia2object; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }

        // Bus state getters
        inline bool getAtnLine() const { return busLines.atn; }
        inline bool getClkLine() const { return busLines.clk; }
        inline bool getDataLine() const { return busLines.data; }

        // Line state management called by CIA1 and CIA2
        void setClkLine(bool state); // CLK line state (via CIA1 Port B bit 7)
        void setDataLine(bool state); // C64 drives DATA line state (via CIA1 Port B bit 6)
        void setAtnLine(bool state);  // C64 drives ATN line state (via CIA2 Port A bit 3)
        void setSrqLine(bool state); // C64 drives SRQ line state

        // Return value: true=high/released, false=low/asserted
        inline bool readDataLine() const { return busLines.data; } // Reads final DATA state (for CIA1 Port B bit 6 read)
        inline bool readClkLine() const { return busLines.clk; }   // Reads final CLK state (for CIA1 Port B bit 7 read)
        inline bool readAtnLine() const { return busLines.atn; }   // Reads final ATN state
        inline bool readSrqLine() const { return line_srqin; }  // Reads final SRQ state (for CIA2 Port A bit 2 read)

        // Peripheral Interaction (Called by Peripheral instances)
        void peripheralControlClk(Peripheral* device, bool state);
        void peripheralControlData(Peripheral* device, bool state);
        void peripheralControlAtn(Peripheral* device, bool state);
        void peripheralControlSrq(Peripheral* device, bool state);

        // Device registration
        void registerDevice(int deviceNumber, Peripheral* device);
        void unregisterDevice(int deviceNumber);

        // IEC commands
        void listen(int deviceNumber);
        void unListen(int deviceNumber);
        void talk(int deviceNumber);
        void unTalk(int deviceNumber);
        void secondaryAddress(uint8_t devNum, uint8_t sa);

        // Main emulation cycle
        void tick(uint64_t cyclesPassed);

        // ML Monitor Functions
        inline IECBusLines getBusLines() const { return busLines; }
        inline bool getSRQLine() const { return line_srqin; }
        inline bool getC64DrivesAtnLow() const { return c64DrivesAtnLow; }
        inline bool getC64DrivesClkLow() const { return c64DrivesClkLow; }
        inline bool getC64DrivesDataLow() const { return c64DrivesDataLow; }
        inline bool getPeripheralDrivesAtnLow() const { return peripheralDrivesAtnLow; }
        inline bool getPeripheralDrivesClkLow() const { return peripheralDrivesClkLow; }
        inline bool getPeripheralDrivesDataLow() const { return peripheralDrivesDataLow; }
        inline State getState() const { return currentState; }
        inline Peripheral* getCurrentTalker() const { return currentTalker; }
        const std::vector<Peripheral*>& getCurrentListeners() const { return currentListeners; }
        const std::map<int, Peripheral*>& getDevices() const { return devices; }

    protected:

    private:

        // Non-owning pointers
        CIA2* cia2object;
        Logging* logger;
        Peripheral* currentTalker;

        // Internal state
        IECBusLines busLines;
        bool line_srqin; // SRQ Line state (true=high/false=inactive)

        // Driver intentions (true = intends to drive LOW)
        bool c64DrivesAtnLow;
        bool c64DrivesClkLow;
        bool c64DrivesDataLow;
        bool peripheralDrivesClkLow;
        bool peripheralDrivesDataLow;
        bool peripheralDrivesAtnLow;

        bool lastClk;
        bool lastData;

        // Peripheral Tracking
        std::map<int, Peripheral*> devices;
        std::vector<Peripheral*> currentListeners;

        // Helper Methods
        void updateBusState();
        void updateSrqLine();  // Polls peripherals for SRQ status
};

#endif // IECBUS_H
