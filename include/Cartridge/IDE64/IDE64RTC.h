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
#include "StateReader.h"
#include "StateWriter.h"

class IDE64RTC
{
    public:
        IDE64RTC();
        ~IDE64RTC();

        void reset();

        uint8_t readByte() const;
        void writeByte(uint8_t value);

        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

    protected:

    private:
        static constexpr size_t CMOS_RAM_SIZE = 31;

        std::array<uint8_t, CMOS_RAM_SIZE> cmosRAM{};

        struct RTCState
        {
            uint8_t seconds    = 0;
            uint8_t minutes    = 0;
            uint8_t hours      = 0;
            uint8_t dayOfWeek  = 1;
            uint8_t dayOfMonth = 1;
            uint8_t month      = 1;
            uint16_t year      = 0;
        } rtcState;

        struct WireState
        {
            uint8_t latch = 0;
            bool clkSeen = false;
            bool dataIn = false;
        } wireState;
};

#endif // IDE64RTC_H
