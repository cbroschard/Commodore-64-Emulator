// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MOS6551_H
#define MOS6551_H

#include <cstdint>
#include "StateReader.h"
#include "StateWriter.h"

class RS232Device;
class RS232Endpoint;

class MOS6551
{
    public:
        explicit MOS6551(RS232Device& serial);
        virtual ~MOS6551();

        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        inline void attachEndpoint(RS232Endpoint* endpoint) { this->endpoint = endpoint; }
        inline void detachEndpoint() { endpoint = nullptr; }

        inline bool isExternalBaudSelected() const { return (controlRegister & CTRL_SBR_MASK) == 0; }

        void reset();
        void tick(uint32_t cycles);

        uint8_t read(uint16_t reg);
        uint8_t peek(uint16_t reg) const;
        void write(uint16_t reg, uint8_t value);

        // Getters
        inline bool getIRQ() const { return irq; }
        inline uint8_t getStatusRegister() const { return statusRegister; }
        inline uint8_t getCommandRegister() const { return commandRegister; }
        inline uint8_t getControlRegister() const { return controlRegister; }
        inline bool isTxBusy() const { return txBusy; }
        inline double getTxCountdown() const { return txCountdown; }
        inline uint8_t getTransmitData() const { return transmitData; }
        inline bool hasEndpoint() const { return endpoint != nullptr; }

        // Setters
        void setBaudMultiplier(double multiplier);

    private:
        RS232Device& serial;
        RS232Endpoint* endpoint;

        // Status Register
        static constexpr uint8_t STATUS_IRQ  = 0x80;
        static constexpr uint8_t STATUS_DSR  = 0x40;
        static constexpr uint8_t STATUS_DCD  = 0x20;
        static constexpr uint8_t STATUS_TDRE = 0x10;
        static constexpr uint8_t STATUS_RDRF = 0x08;
        static constexpr uint8_t STATUS_OVRN = 0x04;
        static constexpr uint8_t STATUS_FE   = 0x02;
        static constexpr uint8_t STATUS_PE   = 0x01;

        // Command Register
        static constexpr uint8_t CMD_DTR      = 0x01;
        static constexpr uint8_t CMD_IRD      = 0x02;
        static constexpr uint8_t CMD_TIC_MASK = 0x0C;
        static constexpr uint8_t CMD_REM      = 0x10;
        static constexpr uint8_t CMD_PME      = 0x20;
        static constexpr uint8_t CMD_PMC_MASK = 0xC0;

        // Control Register
        static constexpr uint8_t CTRL_SBR_MASK = 0x0F;
        static constexpr uint8_t CTRL_RCS      = 0x10;
        static constexpr uint8_t CTRL_WL_MASK  = 0x60;
        static constexpr uint8_t CTRL_SBN      = 0x80;

        uint8_t receiveData;
        uint8_t transmitData;

        uint8_t statusRegister;
        uint8_t commandRegister;
        uint8_t controlRegister;

        bool irq;

        bool rxd;
        bool txd;

        bool rts;
        bool cts;
        bool dtr;
        bool dcd;
        bool dsr;

        bool lastDCD;
        bool lastDSR;

        bool latchedDCD;
        bool latchedDSR;

        bool modemStatusLatched;

        bool echoPending;
        bool echoLevel;
        double echoCountdown;

        double baudMultiplier;

        bool txBusy;
        double txCountdown;

        bool rxBusy;
        double rxCountdown;
        uint8_t rxPendingByte;

        void receiveByte(uint8_t value);

        // Helpers
        void updateStatus();
        void updateCommand();
        void updateIRQ();
        void updateControl();

        void programmedReset();

        uint32_t decodeBaudRate() const;
        uint8_t decodeWordLength() const;
        double decodeStopBits() const;

        double characterCycles() const;
};

#endif // MOS6551_H
