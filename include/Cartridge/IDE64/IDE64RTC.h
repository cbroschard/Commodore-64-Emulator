// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IDE64RTC_H
#define IDE64RTC_H

#include <array>
#include <cstdint>
#include "Common/BCD.h"
#include "StateReader.h"
#include "StateWriter.h"

class IDE64RTC
{
    public:
        IDE64RTC();
        ~IDE64RTC();

        void reset();

        void setChipEnabled(bool enabled);

        uint8_t readByte();
        void writeByte(uint8_t value);

        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

        bool savePersistence(const std::string& path) const;
        bool loadPersistence(const std::string& path);

    protected:

    private:
        static constexpr size_t CMOS_RAM_SIZE = 31;

        std::array<uint8_t, CMOS_RAM_SIZE> cmosRAM{};

        enum class TransferPhase : uint8_t
        {
            Command,
            ReadData,
            WriteData,
            Ignore
        };

        struct RTCState
        {
            uint8_t seconds         = 0;
            uint8_t minutes         = 0;
            uint8_t hours           = 0;
            uint8_t dayOfWeek       = 1;
            uint8_t dayOfMonth      = 1;
            uint8_t month           = 1;
            uint16_t year           = 2000;

            uint8_t writeProtect    = 0;
            uint8_t trickleCharger  = 0;

            bool clockHalted        = false;

            bool hourMode12 = false;
            bool hourPM = false;
        } rtcState;

        struct WireState
        {
            bool chipEnabled = false;

            TransferPhase phase = TransferPhase::Command;

            uint8_t shiftRegister = 0;
            uint8_t outputShiftRegister = 0;
            uint8_t bitCount = 0;

            uint8_t transferIndex = 0;

            uint8_t command = 0;
            uint8_t address = 0;

            bool ramSelected = false;
            bool readOperation = false;
            bool burstOperation = false;

            bool dataOut = true;
        } wireState;

        void decodeCommand(uint8_t command);

        uint8_t readClockRegister(uint8_t address) const;
        void writeClockRegister(uint8_t address, uint8_t value);
};

#endif // IDE64RTC_H
