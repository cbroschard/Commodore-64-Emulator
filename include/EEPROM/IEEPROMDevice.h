// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IEEPROMDEVICE_H
#define IEEPROMDEVICE_H

#include <string>
#include "StateReader.h"
#include "StateWriter.h"

class IEEPROMDevice
{
    public:
        IEEPROMDevice();
        virtual ~IEEPROMDevice();

        virtual void reset() = 0;

        virtual void setCS(bool level) = 0;
        virtual void setCLK(bool level) = 0;
        virtual void setDI(bool level) = 0;

        virtual bool getDO() const = 0;

        virtual void saveState(StateWriter& wrtr) const = 0;
        virtual bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) = 0;

        virtual void setLines(bool cs, bool clk, bool di)
        {
            setCS(cs);
            setCLK(clk);
            setDI(di);
        }

        virtual bool savePersistence(const std::string& path) const = 0;
        virtual bool loadPersistence(const std::string& path) = 0;

    protected:

    private:
};

#endif // IEEPROMDEVICE_H
