// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IDE64RTC_H
#define IDE64RTC_H

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
        struct RTCState
        {
            uint8_t seconds;
            uint8_t minutes;
            uint8_t hours;
            uint8_t dayOfWeek;
            uint8_t dayOfMonth;
            uint8_t month;
            uint16_t year;
        } rtcState;

        struct WireState
        {
            uint8_t latch = 0;
            bool clkSeen = false;
            bool dataIn = false;
        } wireState;
};

#endif // IDE64RTC_H
