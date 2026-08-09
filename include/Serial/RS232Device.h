// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef RS232DEVICE_H
#define RS232DEVICE_H

#include <cstdint>
#include <queue>
#include <sstream>
#include <string>
#include "StateReader.h"
#include "StateWriter.h"

class RS232Device
{
    public:
        RS232Device();
        virtual ~RS232Device();

        enum class Parity : uint8_t
        {
            None,
            Odd,
            Even
        };

        enum class FlowControl
        {
            None,
            RTS_CTS
        };

        struct RS232Config
        {
            uint32_t baud = 300;
            uint8_t dataBits = 8;
            uint8_t stopBits = 1;
            Parity parity = Parity::None;
            FlowControl flowControl = FlowControl::None;
        };

        // Pointer attachment
        inline void attachPeerDevice(RS232Device* peer) { this->peer = peer; }
        inline void detachPeerDevice() { peer = nullptr; }

        // State management
        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        void reset();

        // Setters
        inline void setCTS(bool state) { cts = state; }
        inline void setRXD(bool state) { rxd = state; }
        inline void setDSR(bool state) { dsr = state; }
        inline void setDCD(bool state) { dcd = state; }
        inline void setRI(bool state) { ri = state; }

        void setTXD(bool state);
        void setDTR(bool state);
        void setRTS(bool state);

        // Getters
        inline bool getRXD() const { return rxd; }
        inline bool getTXD() const { return txd; }
        inline bool getDSR() const { return dsr; }
        inline bool getCTS() const { return cts; }
        inline bool getRI() const { return ri; }
        inline bool getDCD() const { return dcd; }
        inline bool hasParityError() const { return parityError; }
        inline bool hasFramingError() const { return framingError; }

        void clearReceiveErrors();

        void tick(uint32_t cyclesElapsed);

        void setClockRate(double hz);
        void setConfig(const RS232Config& cfg);

        // Transmit
        void queueTransmitByte(uint8_t value);
        bool isTransmitIdle() const;

        // Receive
        bool hasReceivedByte() const;
        bool popReceivedByte(uint8_t& value);

        // ML Monitor
        std::string debugString() const;
        std::string selfTest(uint8_t testByte, Parity parity);
        std::string selfTestMulti();
        std::string selfTestFormats();

    protected:

    private:
        // Non-owning Pointers
        RS232Device* peer = nullptr;

        enum class TxState : uint8_t
        {
            Idle,
            StartBit,
            DataBits,
            ParityBit,
            StopBit
        };

        enum class RxState : uint8_t
        {
            Idle,
            StartBit,
            DataBits,
            ParityBit,
            StopBit
        };

        RS232Config config;

        uint64_t cycleAccumulator;
        int rxBitIndex;
        uint8_t rxShift;
        bool lastRXD;

        bool dtr;
        bool dsr;
        bool rts;
        bool txd;
        bool rxd;
        bool cts;
        bool dcd;
        bool ri;

        bool parityError;
        bool framingError;

        double clockHz;
        double cyclesPerBit;

        TxState txState;

        std::queue<uint8_t> txBytes;
        double txCountdown;
        uint8_t txShift;
        uint8_t txOriginalByte;
        int txBitIndex;

        double rxCountdown;
        RxState rxState;
        std::queue<uint8_t> rxBytes;

        // Helpers
        void tickTX(uint32_t cyclesElapsed);
        void tickRX(uint32_t cyclesElapsed);
        bool calculateParity(uint8_t value) const;
};

#endif // RS232DEVICE_H
